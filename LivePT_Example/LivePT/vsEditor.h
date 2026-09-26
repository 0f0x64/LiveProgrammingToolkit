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

    

    // 3. ПОЛНЫЙ ВЫРЕЗАТЕЛЬ СКОБОК ДЛЯ ПАРСИНГА С КЛАВИАТУРЫ
    // Исправленная оригинальная функция: парсит текст из файла, используя конвертацию в валидный ID
    inline void ParseAndStoreParamValue(const std::wstring& fileText, const std::string& currentActiveFile, int validRuntimeId) {
        if (validRuntimeId == -1 || fileText.empty()) return;

        // Конвертируем валидный Runtime ID из карты в реальный targetId базы рантайма
        std::string vsLookupKey = LivePT::NormalizePath(currentActiveFile.c_str()) + ":" + std::to_string(validRuntimeId);
        int targetId = getID(vsLookupKey);
        if (targetId == -1) return;

        const auto& params = LivePT::getParamDesc();
        const auto& p = params[targetId];

        // НАДЁЖНЫЙ ЛИНЕЙНЫЙ ПЕРЕВОД КООРДИНАТ КОМПИЛЯТОРА В ОФСЕТ ЗА O(N)
        size_t lineStartOffset = 0;
        long currentLineIdx = 1;
        while (currentLineIdx < p.line && lineStartOffset < fileText.length()) {
            size_t nextNL = fileText.find(L'\n', lineStartOffset);
            if (nextNL != std::wstring::npos) {
                lineStartOffset = nextNL + 1;
                currentLineIdx++;
            }
            else {
                break;
            }
        }

        size_t nextNL = fileText.find(L'\n', lineStartOffset);
        if (nextNL == std::wstring::npos) nextNL = fileText.length();
        std::wstring lineText = fileText.substr(lineStartOffset, nextNL - lineStartOffset);

        long currentVisualCol = 1;
        size_t paramCharIdx = lineText.length();
        for (size_t i = 0; i < lineText.length(); ++i) {
            if (currentVisualCol >= p.column) { paramCharIdx = i; break; }
            currentVisualCol += (lineText[i] == L'\t') ? (4 - ((currentVisualCol - 1) % 4)) : 1;
        }
        size_t anchorOffset = lineStartOffset + paramCharIdx;

        if (anchorOffset >= fileText.length()) return;

        // ЖЕЛЕЗОБЕТОННЫЙ РЕТРОПОИСК: Отматываем текст назад до истинного начала "eval"
        size_t evalAbsolutePos = std::wstring::npos;
        size_t rfindPos = fileText.rfind(L"eval", anchorOffset);
        if (rfindPos != std::wstring::npos) {
            bool validLeft = (rfindPos == 0 || (!iswalnum(fileText[rfindPos - 1]) && fileText[rfindPos - 1] != L'_'));
            bool validRight = (rfindPos + 4 >= fileText.length() || (!iswalnum(fileText[rfindPos + 4]) && fileText[rfindPos + 4] != L'_'));
            if (validLeft && validRight) {
                evalAbsolutePos = rfindPos; // Нашли физическое начало многострочного агрегата
            }
        }
        if (evalAbsolutePos == std::wstring::npos) {
            evalAbsolutePos = anchorOffset;
        }

        // Вырезаем скобки строго от истинного найденного начала макроса
        size_t openBracket = fileText.find(L'(', evalAbsolutePos);
        size_t closeBracket = FindCloseBracket(fileText, openBracket);

        if (openBracket != std::wstring::npos && closeBracket != std::wstring::npos && closeBracket > openBracket) {
            std::wstring innerValueW = fileText.substr(openBracket + 1, closeBracket - openBracket - 1);
            std::string innerValueA = ConvertWStringToUtf8(innerValueW);

            // Чистим пробелы и переносы строк внутри агрегата
            innerValueA.erase(0, innerValueA.find_first_not_of(" \t\r\n"));
            innerValueA.erase(innerValueA.find_last_not_of(" \t\r\n") + 1);

            // Отправляем чистый отпарсенный агрегат в движок рантайма
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

    
    // Возвращает актуальный сырой ID макроса, на котором ПРЯМО СЕЙЧАС стоит курсор.
// Считает абсолютно ВСЕ eval (включая комменты), чтобы индексы жестко бились с картой.
// Если курсор стоит вне скобок макроса — возвращает -1.
    struct EvalContext {
        int rawId = -1;
        size_t absolutePos = std::wstring::npos;
    };


    // Буфер для кэширования последнего вычисленного контекста на текущих координатах
    static EvalContext g_cachedEvalCtx;
    static long g_cachedEvalLine = -1;
    static long g_cachedEvalCol = -1;
    static std::wstring g_cachedEvalTextHash = L""; // Защита на случай, если текст строки изменился


    inline EvalContext GetCurrentRawIdUnderCursor(const std::wstring& fileText, long cursorLine, long cursorColumn, const std::wstring& currentLineText) {
        EvalContext ctx;
        if (fileText.empty()) return ctx;

        // 1. ВЫЧИСЛЯЕМ АБСОЛЮТНЫЙ ОФСЕТ КУРСОРA В ФАЙЛЕ (Линейно O(N) до нужной строки)
        size_t lineStartOffset = 0;
        long currentLineIdx = 1;
        while (currentLineIdx < cursorLine && lineStartOffset < fileText.length()) {
            size_t nextNL = fileText.find(L'\n', lineStartOffset);
            if (nextNL != std::wstring::npos) {
                lineStartOffset = nextNL + 1;
                currentLineIdx++;
            }
            else {
                break;
            }
        }

        size_t nextNL = fileText.find(L'\n', lineStartOffset);
        if (nextNL == std::wstring::npos) nextNL = fileText.length();
        std::wstring lineText = fileText.substr(lineStartOffset, nextNL - lineStartOffset);

        // Учитываем табы при расчете физического офсета символа в строке
        long currentVisualCol = 1;
        size_t cursorCharIdx = lineText.length();
        for (size_t i = 0; i < lineText.length(); ++i) {
            if (currentVisualCol >= cursorColumn) { cursorCharIdx = i; break; }
            currentVisualCol += (lineText[i] == L'\t') ? (4 - ((currentVisualCol - 1) % 4)) : 1;
        }
        size_t globalCursorOffset = lineStartOffset + cursorCharIdx;

        // 2. ЧЕСТНЫЙ РЕТРОПОИСК НАЗАД С УЧЕТОМ СКОБОЧНОГО БАЛАНСА
        size_t searchOrigin = globalCursorOffset;
        size_t targetMacroStart = std::wstring::npos;

        while (searchOrigin > 0) {
            size_t rfindPos = fileText.rfind(L"eval", searchOrigin);
            if (rfindPos == std::wstring::npos) break;

            // Проверка границ токена eval
            bool validLeft = (rfindPos == 0 || (!iswalnum(fileText[rfindPos - 1]) && fileText[rfindPos - 1] != L'_'));
            bool validRight = (rfindPos + 4 >= fileText.length() || (!iswalnum(fileText[rfindPos + 4]) && fileText[rfindPos + 4] != L'_'));

            if (validLeft && validRight) {
                size_t openBracket = fileText.find(L'(', rfindPos + 4);
                if (openBracket != std::wstring::npos) {
                    size_t closeBracket = FindCloseBracket(fileText, openBracket);

                    if (closeBracket != std::wstring::npos) {
                        // Сценарий А: Курсор стоит прямо на слове "eval" или на его открывающей скобке
                        if (globalCursorOffset >= rfindPos && globalCursorOffset <= openBracket + 1) {
                            targetMacroStart = rfindPos;
                            break;
                        }

                        // Сценарий Б: Курсор ушел глубоко внутрь многострочного агрегата (на строки .r, .g, .b)
                        if (globalCursorOffset > openBracket + 1 && globalCursorOffset <= closeBracket) {
                            int bracketCount = 0;
                            // Считаем скобки от начала макроса до физической позиции курсора
                            for (size_t k = openBracket; k < globalCursorOffset && k < fileText.length(); ++k) {
                                if (fileText[k] == L'(') bracketCount++;
                                if (fileText[k] == L')') bracketCount--;
                            }

                            // Если баланс >= 1, мы гарантированно внутри незакрытого тела этого агрегата
                            if (bracketCount >= 1) {
                                targetMacroStart = rfindPos;
                                break;
                            }
                        }
                    }
                }
            }
            if (rfindPos == 0) break;
            searchOrigin = rfindPos - 1;
        }

        if (targetMacroStart == std::wstring::npos) {
            return ctx; // Возвращаем дефолтный {-1, npos}
        }

        ctx.absolutePos = targetMacroStart;

        // 3. ТУПОЙ СКВОЗНОЙ ПОДЧЕТ ДО ИСТИННОЙ ТОЧКИ (Идеально бьется с линейным генератором карты)
        int runningRawCounter = 0;
        size_t currentOffset = 0;
        while ((currentOffset = fileText.find(L"eval", currentOffset)) != std::wstring::npos && currentOffset < targetMacroStart) {
            bool validLeft = (currentOffset == 0 || (!iswalnum(fileText[currentOffset - 1]) && fileText[currentOffset - 1] != L'_'));
            bool validRight = (currentOffset + 4 >= fileText.length() || (!iswalnum(fileText[currentOffset + 4]) && fileText[currentOffset + 4] != L'_'));
            if (validLeft && validRight) runningRawCounter++;
            currentOffset += 4;
        }

        ctx.rawId = runningRawCounter;
        return ctx;
    }




    // Хранилище кэша карт: Полный путь к файлу (std::string) -> Вектор перевода (std::vector<int>)
    static std::unordered_map<std::string, std::vector<int>> g_filesMapsCache;

    static std::string g_cachedMapFilePath = "";

    // Индекс вектора — сырой ID в тексте (включая вообще ВСЕ токены "eval"). 
// Значение — валидный Runtime ID из базы компилятора (или -1)
    inline int CountRawEvalsUpToOffset(const std::wstring& fileText, size_t targetOffset) {
        if (targetOffset == std::wstring::npos || targetOffset > fileText.length()) {
            return 0;
        }

        int rawEvalCount = 0;
        size_t currentOffset = 0;

        while ((currentOffset = fileText.find(L"eval", currentOffset)) != std::wstring::npos) {
            // Если дошли или перешагнули физическое начало нашего макроса — стоп
            if (currentOffset >= targetOffset) {
                break;
            }

            bool validLeft = (currentOffset == 0 || (!iswalnum(fileText[currentOffset - 1]) && fileText[currentOffset - 1] != L'_'));
            bool validRight = (currentOffset + 4 >= fileText.length() || (!iswalnum(fileText[currentOffset + 4]) && fileText[currentOffset + 4] != L'_'));

            if (validLeft && validRight) {
                rawEvalCount++;
            }

            currentOffset += 4;
        }

        return rawEvalCount;
    }

    inline void BuildRawToRuntimeMapLinear(const std::string& targetFileName, const std::wstring& fileText) {
        if (fileText.empty()) return;

        std::string normalizedPath = LivePT::NormalizePath(targetFileName.c_str());

        // ПРОВЕРКА КЭША: Если этот файл уже обрабатывался, ничего не пересобираем
        if (g_filesMapsCache.find(normalizedPath) != g_filesMapsCache.end()) {
            return;
        }

        const auto& params = LivePT::getParamDesc();
        std::vector<int> tempMap;
        int currentId = 0;

        // Идем строго по базе компилятора сверху вниз
        while (true) {
            std::string lookupKey = normalizedPath + ":" + std::to_string(currentId);
            int paramIndex = LivePT::getID(lookupKey);

            if (paramIndex == -1) {
                break; // Зарегистрированные компилятором макросы закончились
            }

            const auto& p = params[paramIndex];

            // ПЕРЕВОДИМ КООРДИНАТЫ АНКОРА КОМПИЛЯТОРА В ОФСЕТ (Локальный O(N) проход до строки)
            size_t lineStartOffset = 0;
            long currentLineIdx = 1;
            while (currentLineIdx < p.line && lineStartOffset < fileText.length()) {
                size_t nextNL = fileText.find(L'\n', lineStartOffset);
                if (nextNL != std::wstring::npos) {
                    lineStartOffset = nextNL + 1;
                    currentLineIdx++;
                }
                else {
                    break;
                }
            }

            size_t nextNL = fileText.find(L'\n', lineStartOffset);
            if (nextNL == std::wstring::npos) nextNL = fileText.length();
            std::wstring lineText = fileText.substr(lineStartOffset, nextNL - lineStartOffset);

            long currentVisualCol = 1;
            size_t paramCharIdx = lineText.length();
            for (size_t i = 0; i < lineText.length(); ++i) {
                if (currentVisualCol >= p.column) { paramCharIdx = i; break; }
                currentVisualCol += (lineText[i] == L'\t') ? (4 - ((currentVisualCol - 1) % 4)) : 1;
            }
            size_t anchorOffset = lineStartOffset + paramCharIdx;

            // РЕТРОПОИСК: Находим реальное физическое начало слова "eval" для текущего параметра
            size_t currentMacroStart = std::wstring::npos;
            if (anchorOffset != std::wstring::npos && anchorOffset < fileText.length()) {
                size_t rfindPos = fileText.rfind(L"eval", anchorOffset);
                if (rfindPos != std::wstring::npos) {
                    bool validLeft = (rfindPos == 0 || (!iswalnum(fileText[rfindPos - 1]) && fileText[rfindPos - 1] != L'_'));
                    bool validRight = (rfindPos + 4 >= fileText.length() || (!iswalnum(fileText[rfindPos + 4]) && fileText[rfindPos + 4] != L'_'));
                    if (validLeft && validRight) {
                        currentMacroStart = rfindPos;
                    }
                }
            }
            if (currentMacroStart == std::wstring::npos) {
                currentMacroStart = anchorOffset;
            }

            // ТУПОЙ СКВОЗНОЙ ПОДСЧЕТ: Считаем eval строго до найденной физической точки начала макроса
            // Логика поиска ПОЛНОСТЬЮ ИДЕНТИЧНА рантайм-счетчику
            int runningRawCounter = 0;
            size_t currentOffset = 0;
            while ((currentOffset = fileText.find(L"eval", currentOffset)) != std::wstring::npos && currentOffset < currentMacroStart) {
                bool validLeft = (currentOffset == 0 || (!iswalnum(fileText[currentOffset - 1]) && fileText[currentOffset - 1] != L'_'));
                bool validRight = (currentOffset + 4 >= fileText.length() || (!iswalnum(fileText[currentOffset + 4]) && fileText[currentOffset + 4] != L'_'));
                if (validLeft && validRight) runningRawCounter++;
                currentOffset += 4;
            }

            // Записываем сопоставление в карту
            int rawCounterId = runningRawCounter;
            if (rawCounterId >= 0) {
                if (rawCounterId >= static_cast<int>(tempMap.size())) {
                    tempMap.resize(rawCounterId + 1, -1);
                }
                tempMap[rawCounterId] = currentId; // map[сырой id] = валидный id компилятора
            }

            currentId++;
        }

        if (!tempMap.empty()) {
            g_filesMapsCache[normalizedPath] = std::move(tempMap);
        }
    }


    inline void vsEditor() {
        if (!initVsEditor()) return;

        VARIANT vtActiveDoc; VariantInit(&vtActiveDoc);
        HRESULT hr = pDTE ? AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0) : E_FAIL;

        if (hr == CO_E_OBJNOTCONNECTED || hr == RPC_E_DISCONNECTED || hr == E_ACCESSDENIED) {
            ResetDTEConnection();
            return;
        }
        if (FAILED(hr) || !vtActiveDoc.pdispVal) { VariantClear(&vtActiveDoc); return; }

        IDispatch* pActiveDoc = vtActiveDoc.pdispVal;
        std::string currentActiveFile = GetActiveDocumentPath(pActiveDoc);
        if (currentActiveFile.empty()) { VariantClear(&vtActiveDoc); return; }

        long line = 0, column = 0;
        if (!GetCursorCoordinates(pActiveDoc, line, column)) { VariantClear(&vtActiveDoc); return; }

        std::wstring currentLineText = DownloadCurrentLineText(pActiveDoc);

        // Оптимизация холостых тиков таймера, если мышь не зажата и курсор стоит на месте
        if (!LivePT::isMouseDragging()) {
            if (line == g_lastLine && currentLineText == g_lastLineTextBuffer) {
                VariantClear(&vtActiveDoc);
                return;
            }
        }
        bool lg = (line != g_lastLine || g_lastCol != column);

        g_lastLineTextBuffer = currentLineText;
        g_lastLine = line;
        g_lastCol = column;

        std::wstring fileText = DownloadDocumentText(pActiveDoc);
        if (fileText.empty()) { VariantClear(&vtActiveDoc); return; }

        // Шаг 1: Запуск линейного сборщика карты (выполняется строго 1 раз при открытии файла)
        BuildRawToRuntimeMapLinear(currentActiveFile, fileText);

        // Шаг 2: Получаем рантайм-контекст через наш точный двусторонний счетчик
        EvalContext evalCtx = GetCurrentRawIdUnderCursor(fileText, line, column, currentLineText);

        int finalValidRuntimeId = -1;

        if (evalCtx.rawId != -1 && evalCtx.absolutePos != std::wstring::npos) {
            std::string normalizedPath = LivePT::NormalizePath(currentActiveFile.c_str());

            // Шаг 3: Перегоняем сырой текстовый ID под курсором в валидный ID компилятора
            auto it = g_filesMapsCache.find(normalizedPath);
            if (it != g_filesMapsCache.end()) {
                const auto& currentFileMap = it->second;

                if (evalCtx.rawId >= 0 && evalCtx.rawId < static_cast<int>(currentFileMap.size())) {
                    finalValidRuntimeId = currentFileMap[evalCtx.rawId];
                }
            }
        }

        // Шаг 4: Если ID валиден, выводим лог и отправляем данные в оригинальный парсер скобок
        if (finalValidRuntimeId != -1) {

            // Передаем управление в парсер скобок
            ParseAndStoreParamValue(fileText, currentActiveFile, finalValidRuntimeId);
        }

        VariantClear(&vtActiveDoc);
    }


    
}