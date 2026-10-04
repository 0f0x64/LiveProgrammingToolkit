namespace LivePT {

    int GetMouseDragTargetParamId();

    static IDispatch* pDTE = nullptr;

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

        int safetyCounter = 32;

        while (searchPid != 0 && --safetyCounter > 0) {
            PROCESSENTRY32W pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32W);

            DWORD parentPid = 0;
            std::wstring procName = L"";

            if (Process32FirstW(hSnapshot, &pe32)) {
                do {
                    if (pe32.th32ProcessID == searchPid) {
                        parentPid = pe32.th32ParentProcessID;
                        procName = ToLower(pe32.szExeFile);
                        break;
                    }
                } while (Process32NextW(hSnapshot, &pe32));
            }

            if (procName.find(L"devenv") != std::wstring::npos) {
                studioPid = searchPid;
                break;
            }

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

    void StartUndoTransaction(const std::wstring& name) {

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


    inline long GetVSTabSize() {
        static long cachedTabSize = 4;
        static bool isLoaded = false;

        if (isLoaded || !pDTE) {
            return cachedTabSize;
        }

        CComVariant vtProperties;

        HRESULT hr = AutoWrap(DISPATCH_PROPERTYGET, &vtProperties, pDTE, L"Properties", 2,
            CComVariant(L"TextEditor"), CComVariant(L"C/C++"));

        if (SUCCEEDED(hr) && vtProperties.pdispVal) {
            CComVariant vtTabSizeItem;

            hr = AutoWrap(DISPATCH_METHOD, &vtTabSizeItem, vtProperties.pdispVal, L"Item", 1, CComVariant(L"TabSize"));

            if (SUCCEEDED(hr) && vtTabSizeItem.pdispVal) {
                CComVariant vtValue;
                hr = AutoWrap(DISPATCH_PROPERTYGET, &vtValue, vtTabSizeItem.pdispVal, L"Value", 0);

                if (SUCCEEDED(hr)) {

                    if (vtValue.vt == VT_I4 || vtValue.vt == VT_INT) {
                        cachedTabSize = vtValue.lVal;
                        isLoaded = true;
                    }
                    else if (vtValue.vt == VT_I2) {
                        cachedTabSize = vtValue.iVal;
                        isLoaded = true;
                    }
                }
            }
        }

        return cachedTabSize;

    }

    inline size_t FindCloseBracket(const std::wstring& text, size_t openBracketPos) {

        if (openBracketPos == std::wstring::npos) return std::wstring::npos;

        int bracketCount = 1;

        for (size_t k = openBracketPos + 1; k < text.length(); ++k) {
            if (text[k] == L'(') bracketCount++;
            if (text[k] == L')') bracketCount--;
            if (bracketCount == 0) return k;
        }

        return std::wstring::npos;
    }

    inline std::string GetActiveDocumentPath(IDispatch* pActiveDoc) {

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

    inline bool GetCursorCoordinates(IDispatch* pActiveDoc, long& outLine, long& outColumn) {

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

    inline std::wstring DownloadDocumentText(IDispatch* pActiveDoc) {

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

    inline long GetVisualColumn(const std::wstring& lineText, size_t charIdx) {
        long tabSize = GetVSTabSize();
        size_t visualCol = 0;

        for (size_t i = 0; i < charIdx && i < lineText.length(); ++i) {
            if (lineText[i] == L'\t') {
                visualCol += tabSize - (visualCol % tabSize);
            }
            else {
                visualCol++;
            }
        }
        return static_cast<long>(visualCol) + 1;
    }

    inline void ParseAndStoreParamValueDirect(const std::wstring& fileText, int targetId) {
        if (fileText.empty() || targetId < 0 || targetId >= static_cast<int>(paramDesc.size())) {
            //Log("[LivePT Debug] ParseDirect: Early abort due to empty text or bad targetId: " + std::to_string(targetId));
            return;
        }

        auto& p = paramDesc[targetId];
        std::wstring macroName = GetConfiguredMacroName();
        long tabSize = GetVSTabSize();

        // 1. Находим физическое смещение начала строки p.line
        size_t lineStartOffset = 0;
        long currentLineIdx = 1;
        while (currentLineIdx < p.line && lineStartOffset < fileText.length()) {
            size_t nextNL = fileText.find(L'\n', lineStartOffset);
            if (nextNL != std::wstring::npos) {
                lineStartOffset = nextNL + 1;
                currentLineIdx++;
            }
            else break;
        }

        // 2. Выделяем текст этой строки
        size_t nextNL = fileText.find(L'\n', lineStartOffset);
        if (nextNL == std::wstring::npos) nextNL = fileText.length();
        std::wstring lineText = fileText.substr(lineStartOffset, nextNL - lineStartOffset);

        // 3. Вычисляем физический символ по p.column
        long currentVisualCol = 1;
        size_t paramCharIdx = lineText.length();
        for (size_t i = 0; i < lineText.length(); ++i) {
            if (currentVisualCol >= p.column) { paramCharIdx = i; break; }
            currentVisualCol += (lineText[i] == L'\t') ? (tabSize - ((currentVisualCol - 1) % tabSize)) : 1;
        }
        size_t anchorOffset = lineStartOffset + paramCharIdx;

        if (anchorOffset >= fileText.length()) {
            //Log("[LivePT Debug] ParseDirect: Anchor offset out of file bounds.");
            return;
        }

        // 4. Вырезаем аргументы внутри круглых скобок eval(...)
        size_t openBracket = fileText.find(L'(', anchorOffset);
        size_t closeBracket = FindCloseBracket(fileText, openBracket);

        if (openBracket != std::wstring::npos && closeBracket != std::wstring::npos && closeBracket > openBracket) {
            std::wstring innerValueW = fileText.substr(openBracket + 1, closeBracket - openBracket - 1);
            std::string innerValueA = ConvertWStringToUtf8(innerValueW);

            innerValueA.erase(0, innerValueA.find_first_not_of(" \t\r\n"));
            innerValueA.erase(innerValueA.find_last_not_of(" \t\r\n") + 1);

            // КРИТИЧЕСКИЙ ЛОГ ВЫРЕЗАНИЯ СТРОКИ
            /*Log("[LivePT Debug CUT] TargetParamId: " + std::to_string(targetId) +
                " | Macro base coords: " + std::to_string(p.line) + ":" + std::to_string(p.column) +
                " | Cut text to lambda: '" + innerValueA + "'");
                */
            // Обновляем рантайм-переменную в памяти движка игры
            UpdateParamValue(targetId, innerValueA);
        }
        else {
            //Log("[LivePT Debug] ParseDirect: Failed to resolve parenthesis geometry around anchor.");
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

                AutoWrap(DISPATCH_METHOD, NULL, pEditStart.pdispVal, L"StartOfLine", 0);

                CComVariant pEditEnd;
                AutoWrap(DISPATCH_METHOD, &pEditEnd, vtActivePoint.pdispVal, L"CreateEditPoint", 0);
                AutoWrap(DISPATCH_METHOD, NULL, pEditEnd.pdispVal, L"EndOfLine", 0);

                if (pEditEnd.vt == VT_DISPATCH && pEditEnd.pdispVal) {
                    VARIANT vtLineText; VariantInit(&vtLineText);

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

//    static std::wstring g_lastLineTextBuffer = L"";
    static std::wstring g_currentFileFullText = L"";
//    static long g_lastLine = -1;
//    static long g_lastCol = -1;

    inline bool isMouseDragging();

    struct EvalContext {
        int rawId = -1;
        size_t absolutePos = std::wstring::npos;
    };

    static EvalContext g_cachedEvalCtx;
    static long g_cachedEvalLine = -1;
    static long g_cachedEvalCol = -1;
    static std::wstring g_cachedEvalTextHash = L"";

    std::string g_currentActiveFile = "";
    //static long          g_lastLineCount = -1;
    //static size_t        g_lastLineLength = 0;

    static std::string   g_lastActiveFile = "";
    static long          g_lastLineCount = -1;
    static size_t        g_lastLineLength = 0;
    static long          g_lastLine = -1;
    static long          g_lastCol = -1;
    static std::wstring  g_lastLineTextBuffer = L"";

    inline int FindParamIdByStrictGeometry(const std::string& normalizedFile, long cursorLine, long cursorColumn) {
        auto& params = LivePT::getParamDesc();
        int targetParamId = -1;

        // 1. Линейно находим последний макрос, который начался ДО текущего положения курсора Visual Studio
        for (int localId = 0; ; ++localId) {
            std::string vsLookupKey = normalizedFile + ":" + std::to_string(localId);
            int paramId = LivePT::getID(vsLookupKey);

            if (paramId == -1) break; // Макросы в текущем файле закончились — выходим
            if (paramId < 0 || paramId >= static_cast<int>(params.size())) continue;

            const auto& p = params[paramId];

            // Проверяем, находится ли макрос ДО начала или на позиции курсора
            if (p.line < cursorLine || (p.line == cursorLine && p.column <= cursorColumn)) {
                targetParamId = paramId;
            }
            else {
                // Как только наткнулись на макрос, который стоит строго НИЖЕ или ПРАВЕЕ курсора — прерываем цикл.
                break;
            }
        }

        // 2. КРИТИЧЕСКАЯ ГЕОМЕТРИЧЕСКАЯ ДИАГНОСТИКА ГРАНИЦ МАКРОСА
        if (targetParamId != -1 && !g_currentFileFullText.empty()) {
            const auto& p = params[targetParamId];

            // Вычисляем абсолютное смещение начала строки макроса p.line
            size_t macroLineOffset = 0;
            long currentLineIdx = 1;
            while (currentLineIdx < p.line && macroLineOffset < g_currentFileFullText.length()) {
                size_t nextNL = g_currentFileFullText.find(L'\n', macroLineOffset);
                if (nextNL != std::wstring::npos) {
                    macroLineOffset = nextNL + 1;
                    currentLineIdx++;
                }
                else break;
            }

            // Абсолютное смещение самого слова eval в файле
            size_t macroAbsoluteOffset = macroLineOffset + (p.column > 0 ? p.column - 1 : 0);

            // Вычисляем абсолютное смещение текущей строки курсора cursorLine
            size_t cursorLineOffset = 0;
            currentLineIdx = 1;
            while (currentLineIdx < cursorLine && cursorLineOffset < g_currentFileFullText.length()) {
                size_t nextNL = g_currentFileFullText.find(L'\n', cursorLineOffset);
                if (nextNL != std::wstring::npos) {
                    cursorLineOffset = nextNL + 1;
                    currentLineIdx++;
                }
                else break;
            }

            // Абсолютное смещение текущего курсора в файле
            size_t cursorAbsoluteOffset = cursorLineOffset + (cursorColumn > 0 ? cursorColumn - 1 : 0);

            // Ищем открывающую круглую скобку макроса eval( строго после его начала
            size_t openBracketPos = g_currentFileFullText.find(L'(', macroAbsoluteOffset);

            if (openBracketPos != std::wstring::npos && openBracketPos < g_currentFileFullText.length()) {
                size_t closeBracketPos = std::wstring::npos;
                int bracketCount = 1;

                // Сканируем файл вперед для подсчета баланса скобок
                for (size_t k = openBracketPos + 1; k < g_currentFileFullText.length(); ++k) {
                    wchar_t ch = g_currentFileFullText[k];

                    if (ch == L'(') {
                        bracketCount++;
                    }
                    else if (ch == L')') {
                        bracketCount--;
                        if (bracketCount == 0) {
                            // Нашли закрытие макроса! Проверяем синтаксический паттерн конца выражения
                            if (k > 0) {
                                wchar_t prevCh = g_currentFileFullText[k - 1];
                                // Два оговоренных варианта закрытия многострочных/сложных агрегатов: )) или })
                                if (prevCh == L')' || prevCh == L'}') {
                                    closeBracketPos = k;
                                    break;
                                }
                            }
                            // Фолбэк на стандартное одиночное закрытие, если вложенностей не было
                            closeBracketPos = k;
                            break;
                        }
                    }
                }

                // Проверка локации курсора: если закрывающая скобка успешно определена,
                // и курсор физически улетел дальше нее, значит мы СНАРУЖИ макроса.
                if (closeBracketPos != std::wstring::npos) {
                    if (cursorAbsoluteOffset > closeBracketPos) {
                        return -1; // Курсор вне макроса — блокируем драг
                    }
                }
            }

            // Старая добрая защита от жесткого закрытия строки точкой с запятой
            if (macroAbsoluteOffset < cursorAbsoluteOffset && cursorAbsoluteOffset <= g_currentFileFullText.length()) {
                size_t searchLength = cursorAbsoluteOffset - macroAbsoluteOffset;
                std::wstring textBetween = g_currentFileFullText.substr(macroAbsoluteOffset, searchLength);

                if (textBetween.find(L';') != std::wstring::npos) {
                    return -1;
                }
            }
        }

        return targetParamId;
    }




    inline void ShiftDatabaseCoordinates(const std::string& targetFile, long targetLine, long visualStartCol, int lineDelta, int columnDelta);

    // === ЕДИНЫЙ ПОЛНОСТЬЮ ОЧИЩЕННЫЙ ДИСПЕТЧЕР LivePT ===
        // === ЕДИНЫЙ ПОЛНОСТЬЮ ОЧИЩЕННЫЙ ДИСПЕТЧЕР СИНХРОНИЗАЦИИ LivePT ===
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
        g_currentActiveFile = currentActiveFile;
        if (currentActiveFile.empty()) { VariantClear(&vtActiveDoc); return; }

        long line = 0, column = 0;
        if (!GetCursorCoordinates(pActiveDoc, line, column)) { VariantClear(&vtActiveDoc); return; }

        std::wstring currentLineText = DownloadCurrentLineText(pActiveDoc);
        std::wstring fileText = DownloadDocumentText(pActiveDoc);
        if (fileText.empty()) { VariantClear(&vtActiveDoc); return; }

        g_currentFileFullText = fileText;

        long currentLineCount = static_cast<long>(std::count(fileText.begin(), fileText.end(), L'\n')) + 1;
        size_t currentLineLength = currentLineText.length();

        std::string normalizedPath = LivePT::NormalizePath(currentActiveFile.c_str());

        if (currentActiveFile != g_lastActiveFile) {
            g_lastActiveFile = currentActiveFile;
            g_lastLine = line;
            g_lastCol = column;
            g_lastLineCount = currentLineCount;
            g_lastLineLength = currentLineLength;
            g_lastLineTextBuffer = currentLineText;

            VariantClear(&vtActiveDoc);
            return;
        }

        bool wasChanged = (currentLineText != g_lastLineTextBuffer || currentLineCount != g_lastLineCount);

        if (wasChanged) {
            int lineDelta = static_cast<int>(currentLineCount - g_lastLineCount);
            int columnDelta = static_cast<int>(currentLineLength - g_lastLineLength);
            long visualStartCol = (column < g_lastCol) ? column : g_lastCol;

            LivePT::ShiftDatabaseCoordinates(currentActiveFile, g_lastLine, visualStartCol, lineDelta, columnDelta);
        }

        g_lastLineTextBuffer = currentLineText;
        g_lastLine = line;
        g_lastCol = column;
        g_lastLineCount = currentLineCount;
        g_lastLineLength = currentLineLength;

        // Поиск активного макроса по строгой геометрии
        int finalParamDescId = FindParamIdByStrictGeometry(normalizedPath, line, column);

        if (finalParamDescId != -1) {
            ParseAndStoreParamValueDirect(fileText, finalParamDescId);
        }

        VariantClear(&vtActiveDoc);
    }


  
    inline void OpenFileAndMoveCursorToLocation(const std::source_location& location) {
        if (!LivePT::initVsEditor() || !LivePT::pDTE) {
            return;
        }

        std::string rawPath = location.file_name();
        std::wstring wRawPath(rawPath.begin(), rawPath.end());
        long targetLine = static_cast<long>(location.line());

        wchar_t absoluteBuffer[MAX_PATH] = { 0 };
        GetFullPathNameW(wRawPath.c_str(), MAX_PATH, absoluteBuffer, nullptr);
        std::wstring wTargetDocPath = absoluteBuffer;

        HWND hVSMainWnd = NULL;
        CComVariant vtMainWindow;
        if (SUCCEEDED(LivePT::AutoWrap(DISPATCH_PROPERTYGET, &vtMainWindow, LivePT::pDTE, L"MainWindow", 0)) && vtMainWindow.pdispVal) {
            CComVariant vtHWnd;
            if (SUCCEEDED(LivePT::AutoWrap(DISPATCH_PROPERTYGET, &vtHWnd, vtMainWindow.pdispVal, L"HWnd", 0))) {
                hVSMainWnd = (HWND)(ULONG_PTR)vtHWnd.lVal;
            }
        }

        if (hVSMainWnd && ::IsWindow(hVSMainWnd)) {
            ::ReleaseCapture();

            DWORD gameThreadId = ::GetCurrentThreadId();
            DWORD vsThreadId = ::GetWindowThreadProcessId(hVSMainWnd, NULL);

            if (gameThreadId != vsThreadId) {
                ::AttachThreadInput(gameThreadId, vsThreadId, TRUE);
            }

            ::ShowWindow(hVSMainWnd, SW_RESTORE);
            ::SetForegroundWindow(hVSMainWnd);
            ::SetFocus(hVSMainWnd);

            if (gameThreadId != vsThreadId) {
                ::AttachThreadInput(gameThreadId, vsThreadId, FALSE);
            }

            for (int k = 0; k < 5; ++k) {
                MSG msg;
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                ::Sleep(10);
            }
        }

        CComVariant vtItemOperations;
        HRESULT hr = LivePT::AutoWrap(DISPATCH_PROPERTYGET, &vtItemOperations, LivePT::pDTE, L"ItemOperations", 0);
        if (FAILED(hr) || !vtItemOperations.pdispVal) return;

        CComBSTR bstrPath(wTargetDocPath.c_str());
        CComBSTR bstrKind(L"{00000000-0000-0000-0000-000000000000}"); // vsViewKindPrimary
        CComVariant vtTargetWindow;

        // ИСПРАВЛЕН ПОРЯДОК: Сначала передаем FileName (bstrPath), затем ViewKind (bstrKind)
        // Внутри вашей AutoWrap они перевернутся в (bstrKind, bstrPath), как и требует COM Invoke!
        hr = LivePT::AutoWrap(DISPATCH_METHOD, &vtTargetWindow, vtItemOperations.pdispVal, L"OpenFile", 2,
            CComVariant(bstrPath),
            CComVariant(bstrKind));

        if (FAILED(hr) || !vtTargetWindow.pdispVal) {
            //LivePT::Log("OpenFileAndMoveCursorToLocation: ItemOperations->OpenFile failed due to bad late binding order.");
            return;
        }

        LivePT::AutoWrap(DISPATCH_METHOD, NULL, vtTargetWindow.pdispVal, L"Activate", 0);

        CComVariant vtSelection;
        hr = LivePT::AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, vtTargetWindow.pdispVal, L"Selection", 0);

        if (SUCCEEDED(hr) && vtSelection.vt == VT_DISPATCH && vtSelection.pdispVal) {
            // Передаем аргументы строго слева направо: Line (targetLine), Offset (1L), Extend (0L)
            LivePT::AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"MoveToLineAndOffset", 3,
                CComVariant(targetLine),
                CComVariant(1L),
                CComVariant(0L));
        }
    }

    inline bool IsCursorInsideFunctionCall(const std::source_location& location) {
        // 1. Быстрая нормализация пути текущего проверяемого вызова
        std::string activeFile = LivePT::NormalizePath(location.file_name());

        // Глобальное состояние "активного окна с курсором", вычисленное ОДИН РАЗ
        static std::string cachedActiveFile = "";
        static long cachedVSLine = -1;

        // Границы функции Draw, внутри которой СЕЙЧАС физически стоит курсор в VS
        static long validStartLine = -1;
        static long validEndLine = -1;

        // 2. РАННИЙ ВОЗВРАТ ДЛЯ ВСЕХ 100500 ОБЪЕКТОВ С ЛЮБЫМИ УСЛОВИЯМИ
        if (cachedActiveFile == g_currentActiveFile && cachedVSLine == g_lastLine) {
            // Объект подсвечивается ТОЛЬКО если его строка совпадает с началом вызова под курсором
            return (validStartLine != -1 && static_cast<long>(location.line()) == validStartLine);
        }

        // --- СЮДА МЫ ЗАХОДИМ СТРОГО 1 РАЗ НА ВЕСЬ КАДР, КОГДА КУРСОР РЕАЛЬНО СДВИНУЛСЯ ---
        cachedActiveFile = g_currentActiveFile;
        cachedVSLine = g_lastLine;
        validStartLine = -1;
        validEndLine = -1;

        // Если курсор сейчас в другом файле или текст пуст — выходим сразу
        if (activeFile != g_currentActiveFile || g_currentFileFullText.empty()) {
            return false;
        }

        //Log("cache");

        // 3. НАХОДИМ СМЕЩЕНИЕ ТЕКУЩЕЙ СТРОКИ КУРСОРA
        size_t cursorLineOffset = 0;
        long currentLineIdx = 1;
        while (currentLineIdx < g_lastLine && cursorLineOffset < g_currentFileFullText.length()) {
            size_t nextNL = g_currentFileFullText.find(L'\n', cursorLineOffset);
            if (nextNL != std::wstring::npos) {
                cursorLineOffset = nextNL + 1;
                currentLineIdx++;
            }
            else break;
        }

        // Берем конец строки курсора для корректного старта поиска rfind
        size_t nextLineNL = g_currentFileFullText.find(L'\n', cursorLineOffset);
        size_t scanOffset = (nextLineNL != std::wstring::npos) ? nextLineNL : g_currentFileFullText.length();

        // 4. ИЩЕМ НАЧАЛО ВЫЗОВА DRAW НАЗАД ОТ КУРСОРA
        // Ищем ключевое слово "Draw", чтобы не спотыкаться об внутренние скобки eval(
        size_t drawOffset = g_currentFileFullText.rfind(L"Draw", scanOffset);
        if (drawOffset != std::wstring::npos) {

            // Вычисляем номер строки начала вызова (foundStartLine)
            long foundStartLine = 1;
            size_t lineCheckOffset = 0;
            while (lineCheckOffset < drawOffset && lineCheckOffset < g_currentFileFullText.length()) {
                size_t nextNL = g_currentFileFullText.find(L'\n', lineCheckOffset);
                if (nextNL != std::wstring::npos && nextNL < drawOffset) {
                    foundStartLine++;
                    lineCheckOffset = nextNL + 1;
                }
                else break;
            }

            // Проверяем, что найденный Draw действительно находится выше или на строке курсора
            if (foundStartLine <= g_lastLine) {

                // 5. ИЩЕМ ЗАКРЫВАЮЩУЮ ТОЧКУ С ЗАПЯТОЙ ВПЕРЕД ПОСЛЕ НАЧАЛА DRAW!
                size_t callEndOffset = g_currentFileFullText.find(L';', drawOffset);
                if (callEndOffset != std::wstring::npos) {

                    // Вычисляем номер строки конца вызова (foundEndLine), перебирая \n от начала Draw до ;
                    long foundEndLine = foundStartLine;
                    size_t lineEndCheckOffset = drawOffset;
                    while (lineEndCheckOffset < callEndOffset && lineEndCheckOffset < g_currentFileFullText.length()) {
                        size_t nextNL = g_currentFileFullText.find(L'\n', lineEndCheckOffset);
                        if (nextNL != std::wstring::npos && nextNL < callEndOffset) {
                            foundEndLine++;
                            lineEndCheckOffset = nextNL + 1;
                        }
                        else break;
                    }

                    // 6. ПРОВЕРЯЕМ ПОПАДАНИЕ КУРСОРA В ДИАПАЗОН СТРОК
                    if (g_lastLine >= foundStartLine && g_lastLine <= foundEndLine) {
                        validStartLine = foundStartLine;
                        validEndLine = foundEndLine;
                    }
                }
            }
        }

        // Результат для самого первого зашедшего вызова в текущем кадре изменения
        return (validStartLine != -1 && static_cast<long>(location.line()) == validStartLine);
    }





}
