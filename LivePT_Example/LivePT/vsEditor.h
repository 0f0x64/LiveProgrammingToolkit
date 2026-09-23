namespace LivePT {

    static IDispatch* pDTE = nullptr;

    inline size_t GetLineStartOffset(const std::wstring& text, long targetLine);
    inline std::wstring DownloadCurrentLineText(IDispatch* pActiveDoc);
    inline size_t FindCloseBracket(const std::wstring& text, size_t openBracketPos);

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

    inline DWORD GetStudioProcessId() {
        DWORD currentPid = GetCurrentProcessId();

        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

        DWORD searchPid = currentPid;
        DWORD studioPid = 0;

        // Счётчик для защиты от вечного цикла (максимум 32 уровня вложенности процессов)
        int safetyCounter = 32;

        while (searchPid != 0 && --safetyCounter > 0) {
            PROCESSENTRY32W pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32W);

            DWORD parentPid = 0;
            std::wstring procName = L"";

            // Сбрасываем итератор снимка на начало
            if (Process32FirstW(hSnapshot, &pe32)) {
                do {
                    if (pe32.th32ProcessID == searchPid) {
                        parentPid = pe32.th32ParentProcessID;
                        procName = ToLower(pe32.szExeFile);
                        break;
                    }
                } while (Process32NextW(hSnapshot, &pe32));
            }

            // Если текущий процесс в цепочке — это сама Студия, забираем его PID
            if (procName.find(L"devenv") != std::wstring::npos) {
                studioPid = searchPid;
                break;
            }

            // Защита от некорректных данных ОС
            if (parentPid == 0 || parentPid == searchPid) {
                break;
            }

            searchPid = parentPid;
        }

        CloseHandle(hSnapshot);
        return studioPid;
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

    // 2. СИНХРОННЫЙ С КОРНЕМ СЧЕТЧИК ИНДЕКСОВ С НАЧАЛА ФАЙЛА
    inline int GetParamIndexByTextOrder(const std::wstring& fileText, const std::string& currentActiveFile, long cursorLine, size_t evalIdxInLine) {
        size_t globalFileCounter = 0;
        size_t pos = 0;
        size_t targetLineOffset = GetLineStartOffset(fileText, cursorLine);

        while ((pos = fileText.find(L"eval", pos)) != std::wstring::npos) {
            bool validLeft = (pos == 0 || !iswalnum(fileText[pos - 1]) && fileText[pos - 1] != L'_');
            bool validRight = (pos + 4 >= fileText.length() || !iswalnum(fileText[pos + 4]) && fileText[pos + 4] != L'_');

            if (validLeft && validRight) {
                // Честная проверка на комментарии перед инкрементом глобального счетчика
                size_t lineStart = fileText.rfind(L'\n', pos);
                lineStart = (lineStart == std::wstring::npos) ? 0 : lineStart + 1;
                std::wstring prevCode = fileText.substr(lineStart, pos - lineStart);

                size_t firstNonSpace = prevCode.find_first_not_of(L" \t");
                bool isCommented = (firstNonSpace != std::wstring::npos && prevCode[firstNonSpace] == L'#') ||
                    (prevCode.find(L"//") != std::wstring::npos);

                if (!isCommented) {
                    if (pos < targetLineOffset) {
                        globalFileCounter++;
                    }
                    else {
                        break; // Дошли до строки курсора, останавливаем сквозной счет
                    }
                }
            }
            pos += 4;
        }
        return static_cast<int>(globalFileCounter + evalIdxInLine);
    }

    inline size_t FindCloseBracket(const std::wstring & text, size_t openBracketPos) {
            if (openBracketPos == std::wstring::npos) return std::wstring::npos;
            int bracketCount = 1;
            for (size_t k = openBracketPos + 1; k < text.length(); ++k) {
                if (text[k] == L'(') bracketCount++;
                if (text[k] == L')') bracketCount--;
                if (bracketCount == 0) return k;
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
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtDisplayCol, pActivePoint, L"DisplayColumn", 0))) {
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

    // 1. ПРОВЕРКА КУРСОРA С ОДНОПРОХОДНЫМ ЛИНЕЙНЫМ ПОИСКОМ И УЧЕТОМ КОММЕНТАРИЕВ
    inline bool CheckCursorInsideEval(const std::wstring& fileText, long line, long column, size_t& outEvalIdxInLine, size_t& outTargetEvalAbsolutePos) {
        // Вычисляем абсолютную позицию курсора в файле
        size_t currentLineStart = GetLineStartOffset(fileText, line);
        size_t globalCursorPos = currentLineStart;

        std::wstring lineText = DownloadCurrentLineText(pDTE);
        if (lineText.empty()) {
            size_t nextNL = fileText.find(L'\n', currentLineStart);
            if (nextNL == std::wstring::npos) nextNL = fileText.length();
            lineText = fileText.substr(currentLineStart, nextNL - currentLineStart);
        }

        long currentVisualCol = 1;
        for (size_t i = 0; i < lineText.length(); ++i) {
            if (currentVisualCol >= column) { globalCursorPos += i; break; }
            currentVisualCol += (lineText[i] == L'\t') ? (4 - ((currentVisualCol - 1) % 4)) : 1;
        }

        size_t pos = 0;
        while ((pos = fileText.find(L"eval", pos)) != std::wstring::npos) {
            bool validLeft = (pos == 0 || !iswalnum(fileText[pos - 1]) && fileText[pos - 1] != L'_');
            bool validRight = (pos + 4 >= fileText.length() || !iswalnum(fileText[pos + 4]) && fileText[pos + 4] != L'_');

            if (validLeft && validRight) {
                // ПРОВЕРКА НА КОММЕНТАРИИ И ДИРЕКТИВЫ ПРЕПРОЦЕССОРА НА ТЕКУЩЕЙ СТРОКЕ:
                // Ищем начало строки, на которой расположен данный конкретный eval
                size_t lineStart = fileText.rfind(L'\n', pos);
                lineStart = (lineStart == std::wstring::npos) ? 0 : lineStart + 1;
                std::wstring prevCode = fileText.substr(lineStart, pos - lineStart);

                size_t firstNonSpace = prevCode.find_first_not_of(L" \t");
                bool isCommented = (firstNonSpace != std::wstring::npos && prevCode[firstNonSpace] == L'#') ||
                    (prevCode.find(L"//") != std::wstring::npos);

                if (!isCommented) {
                    size_t openBracket = fileText.find(L'(', pos + 4);
                    if (openBracket != std::wstring::npos) {
                        size_t globalCloseBracket = FindCloseBracket(fileText, openBracket);

                        // Если курсор зажат строго внутри скобок этого макроса (однострочного или многострочного)
                        if (globalCursorPos >= pos && globalCursorPos <= globalCloseBracket) {
                            outTargetEvalAbsolutePos = pos;

                            // Считаем, какой по счету это eval строго на его родной строке начала
                            outEvalIdxInLine = 0;
                            size_t scanPos = lineStart;
                            while ((scanPos = fileText.find(L"eval", scanPos)) != std::wstring::npos && scanPos < pos) {
                                bool sLeft = (scanPos == 0 || !iswalnum(fileText[scanPos - 1]) && fileText[scanPos - 1] != L'_');
                                bool sRight = (scanPos + 4 >= fileText.length() || !iswalnum(fileText[scanPos + 4]) && fileText[scanPos + 4] != L'_');
                                if (sLeft && sRight) {
                                    outEvalIdxInLine++;
                                }
                                scanPos += 4;
                            }
                            return true;
                        }
                    }
                }
            }
            pos += 4;
        }
        return false;
    }


    // 3. ПОЛНЫЙ ВЫРЕЗАТЕЛЬ СКОБОК ДЛЯ ПАРСИНГА С КЛАВИАТУРЫ
    inline void ParseAndStoreParamValue(const std::wstring& fileText, const std::string& currentActiveFile, long line, size_t evalIdxInLine, size_t targetEvalAbsolutePos) {
        long evalRealLine = 1;
        size_t lOffset = 0;
        while (lOffset < targetEvalAbsolutePos) {
            size_t nextNL = fileText.find(L'\n', lOffset);
            if (nextNL != std::wstring::npos && nextNL < targetEvalAbsolutePos) { evalRealLine++; lOffset = nextNL + 1; }
            else break;
        }

        int counterID = GetParamIndexByTextOrder(fileText, currentActiveFile, evalRealLine, evalIdxInLine);
        if (counterID == -1) return;

        std::string vsLookupKey = currentActiveFile + ":" + std::to_string(counterID);
        int targetId = getID(vsLookupKey);
        if (targetId == -1) return;

        size_t openBracket = fileText.find(L'(', targetEvalAbsolutePos);
        size_t closeBracket = FindCloseBracket(fileText, openBracket);
        if (openBracket != std::wstring::npos && closeBracket != std::wstring::npos) {
            std::wstring innerValueW = fileText.substr(openBracket + 1, closeBracket - openBracket - 1);
            std::string innerValueA = ConvertWStringToUtf8(innerValueW);

            innerValueA.erase(0, innerValueA.find_first_not_of(" \t\r\n"));
            innerValueA.erase(innerValueA.find_last_not_of(" \t\r\n") + 1);

            UpdateParamValue(targetId, innerValueA);
        }
    }


    inline std::wstring DownloadCurrentLineText(IDispatch* pActiveDoc) {
        std::wstring lineText = L"";
        VARIANT vtSelection; VariantInit(&vtSelection);
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, pActiveDoc, L"Selection", 0)) || !vtSelection.pdispVal) return L"";

        VARIANT vtActivePoint; VariantInit(&vtActivePoint);
        if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtActivePoint, vtSelection.pdispVal, L"ActivePoint", 0)) && vtActivePoint.pdispVal) {

            CComVariant pEditStart;
            AutoWrap(DISPATCH_METHOD, &pEditStart, vtActivePoint.pdispVal, L"CreateEditPoint", 0);

            if (pEditStart.vt == VT_DISPATCH && pEditStart.pdispVal) {
                // Двигаем виртуальную точку в начало текущей строки текста
                AutoWrap(DISPATCH_METHOD, NULL, pEditStart.pdispVal, L"StartOfLine", 0);

                CComVariant pEditEnd;
                AutoWrap(DISPATCH_METHOD, &pEditEnd, vtActivePoint.pdispVal, L"CreateEditPoint", 0);
                AutoWrap(DISPATCH_METHOD, NULL, pEditEnd.pdispVal, L"EndOfLine", 0);

                if (pEditEnd.vt == VT_DISPATCH && pEditEnd.pdispVal) {
                    VARIANT vtLineText; VariantInit(&vtLineText);
                    // Выкачиваем из COM-буфера VS СТРОГО одну строку кода вместо всего файла!
                    if (SUCCEEDED(AutoWrap(DISPATCH_METHOD, &vtLineText, pEditStart.pdispVal, L"GetText", 1, pEditEnd))) {
                        if (vtLineText.vt == VT_BSTR && vtLineText.bstrVal) {
                            lineText = vtLineText.bstrVal;
                        }
                        VariantClear(&vtLineText);
                    }
                }
            }
        }
        VariantClear(&vtActivePoint); VariantClear(&vtSelection);
        return lineText;
    }

    // Хранилище для оптимизации
    static DWORD g_lastVsTickTime = 0;
    static std::wstring g_lastLineTextBuffer = L"";
    static long g_lastLine = -1;
    static long g_lastCol = -1;

    inline bool isMouseDragging();

    static inline size_t g_lastTargetEvalPos = 0;
    static inline int    g_cachedCounterID = -1;

    void vsEditor() {
        if (!initVsEditor()) return;

        VARIANT vtActiveDoc; VariantInit(&vtActiveDoc);
        HRESULT hr = pDTE ? AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0) : E_FAIL;

        if (hr == CO_E_OBJNOTCONNECTED || hr == RPC_E_DISCONNECTED || hr == E_ACCESSDENIED) {
            ResetDTEConnection();
            return;
        }
        if (FAILED(hr) || !vtActiveDoc.pdispVal) {
            VariantClear(&vtActiveDoc);
            return;
        }

        IDispatch* pActiveDoc = vtActiveDoc.pdispVal;
        std::string currentActiveFile = GetActiveDocumentPath(pActiveDoc);
        if (currentActiveFile.empty()) {
            VariantClear(&vtActiveDoc);
            return;
        }

        long line = 0, column = 0;
        if (!GetCursorCoordinates(pActiveDoc, line, column)) {
            VariantClear(&vtActiveDoc);
            return;
        }

        std::wstring currentLineText = DownloadCurrentLineText(pActiveDoc);

        if (!LivePT::isMouseDragging()) {
            if (line == g_lastLine && currentLineText == g_lastLineTextBuffer) {
                g_lastCol = column;
                VariantClear(&vtActiveDoc);
                return;
            }

            if (currentLineText == g_lastLineTextBuffer && line != g_lastLine) {
                g_lastLine = line;
                g_lastCol = column;
                VariantClear(&vtActiveDoc);
                return;
            }
        }

        g_lastLineTextBuffer = currentLineText;
        g_lastLine = line;
        g_lastCol = column;

        std::wstring fileText = DownloadDocumentText(pActiveDoc);
        if (fileText.empty()) {
            VariantClear(&vtActiveDoc);
            return;
        }

        size_t evalIdxInLine = 0;
        size_t targetEvalAbsolutePos = 0;

        if (!CheckCursorInsideEval(fileText, line, column, evalIdxInLine, targetEvalAbsolutePos)) {
            VariantClear(&vtActiveDoc);
            return;
        }

        // ЖЕСТКИЙ ФИКС ДЛЯ МНОГОСТРОЧНЫХ АГРЕГАТОВ:
        // Вычисляем, на какой ИМЕННО строке текстового файла Студии физически началось слово "eval"
        long evalRealLine = 1;
        size_t lOffset = 0;
        while (lOffset < targetEvalAbsolutePos) {
            size_t nextNL = fileText.find(L'\n', lOffset);
            if (nextNL != std::wstring::npos && nextNL < targetEvalAbsolutePos) {
                evalRealLine++;
                lOffset = nextNL + 1;
            }
            else break;
        }

        // Принудительно подменяем line физической строкой старта макроса eval!
        // Благодаря этому GetParamIndexByTextOrder() внутри ParseAndStoreParamValue()
        // вычислит идеальный, стабильный counterID, который на 100% совпадет со стартовым ID в игре.
        line = evalRealLine;

        ParseAndStoreParamValue(fileText, currentActiveFile, line, evalIdxInLine, targetEvalAbsolutePos);
        VariantClear(&vtActiveDoc);
    }




    
}