namespace LivePT {

    static IDispatch* pDTE = nullptr;

    std::string ConvertWStringToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) return "";

        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (size_needed <= 0) return "";

        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, str.data(), size_needed, NULL, NULL);
        str.resize(size_needed - 1);

        return str;
    }

    HRESULT AutoWrap(int autoType, VARIANT* pvResult, IDispatch* pDisp, LPCOLESTR ptName, int cArgs...) {
        if (!pDisp) return E_FAIL;

        va_list marker;
        va_start(marker, cArgs);

        DISPID dispID;
        LPOLESTR writableName = const_cast<LPOLESTR>(ptName);
        HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &writableName, 1, LOCALE_USER_DEFAULT, &dispID);
        if (FAILED(hr)) {
            va_end(marker);
            return hr;
        }

        DISPPARAMS dp = { NULL, NULL, 0, 0 };
        DISPID dispidNamed = DISPATCH_PROPERTYPUT;
        VARIANT* pArgs = nullptr;

        if (cArgs > 0) {
            pArgs = new VARIANT[cArgs];
            for (int i = cArgs - 1; i >= 0; i--) {
                pArgs[i] = va_arg(marker, VARIANT);
            }
            dp.cArgs = cArgs;
            dp.rgvarg = pArgs;
        }

        if (autoType & DISPATCH_PROPERTYPUT) {
            dp.cNamedArgs = 1;
            dp.rgdispidNamedArgs = &dispidNamed;
        }

        int finalType = autoType;
        if ((autoType & DISPATCH_PROPERTYGET) && cArgs > 0) {
            finalType |= DISPATCH_METHOD;
        }

        hr = pDisp->Invoke(dispID, IID_NULL, LOCALE_USER_DEFAULT, finalType, &dp, pvResult, NULL, NULL);

        if (pArgs) delete[] pArgs;
        va_end(marker);
        return hr;
    }

    std::wstring ToLower(std::wstring str) {
        std::transform(str.begin(), str.end(), str.begin(), ::towlower);
        return str;
    }

    DWORD GetStudioProcessId() {
        DWORD currentPid = GetCurrentProcessId();
        DWORD parentPid = 0;
        std::wstring parentName = L"";

        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == currentPid) {
                    parentPid = pe32.th32ParentProcessID;
                    break;
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }

        if (parentPid != 0) {
            if (Process32FirstW(hSnapshot, &pe32)) {
                do {
                    if (pe32.th32ProcessID == parentPid) {
                        parentName = ToLower(pe32.szExeFile);

                        if (parentName.find(L"msvsmon") != std::wstring::npos) {
                            DWORD studioPid = pe32.th32ParentProcessID;
                            CloseHandle(hSnapshot);
                            return studioPid;
                        }
                        break;
                    }
                } while (Process32NextW(hSnapshot, &pe32));
            }
        }

        CloseHandle(hSnapshot);
        return parentPid;
    }

    IDispatch* GetDTEByPid(DWORD targetPid) {
            IRunningObjectTable* pROT = nullptr;
            if (FAILED(GetRunningObjectTable(0, &pROT))) return nullptr;

            IEnumMoniker* pEnumMoniker = nullptr;
            if (FAILED(pROT->EnumRunning(&pEnumMoniker))) { pROT->Release(); return nullptr; }

            IMoniker* pMoniker = nullptr;
            ULONG fetched;
            IDispatch* pTargetDTE = nullptr;

            std::wstring pidTargetStr = L":" + std::to_wstring(targetPid);

            while (pEnumMoniker->Next(1, &pMoniker, &fetched) == S_OK) {
                IBindCtx* pBindCtx = nullptr;
                if (SUCCEEDED(CreateBindCtx(0, &pBindCtx))) {
                    LPOLESTR pDisplayName = nullptr;
                    if (SUCCEEDED(pMoniker->GetDisplayName(pBindCtx, nullptr, &pDisplayName))) {
                        std::wstring name(pDisplayName);

                        if (name.find(L"VisualStudio.DTE") != std::wstring::npos &&
                            name.length() >= pidTargetStr.length() &&
                            name.compare(name.length() - pidTargetStr.length(), pidTargetStr.length(), pidTargetStr) == 0) {

                            IUnknown* pUnk = nullptr;
                            if (SUCCEEDED(pROT->GetObject(pMoniker, &pUnk))) {
                                pUnk->QueryInterface(IID_IDispatch, (void**)&pTargetDTE);
                                pUnk->Release();
                            }
                        }
                        CoTaskMemFree(pDisplayName);
                    }
                    pBindCtx->Release();
                }
                pMoniker->Release();
                if (pTargetDTE) break;
            }

            pEnumMoniker->Release(); pROT->Release();
            return pTargetDTE;
        }

    bool initVsEditor()
        {
            if (pDTE) return true;

            DWORD parentPid = GetStudioProcessId();
            Log("Target Studio PID: " + std::to_string(parentPid));

            pDTE = GetDTEByPid(parentPid);
            if (!pDTE) {
                Log("Failed to connect to the host Visual Studio instance.");
                return false;
            }

            Log("Connected to Visual Studio successfully!");
            return true;
        }

    void ResetDTEConnection() {
            if (pDTE) {
                pDTE->Release();
                pDTE = nullptr;
                Log("[EnvDTE] Connection lost. pDTE reset.");
            }
        }

    void StartUndoTransaction(const std::wstring & name) {
            if (!pDTE) return;

            CComVariant vtUndoContext;
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtUndoContext, pDTE, L"UndoContext", 0)) && vtUndoContext.pdispVal) {

                CComVariant vtIsOpen;
                AutoWrap(DISPATCH_PROPERTYGET, &vtIsOpen, vtUndoContext.pdispVal, L"IsOpen", 0);
                if (vtIsOpen.vt == VT_BOOL && vtIsOpen.boolVal == VARIANT_FALSE) {
                    CComBSTR bstrName(name.c_str());
                    AutoWrap(DISPATCH_METHOD, NULL, vtUndoContext.pdispVal, L"Open", 1, CComVariant(bstrName));
                }
            }
        }

    void EndUndoTransaction() {
            if (!pDTE) return;

            CComVariant vtUndoContext;
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtUndoContext, pDTE, L"UndoContext", 0)) && vtUndoContext.pdispVal) {
                CComVariant vtIsOpen;
                AutoWrap(DISPATCH_PROPERTYGET, &vtIsOpen, vtUndoContext.pdispVal, L"IsOpen", 0);
                if (vtIsOpen.vt == VT_BOOL && vtIsOpen.boolVal == VARIANT_TRUE) {
                    AutoWrap(DISPATCH_METHOD, NULL, vtUndoContext.pdispVal, L"Close", 0);
                }
            }
        }

    int GetParamIndexByTextOrder(const std::wstring& fileText, const std::string& currentActiveFile, long cursorLine, size_t evalIdxInLine) {
        // 1. Находим абсолютное смещение начала текущей строки курсора в файле
        size_t targetLineStartOffset = 0;
        long currentLineIdx = 1;

        while (currentLineIdx < cursorLine && targetLineStartOffset < fileText.length()) {
            size_t nextNL = fileText.find(L'\n', targetLineStartOffset);
            if (nextNL != std::wstring::npos) {
                targetLineStartOffset = nextNL + 1;
                currentLineIdx++;
            }
            else {
                break;
            }
        }

        // 2. Считаем ВСЕ валидные макросы eval от начала файла ДО начала нашей целевой строки
        size_t globalFileCounter = 0;
        size_t searchPos = 0;

        while ((searchPos = fileText.find(L"eval", searchPos)) != std::wstring::npos) {
            // Если мы дошли до текущей строки, останавливаем глобальный подсчет
            if (searchPos >= targetLineStartOffset) {
                break;
            }

            // Проверяем, что это честное слово eval, а не часть другого слова
            bool validLeft = (searchPos == 0 || !iswalnum(fileText[searchPos - 1]) && fileText[searchPos - 1] != L'_');
            bool validRight = (searchPos + 4 >= fileText.length() || !iswalnum(fileText[searchPos + 4]) && fileText[searchPos + 4] != L'_');

            if (validLeft && validRight) {
                // Простая проверка на однострочные комментарии перед макросом
                size_t lineStart = fileText.rfind(L'\n', searchPos);
                if (lineStart == std::wstring::npos) lineStart = 0;
                std::wstring executionBeforeEval = fileText.substr(lineStart, searchPos - lineStart);

                // Если перед eval на этой же строке нет символа комментария
                if (executionBeforeEval.find(L"//") == std::wstring::npos) {
                    globalFileCounter++;
                }
            }
            searchPos += 4;
        }

        // 3. Итоговый индекс — это сумма всех макросов выше + индекс макроса на текущей строке
        size_t finalGlobalFileIndex = globalFileCounter + evalIdxInLine;

        return static_cast<int>(finalGlobalFileIndex);
    }


    inline size_t FindCloseBracket(const std::wstring& text, size_t openBracketPos) {
        if (openBracketPos == std::wstring::npos) return std::wstring::npos;
        int bracketCount = 1;
        int figureCount = 0; // Добавляем счетчик фигурных скобок

        for (size_t k = openBracketPos + 1; k < text.length(); ++k) {
            if (text[k] == L'{') figureCount++;
            if (text[k] == L'}') figureCount--;

            if (text[k] == L'(') bracketCount++;
            if (text[k] == L')') bracketCount--;

            // Закрывающая круглая скобка макроса валидна только тогда, 
            // когда все внутренние фигурные скобки агрегата закрыты!
            if (bracketCount == 0 && figureCount == 0) return k;
        }
        return std::wstring::npos;
    }


    inline std::string GetActiveDocumentPath(IDispatch * pActiveDoc) {
            VARIANT vtFullName; VariantInit(&vtFullName);
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtFullName, pActiveDoc, L"FullName", 0)) &&
                vtFullName.vt == VT_BSTR && vtFullName.bstrVal != nullptr) {
                std::string path = ConvertWStringToUtf8(vtFullName.bstrVal);
                VariantClear(&vtFullName);
                std::replace(path.begin(), path.end(), '\\', '/');
                return path;
            }
            VariantClear(&vtFullName);
            return "";
        }

    inline bool GetCursorCoordinates(IDispatch * pActiveDoc, long& outLine, long& outColumn) {
            VARIANT vtSelection; VariantInit(&vtSelection);
            if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, pActiveDoc, L"Selection", 0)) || vtSelection.vt != VT_DISPATCH || !vtSelection.pdispVal) {
                VariantClear(&vtSelection); return false;
            }

            VARIANT vtActivePoint; VariantInit(&vtActivePoint);
            if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtActivePoint, vtSelection.pdispVal, L"ActivePoint", 0)) || vtActivePoint.vt != VT_DISPATCH || !vtActivePoint.pdispVal) {
                VariantClear(&vtActivePoint); VariantClear(&vtSelection); return false;
            }

            IDispatch* pActivePoint = vtActivePoint.pdispVal;
            VARIANT vtLine; VariantInit(&vtLine);
            VARIANT vtDisplayCol; VariantInit(&vtDisplayCol);

            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtLine, pActivePoint, L"Line", 0))) {
                VARIANT vtTarget; VariantInit(&vtTarget);
                if (SUCCEEDED(VariantChangeType(&vtTarget, &vtLine, 0, VT_I4))) outLine = vtTarget.lVal;
                VariantClear(&vtTarget);
            }
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtDisplayCol, pActivePoint, L"LineCharOffset", 0))) {
                VARIANT vtTarget; VariantInit(&vtTarget);
                if (SUCCEEDED(VariantChangeType(&vtTarget, &vtDisplayCol, 0, VT_I4))) outColumn = vtTarget.lVal;
                VariantClear(&vtTarget);
            }

            VariantClear(&vtDisplayCol); VariantClear(&vtLine);
            VariantClear(&vtActivePoint); VariantClear(&vtSelection);
            return (outLine > 0 && outColumn > 0);
        }

    inline std::wstring DownloadDocumentText(IDispatch * pActiveDoc) {
            std::wstring fileText = L"";
            VARIANT vtTextDoc; VariantInit(&vtTextDoc);
            HRESULT hr = AutoWrap(DISPATCH_PROPERTYGET, &vtTextDoc, pActiveDoc, L"Object", 0);
            if (FAILED(hr) || vtTextDoc.vt != VT_DISPATCH || !vtTextDoc.pdispVal) {
                VariantClear(&vtTextDoc);
                hr = AutoWrap(DISPATCH_METHOD, &vtTextDoc, pActiveDoc, L"Object", 0);
            }
            if (SUCCEEDED(hr) && vtTextDoc.vt == VT_DISPATCH && vtTextDoc.pdispVal != nullptr) {
                VARIANT vtStartPoint; VariantInit(&vtStartPoint);
                VARIANT vtEndPoint; VariantInit(&vtEndPoint);

                if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtStartPoint, vtTextDoc.pdispVal, L"StartPoint", 0)) &&
                    SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtEndPoint, vtTextDoc.pdispVal, L"EndPoint", 0))) {

                    VARIANT vtEditPoint; VariantInit(&vtEditPoint);
                    hr = AutoWrap(DISPATCH_METHOD, &vtEditPoint, vtStartPoint.pdispVal, L"CreateEditPoint", 0);

                    if (SUCCEEDED(hr) && vtEditPoint.vt == VT_DISPATCH && vtEditPoint.pdispVal != nullptr) {
                        VARIANT vtAllText; VariantInit(&vtAllText);
                        hr = AutoWrap(DISPATCH_METHOD, &vtAllText, vtEditPoint.pdispVal, L"GetText", 1, vtEndPoint);

                        if (SUCCEEDED(hr) && vtAllText.vt == VT_BSTR && vtAllText.bstrVal != nullptr) {
                            fileText = vtAllText.bstrVal;
                        }
                        VariantClear(&vtAllText); VariantClear(&vtEditPoint);
                    }
                }
                VariantClear(&vtEndPoint); VariantClear(&vtStartPoint);
            }
            VariantClear(&vtTextDoc);
            return fileText;
        }

    inline size_t GetLineStartOffset(const std::wstring & text, long targetLine) {
            size_t offset = 0; long currentIdx = 1;
            while (currentIdx < targetLine && offset < text.length()) {
                size_t nextNL = text.find(L'\n', offset);
                if (nextNL != std::wstring::npos) { offset = nextNL + 1; currentIdx++; }
                else break;
            }
            return offset;
        }

    inline long GetVisualColumn(const std::wstring & lineText, size_t charIdx) {
            const size_t TAB_SIZE = 4; // VS TAB SIZE
            size_t visualCol = 0;

            for (size_t i = 0; i < charIdx && i < lineText.length(); ++i) {
                if (lineText[i] == L'\t') {
                    visualCol += TAB_SIZE - (visualCol % TAB_SIZE);
                }
                else {
                    visualCol++;
                }
            }
            return static_cast<long>(visualCol) + 1;
        }

    inline bool CheckCursorInsideEval(const std::wstring& fileText, long line, long column, size_t& outEvalIdxInLine, size_t& outTargetEvalAbsolutePos) {
        size_t lineStartOffset = GetLineStartOffset(fileText, line);
        size_t lineEndOffset = fileText.find(L'\n', lineStartOffset);
        if (lineEndOffset == std::wstring::npos) lineEndOffset = fileText.length();

        std::wstring wholeLineText = fileText.substr(lineStartOffset, lineEndOffset - lineStartOffset);

        // Переводим 1-based физический LineCharOffset в 0-based индекс строки
        long currentCursorCharIdx = column - 1;

        size_t posInLine = 0;
        size_t validEvalCount = 0;

        size_t bestEvalPos = std::wstring::npos;
        size_t bestEvalIdx = std::wstring::npos;

        while ((posInLine = wholeLineText.find(L"eval", posInLine)) != std::wstring::npos) {
            bool validLeft = (posInLine == 0 || !iswalnum(wholeLineText[posInLine - 1]) && wholeLineText[posInLine - 1] != L'_');
            bool validRight = (posInLine + 4 >= wholeLineText.length() || !iswalnum(wholeLineText[posInLine + 4]) && wholeLineText[posInLine + 4] != L'_');

            if (validLeft && validRight) {
                // Если начало слова "eval" находится левее или прямо под курсором
                if (static_cast<long>(posInLine) <= currentCursorCharIdx) {
                    bestEvalPos = posInLine;
                    bestEvalIdx = validEvalCount;
                }
                validEvalCount++;
            }
            posInLine += 4;
        }

        if (bestEvalPos != std::wstring::npos) {
            outEvalIdxInLine = bestEvalIdx;
            outTargetEvalAbsolutePos = lineStartOffset + bestEvalPos;
            return true;
        }
        return false;
    }

    bool isMouseDragging();

    // Быстрый хелпер: проверяет, является ли токен чистым целым числом
    inline bool IsWholeNumeric(const std::string& str) {
        if (str.empty()) return false;
        size_t start = (!str.empty() && str[0] == '-') ? 1 : 0;
        if (start == str.length()) return false;

        for (size_t i = start; i < str.length(); ++i) {
            // Прямая проверка ASCII-кода без вызова библиотек Си
            if (str[i] < '0' || str[i] > '9') return false;
        }
        return true;
    }


    // Быстрый хелпер: проверяет, является ли токен валидным вещественным числом (float/double)
    inline bool IsFloatNumeric(std::string str) {
        if (str.empty()) return false;
        if (str.back() == 'f' || str.back() == 'F') str.pop_back();
        double dummy;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), dummy);
        // Возвращает true только если строка успешно распарсилась как число (нет букв переменных)
        return ec == std::errc() && ptr == (str.data() + str.size());
    }

    // Вспомогательный метод разбиения строки аргументов "color{10,20,30}" -> ["10","20","30"]
    inline std::vector<std::string> SplitArgs(const std::string& text) {
        std::vector<std::string> tokens;
        size_t start = text.find('{');
        size_t end = text.find_last_of('}');
        if (start == std::string::npos || end == std::string::npos || end <= start) return tokens;
        std::stringstream ss(text.substr(start + 1, end - start - 1));
        std::string token;
        while (std::getline(ss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t\r\n"));
            token.erase(token.find_last_not_of(" \t\r\n") + 1);
            tokens.push_back(token);
        }
        return tokens;
    }

    // Проверяет, что строка содержит ТОЛЬКО символы, из которых строятся числа и латинские переменные
    inline bool IsLptNumericTextSecure(const std::string& str) {
        if (str.empty()) return false;
        for (char c : str) {
            unsigned char uc = static_cast<unsigned char>(c);

            // Если вышли за ASCII (русские буквы в числах, булах и енамах)
            if (uc > 127) return false;

            // Разрешенные символы для конвейера чисел
            bool isValidChar = (uc >= '0' && uc <= '9') ||
                (uc >= 'a' && uc <= 'z') ||
                (uc >= 'A' && uc <= 'Z') ||
                uc == '_' || uc == '.' || uc == '-' ||
                uc == ',' || uc == ':' || uc == '{' || uc == '}';
            if (!isValidChar) return false;
        }
        return true;
    }


    inline void ParseAndStoreParamValue(const std::wstring& fileText, const std::string& currentActiveFile, long line, size_t evalIdxInLine, size_t targetEvalAbsolutePos) {
        int counterID = GetParamIndexByTextOrder(fileText, currentActiveFile, line, evalIdxInLine);
        if (counterID == -1) return;

        std::string vsLookupKey = currentActiveFile + ":" + std::to_string(counterID);
        int targetId = getID(vsLookupKey);
        if (targetId == -1) return;

        size_t openBracket = fileText.find(L'(', targetEvalAbsolutePos);
        size_t closeBracket = FindCloseBracket(fileText, openBracket);
        if (openBracket == std::wstring::npos || closeBracket == std::wstring::npos) return;

        std::wstring innerValueW = fileText.substr(openBracket + 1, closeBracket - openBracket - 1);
        std::string innerValueA = ConvertWStringToUtf8(innerValueW);
        innerValueA.erase(0, innerValueA.find_first_not_of(" \t\r\n"));
        innerValueA.erase(innerValueA.find_last_not_of(" \t\r\n") + 1);

        std::string cleanQuery = innerValueA;
        cleanQuery.erase(std::remove_if(cleanQuery.begin(), cleanQuery.end(), [](unsigned char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            }), cleanQuery.end());

        // ===================================================================
        // 🛡️ УМНАЯ МОНОЛИТНАЯ ЗАЩИТА: ПРОПУСКАЕМ СТРИНГИ, ФИЛЬТРУЕМ ЧИСЛА 🛡️
        // ===================================================================
        std::string tName = paramDesc[targetId].typeName;
        bool isString = (tName.find("string") != std::string::npos || tName.find("char*") != std::string::npos);

        if (!isString) {
            // Если это число, бул, енам или агрегат, жестко проверяем на мусор и кириллицу
            if (!IsLptNumericTextSecure(cleanQuery)) {
                return; // Нашли невалидный мусор или кириллицу -> Полный БАЙПАС
            }
        }
        // ===================================================================

        if (paramDesc[targetId].isEnum) {
            // Энамы пропускаем как есть, их UpdateParamValue разрулит по именам элементов
        }
        else if (cleanQuery.find('{') != std::string::npos) {
            // АГРЕГАТЫ (Сборка без хардкода строк "color")
            auto tokens = SplitArgs(cleanQuery);
            if (!tokens.empty()) {
                size_t bytesPerComponent = sizeof(paramDesc[targetId].value) / tokens.size();
                bool isVar = false;

                for (size_t i = 0; i < tokens.size(); ++i) {
                    if (!IsWholeNumeric(tokens[i]) && !IsFloatNumeric(tokens[i])) { isVar = true; break; }

                    if (bytesPerComponent == 1) {
                        long long val = std::strtoll(tokens[i].c_str(), nullptr, 10);
                        if (val < 0) val = 0; if (val > 255) val = 255;
                        tokens[i] = std::to_string(val);
                    }
                    else if (bytesPerComponent == 4) {
                        if (tokens[i].find('.') != std::string::npos || tokens[i].back() == 'f' || tokens[i].back() == 'F') {
                            double val = std::strtod(tokens[i].c_str(), nullptr);
                            if (val > (std::numeric_limits<float>::max)()) val = (std::numeric_limits<float>::max)();
                            if (val < -(std::numeric_limits<float>::max)()) val = -(std::numeric_limits<float>::max)();
                            tokens[i] = std::to_string(val) + "f";
                        }
                        else {
                            long long val = std::strtoll(tokens[i].c_str(), nullptr, 10);
                            if (val > INT_MAX) val = INT_MAX; if (val < INT_MIN) val = INT_MIN;
                            tokens[i] = std::to_string(val);
                        }
                    }
                }

                if (isVar) return; // Уперлись в переменную -> Полный БАЙПАС агрегата

                // ДИНАМИЧЕСКАЯ СБОРКА СТРОКИ: Вырезаем оригинальное имя типа до скобки {
                std::string reconstructed = "";
                size_t firstBrace = cleanQuery.find('{');
                if (firstBrace != std::string::npos) {
                    reconstructed = cleanQuery.substr(0, firstBrace + 1);
                }

                for (size_t i = 0; i < tokens.size(); ++i) {
                    reconstructed += tokens[i] + (i == tokens.size() - 1 ? "}" : ",");
                }
                innerValueA = reconstructed;
            }
        }
        else {
            // АТОМАРНЫЕ ТИПЫ (int, float, double, bool)
            if (paramDesc[targetId].typeName == "bool") {
                // Булы пропускаем без числовых валидаций, так как мусор (trуе) уже отсечен на входе
            }
            else {
                if (!IsWholeNumeric(cleanQuery) && !IsFloatNumeric(cleanQuery)) {
                    return; // Имя переменной -> БАЙПАС
                }

                if (cleanQuery.find('.') != std::string::npos || cleanQuery.back() == 'f' || cleanQuery.back() == 'F') {
                    double val = std::strtod(cleanQuery.c_str(), nullptr);
                    if (val > (std::numeric_limits<float>::max)() || _isnan(val)) innerValueA = std::to_string((std::numeric_limits<float>::max)()) + "f";
                    else innerValueA = cleanQuery;
                }
                else {
                    long long val = std::strtoll(cleanQuery.c_str(), nullptr, 10);
                    if (val > INT_MAX) innerValueA = std::to_string(INT_MAX);
                    else if (val < INT_MIN) innerValueA = std::to_string(INT_MIN);
                    else innerValueA = cleanQuery;
                }
            }
        }

        UpdateParamValue(targetId, innerValueA);
    }




    void vsEditor() {


            if (isMouseDragging()) return;

            if (!initVsEditor()) return;


            VARIANT vtActiveDoc; VariantInit(&vtActiveDoc);
            HRESULT hr = pDTE ? AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0) : E_FAIL;
            if (hr == CO_E_OBJNOTCONNECTED || hr == RPC_E_DISCONNECTED || hr == E_ACCESSDENIED) { ResetDTEConnection(); return; }
            if (hr == RPC_E_CALL_REJECTED || hr == 0x8001010A || FAILED(hr) || !vtActiveDoc.pdispVal) { VariantClear(&vtActiveDoc); return; }

            IDispatch* pActiveDoc = vtActiveDoc.pdispVal;

            std::string currentActiveFile = GetActiveDocumentPath(pActiveDoc);
            if (currentActiveFile.empty()) { VariantClear(&vtActiveDoc); return; }

            long line = 0, column = 0;
            if (!GetCursorCoordinates(pActiveDoc, line, column)) { VariantClear(&vtActiveDoc); return; }

            std::wstring fileText = DownloadDocumentText(pActiveDoc);
            if (fileText.empty()) { VariantClear(&vtActiveDoc); return; }

            size_t evalIdxInLine = 0;
            size_t targetEvalAbsolutePos = 0;

            if (!CheckCursorInsideEval(fileText, line, column, evalIdxInLine, targetEvalAbsolutePos)) {
                VariantClear(&vtActiveDoc);
                return;
            }

            ParseAndStoreParamValue(fileText, currentActiveFile, line, evalIdxInLine, targetEvalAbsolutePos);

            VariantClear(&vtActiveDoc);

        }

    
}