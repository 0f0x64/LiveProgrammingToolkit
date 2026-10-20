namespace LivePT {

    int GetMouseDragTargetParamId();

    static IDispatch* pDTE = nullptr;

    inline std::wstring DownloadCurrentLineText(IDispatch* pActiveDoc);
    inline size_t FindCloseBracket(const std::wstring& text, size_t openBracketPos);

    inline std::string ConvertWStringToUtf8(const std::wstring& wstr) {

        if (wstr.empty()) return "";

        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (size_needed <= 0) return "";

        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, str.data(), size_needed, NULL, NULL);
        str.resize(size_needed - 1);

        return str;
    }

    inline HRESULT AutoWrap(int autoType, VARIANT* pvResult, IDispatch* pDisp, LPCOLESTR ptName, int cArgs...) {

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

    inline std::wstring ToLower(std::wstring str) {

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

    inline IDispatch* GetDTEByPid(DWORD targetPid) {

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

    inline bool initVsEditor()
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

    inline void ResetDTEConnection() {
        if (pDTE) {
            pDTE->Release();
            pDTE = nullptr;
            Log("[EnvDTE] Connection lost. pDTE reset.");
        }
    }

    inline void StartUndoTransaction(const std::wstring& name) {

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

    inline void EndUndoTransaction() {

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

    // Единый метод безопасного поиска закрывающей скобки макроса.
// Честно считает баланс круглых (), фигурных {} и угловых <> скобок, 
// чтобы корректно определять границы многострочных и сложных параметров.
   // Обновленная безопасная версия в vsEditor.h
    inline size_t FindMacroCloseBracket(const std::wstring& text, size_t openBracketPos) {
        if (openBracketPos == std::wstring::npos || openBracketPos >= text.length()) {
            return std::wstring::npos;
        }

        int roundCount = 1;
        int curlyCount = 0;

        // Состояния парсера для корректного пропуска нерелевантного кода
        bool inSingleLineComment = false;
        bool inMultiLineComment = false;
        bool inStringLiteral = false;
        bool inCharLiteral = false;

        // Ограничиваем поиск разумным пределом, чтобы гарантированно не повесить UI-поток
        size_t maxScanLength = (std::min)(text.length(), openBracketPos + 5000);

        for (size_t k = openBracketPos + 1; k < maxScanLength; ++k) {
            wchar_t ch = text[k];
            wchar_t nextCh = (k + 1 < maxScanLength) ? text[k + 1] : L'\0';
            wchar_t prevCh = (k > 0) ? text[k - 1] : L'\0';

            // 1. ОБРАБОТКА ОДНОСТРОЧНЫХ КОММЕНТАРИЕВ (//)
            if (inSingleLineComment) {
                if (ch == L'\n') inSingleLineComment = false;
                continue;
            }

            // 2. ОБРАБОТКА МНОГОСТРОЧНЫХ КОММЕНТАРИЕВ (/* */)
            if (inMultiLineComment) {
                if (ch == L'*' && nextCh == L'/') {
                    inMultiLineComment = false;
                    k++; // Пропускаем слэш
                }
                continue;
            }

            // 3. ОБРАБОТКА СТРОКОВЫХ ЛИТЕРАЛОВ ("текст")
            if (inStringLiteral) {
                // Проверяем на экранирование кавычки вида \"
                if (ch == L'"' && prevCh != L'\\') inStringLiteral = false;
                continue;
            }

            // 4. ОБРАБОТКА СИМВОЛЬНЫХ ЛИТЕРАЛОВ ('c')
            if (inCharLiteral) {
                if (ch == L'\'' && prevCh != L'\\') inCharLiteral = false;
                continue;
            }

            // --- ТОЧКА СХОЖДЕНИЯ АВТОМАТА: Сюда заходим, только если мы в "чистом" коде ---

            // Проверяем старт комментариев или строк
            if (ch == L'/' && nextCh == L'/') {
                inSingleLineComment = true;
                k++; continue;
            }
            if (ch == L'/' && nextCh == L'*') {
                inMultiLineComment = true;
                k++; continue;
            }
            if (ch == L'"') {
                inStringLiteral = true;
                continue;
            }
            if (ch == L'\'') {
                inCharLiteral = true;
                continue;
            }

            // ЧЕСТНЫЙ ПОДСЧЕТ БАЛАНСА СКОБОК
            if (ch == L'(') {
                roundCount++;
            }
            else if (ch == L')') {
                roundCount--;
                if (curlyCount < 0) curlyCount = 0;

                // Успешный выход: круглые скобки закрылись на верхнем уровне
                if (roundCount == 0 && curlyCount == 0) {
                    return k;
                }
            }
            else if (ch == L'{') {
                curlyCount++;
            }
            else if (ch == L'}') {
                curlyCount--;
            }
        }

        // Если баланс нарушен или текст оборвался — аварийно возвращаем npos
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
            if (lineText[i] == L'\r') {
                continue; // Игнорируем возврат каретки, VS не считает его за колонку
            }
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
            return;
        }

        auto& p = paramDesc[targetId];
        long tabSize = GetVSTabSize();

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

        size_t nextNL = fileText.find(L'\n', lineStartOffset);
        if (nextNL == std::wstring::npos) nextNL = fileText.length();
        std::wstring lineText = fileText.substr(lineStartOffset, nextNL - lineStartOffset);

        long currentVisualCol = 1;
        size_t paramCharIdx = lineText.length();
        for (size_t i = 0; i < lineText.length(); ++i) {
            if (currentVisualCol >= p.column) { paramCharIdx = i; break; }
            if (lineText[i] == L'\r') continue;
            currentVisualCol += (lineText[i] == L'\t') ? (tabSize - ((currentVisualCol - 1) % tabSize)) : 1;
        }
        size_t anchorOffset = lineStartOffset + paramCharIdx;

        if (anchorOffset >= fileText.length()) return;

        size_t openBracket = fileText.find(L'(', anchorOffset);

        if (openBracket != std::wstring::npos && openBracket < fileText.length()) {
            size_t closeBracketPos = FindMacroCloseBracket(fileText, openBracket);

            if (closeBracketPos != std::wstring::npos && closeBracketPos > openBracket) {
                std::wstring innerValueW = fileText.substr(openBracket + 1, closeBracketPos - openBracket - 1);
                innerValueW.erase(std::remove(innerValueW.begin(), innerValueW.end(), L'\r'), innerValueW.end());

                std::string innerValueA = ConvertWStringToUtf8(innerValueW);
                innerValueA.erase(0, innerValueA.find_first_not_of(" \t\n"));
                innerValueA.erase(innerValueA.find_last_not_of(" \t\n") + 1);

                // --- ЛОГИРОВАНИЕ СТРОКИ ИЗ VS ---
                Log("[LivePT VS Update] Param ID: " + std::to_string(targetId) + " | Macro: " + p.macroName);
                Log("[LivePT VS Update] Extracted Raw Text String: '" + innerValueA + "'");

                UpdateParamValue(targetId, innerValueA);
            }
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

    static std::string g_currentActiveFile = "";
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

        for (int localId = 0; ; ++localId) {
            std::string vsLookupKey = normalizedFile + ":" + std::to_string(localId);
            int paramId = LivePT::getID(vsLookupKey);

            if (paramId == -1) break;
            if (paramId < 0 || paramId >= static_cast<int>(params.size())) continue;

            const auto& p = params[paramId];
            if (p.line <= cursorLine) {
                targetParamId = paramId;
            }
            else {
                break;
            }
        }

        if (targetParamId == -1 || g_currentFileFullText.empty()) {
            return -1;
        }

        const auto& p = params[targetParamId];
        long tabSize = GetVSTabSize();

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

        long currentVisualCol = 1;
        size_t cursorCharIdx = 0;
        size_t nextNL = g_currentFileFullText.find(L'\n', cursorLineOffset);
        if (nextNL == std::wstring::npos) nextNL = g_currentFileFullText.length();
        std::wstring cursorLineText = g_currentFileFullText.substr(cursorLineOffset, nextNL - cursorLineOffset);

        for (size_t i = 0; i < cursorLineText.length(); ++i) {
            if (currentVisualCol >= cursorColumn) {
                cursorCharIdx = i;
                break;
            }
            if (cursorLineText[i] == L'\r') continue;
            currentVisualCol += (cursorLineText[i] == L'\t') ? (tabSize - ((currentVisualCol - 1) % tabSize)) : 1;
        }
        if (currentVisualCol < cursorColumn) cursorCharIdx = cursorLineText.length();

        size_t cursorAbsoluteOffset = cursorLineOffset + cursorCharIdx;

        // [ИСПРАВЛЕНО]: Ищем открывающую скобку '(' конкретного макроса/функции
        // Имя берем прямо из структуры p.macroName
        std::wstring wMacroName(p.macroName.begin(), p.macroName.end());

        // Поиск символа '(' начинается сразу за концом имени токена (p.column + длина имени)
        size_t openBracketPos = g_currentFileFullText.find(L'(', macroLineOffset + (p.column > 0 ? p.column - 1 : 0));
        if (openBracketPos == std::wstring::npos || openBracketPos >= g_currentFileFullText.length()) {
            return -1;
        }

        size_t closeBracketPos = FindMacroCloseBracket(g_currentFileFullText, openBracketPos);
        if (closeBracketPos == std::wstring::npos) {
            return -1;
        }

        // Контроль попадания курсора внутрь скобок
        if (cursorAbsoluteOffset <= openBracketPos || cursorAbsoluteOffset > closeBracketPos) {
            return -1;
        }

        return targetParamId;
    }


    inline void ShiftDatabaseCoordinates(const std::string& targetFile, long targetLine, long visualStartCol, int lineDelta, int columnDelta);

    // === ЕДИНЫЙ ПОЛНОСТЬЮ ОЧИЩЕННЫЙ ДИСПЕТЧЕР LivePT ===
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

        bool wasChanged = (currentLineCount != g_lastLineCount) ||
            (line == g_lastLine && currentLineText != g_lastLineTextBuffer);

        if (wasChanged) {
            int lineDelta = static_cast<int>(currentLineCount - g_lastLineCount);
            int columnDelta = static_cast<int>(currentLineLength - g_lastLineLength);
            long visualStartCol = (column < g_lastCol) ? column : g_lastCol;

            // Защита: Если количество строк не менялось (lineDelta == 0), но мы правим текст текущей строки,
            // сдвиг колонок должен происходить только для элементов, которые находятся ПРАВЕЕ курсора.
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
        if (!LivePT::initVsEditor() || !LivePT::pDTE) return;

        std::string rawPath = location.file_name();
        std::string normalizedPath = LivePT::NormalizePath(rawPath.c_str());
        long staticLine = static_cast<long>(location.line());
        long staticColumn = static_cast<long>(location.column());

        // СТРОГИЙ ЛУКАП: Ищем точное совпадение скомпилированной Live-функции
        auto& fileCompileTree = GetFileCompileTree();
        auto itFile = fileCompileTree.find(normalizedPath);
        if (itFile == fileCompileTree.end()) return; // Незарегана — выходим!

        StaticOrderKey staticKey{ static_cast<int>(staticLine), static_cast<int>(staticColumn) };
        auto itParam = itFile->second.find(staticKey);
        if (itParam == itFile->second.end()) return; // Незарегана — выходим!

        int baseParamId = itParam->second;
        auto& params = LivePT::getParamDesc();
        if (baseParamId < 0 || baseParamId >= static_cast<int>(params.size())) return;

        // ВТОРОЙ ЛУКАП: Находим динамический живой ID
        unsigned int counterID = params[baseParamId].counterID;
        std::string vsLookupKey = normalizedPath + ":" + std::to_string(counterID);
        int realParamId = LivePT::getID(vsLookupKey);
        if (realParamId < 0 || realParamId >= static_cast<int>(params.size())) realParamId = baseParamId;

        // БЕРЕМ ИДЕАЛЬНО СДВИГНУТЫЕ КООРДИНАТЫ ИЗ БАЗЫ
        const auto& liveParam = params[realParamId];
        long targetLine = static_cast<long>(liveParam.line);
        long targetCol = static_cast<long>(liveParam.column);

        std::wstring wTargetDocPath(liveParam.fileName.begin(), liveParam.fileName.end());
        wchar_t absoluteBuffer[MAX_PATH] = { 0 };
        GetFullPathNameW(wTargetDocPath.c_str(), MAX_PATH, absoluteBuffer, nullptr);
        wTargetDocPath = absoluteBuffer;

        // --- Фокусировка окна Visual Studio ---
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
            if (gameThreadId != vsThreadId) ::AttachThreadInput(gameThreadId, vsThreadId, TRUE);
            ::ShowWindow(hVSMainWnd, SW_SHOW);
            ::SetForegroundWindow(hVSMainWnd);
            ::SetFocus(hVSMainWnd);
            if (gameThreadId != vsThreadId) ::AttachThreadInput(gameThreadId, vsThreadId, FALSE);
        }

        // --- Открытие документа через EnvDTE ---
        CComVariant vtItemOperations;
        if (FAILED(LivePT::AutoWrap(DISPATCH_PROPERTYGET, &vtItemOperations, LivePT::pDTE, L"ItemOperations", 0)) || !vtItemOperations.pdispVal) return;

        CComBSTR bstrPath(wTargetDocPath.c_str());
        CComBSTR bstrKind(L"{00000000-0000-0000-0000-000000000000}");
        CComVariant vtTargetWindow;
        if (FAILED(LivePT::AutoWrap(DISPATCH_METHOD, &vtTargetWindow, vtItemOperations.pdispVal, L"OpenFile", 2, CComVariant(bstrPath), CComVariant(bstrKind))) || !vtTargetWindow.pdispVal) return;

        LivePT::AutoWrap(DISPATCH_METHOD, NULL, vtTargetWindow.pdispVal, L"Activate", 0);
        HRESULT hr;
        CComVariant vtSelection;
        if (SUCCEEDED(LivePT::AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, vtTargetWindow.pdispVal, L"Selection", 0)) && vtSelection.pdispVal) {
            LivePT::AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"MoveToLineAndOffset", 3,
                CComVariant(targetLine),
                CComVariant(targetCol > 0 ? targetCol : 1L),
                CComVariant(0L));

            VARIANT vtActiveDoc; VariantInit(&vtActiveDoc);
            if (SUCCEEDED(hr = pDTE ? AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0) : E_FAIL) && vtActiveDoc.pdispVal) {
                IDispatch* pActiveDoc = vtActiveDoc.pdispVal;
                g_currentActiveFile = GetActiveDocumentPath(pActiveDoc);
                g_lastLineTextBuffer = DownloadCurrentLineText(pActiveDoc);
                g_lastLine = targetLine;
                g_lastCol = targetCol > 0 ? targetCol : 1L;
                VariantClear(&vtActiveDoc);
            }
        }
    }


    inline bool IsCursorInsideFunctionCall(const std::source_location& location) {
        if (g_currentFileFullText.empty()) return false;

        std::string activeFile = LivePT::NormalizePath(location.file_name());
        if (activeFile != g_currentActiveFile) return false;

        long staticLine = static_cast<long>(location.line());
        long staticColumn = static_cast<long>(location.column());

        // СТРОГИЙ ЛУКАП ПО БАЗЕ: Ищем точное совпадение скомпилированной Live-функции
        auto& fileCompileTree = GetFileCompileTree();
        auto itFile = fileCompileTree.find(activeFile);
        if (itFile == fileCompileTree.end()) return false; // Незарегана — игнорируем хайлайт

        StaticOrderKey staticKey{ static_cast<int>(staticLine), static_cast<int>(staticColumn) };
        auto itParam = itFile->second.find(staticKey);
        if (itParam == itFile->second.end()) return false; // Незарегана — игнорируем хайлайт

        int baseParamId = itParam->second;
        auto& params = LivePT::getParamDesc();
        if (baseParamId < 0 || baseParamId >= static_cast<int>(params.size())) return false;

        // ВТОРОЙ ЛУКАП: Находим динамический живой ID из рантайм-реестра VS
        unsigned int counterID = params[baseParamId].counterID;
        std::string vsLookupKey = activeFile + ":" + std::to_string(counterID);

        int realParamId = LivePT::getID(vsLookupKey);
        if (realParamId < 0 || realParamId >= static_cast<int>(params.size())) {
            realParamId = baseParamId;
        }

        // Берем идеально выровненную стартовую строку и имя зарегистрированного токена
        long startLiveLine = static_cast<long>(params[realParamId].line);
        std::string rawMacroName = params[realParamId].macroName;
        std::wstring wMacroName(rawMacroName.begin(), rawMacroName.end());

        // Кэширование кадра для исключения фризов UI-потока при отрисовке
        static std::string cachedActiveFile = "";
        static long cachedVSLine = -1;
        static long cachedRealParamId = -1;
        static bool cachedResult = false;

        if (cachedActiveFile == g_currentActiveFile && cachedVSLine == g_lastLine && cachedRealParamId == realParamId) {
            return cachedResult;
        }

        cachedActiveFile = g_currentActiveFile;
        cachedVSLine = g_lastLine;
        cachedRealParamId = realParamId;
        cachedResult = false;

        // Переводим startLiveLine в физическое смещение wchar_t в буфере файла
        size_t lineStartOffset = 0;
        long currentLineIdx = 1;
        while (currentLineIdx < startLiveLine && lineStartOffset < g_currentFileFullText.length()) {
            size_t nextNL = g_currentFileFullText.find(L'\n', lineStartOffset);
            if (nextNL != std::wstring::npos) {
                lineStartOffset = nextNL + 1;
                currentLineIdx++;
            }
            else break;
        }

        if (lineStartOffset >= g_currentFileFullText.length()) return false;

        // Ищем токен прямо на выровненной строке
        size_t nextLineNL = g_currentFileFullText.find(L'\n', lineStartOffset);
        size_t scanLimit = (nextLineNL != std::wstring::npos) ? nextLineNL : g_currentFileFullText.length();

        size_t macroOffset = g_currentFileFullText.find(wMacroName, lineStartOffset);
        if (macroOffset == std::wstring::npos || macroOffset > scanLimit) {
            macroOffset = g_currentFileFullText.rfind(wMacroName, scanLimit);
        }

        if (macroOffset != std::wstring::npos) {
            long foundStartLine = startLiveLine;
            size_t endOffset = std::wstring::npos;

            // Разделение логики: если это старый eval (когда вызван напрямую), считаем его скобки.
            // Для Live-функций — честно ведем подсчет диапазона до закрывающей точки с запятой ';'
            if (rawMacroName == "eval") {
                size_t openBracket = g_currentFileFullText.find(L'(', macroOffset);
                if (openBracket != std::wstring::npos) {
                    endOffset = FindMacroCloseBracket(g_currentFileFullText, openBracket);
                }
            }
            else {
                endOffset = g_currentFileFullText.find(L';', macroOffset);
            }

            if (endOffset != std::wstring::npos && endOffset >= macroOffset) {
                long foundEndLine = foundStartLine;
                size_t scanOffset = macroOffset;
                while (scanOffset < endOffset && scanOffset < g_currentFileFullText.length()) {
                    size_t nextNL = g_currentFileFullText.find(L'\n', scanOffset);
                    if (nextNL != std::wstring::npos && nextNL < endOffset) {
                        foundEndLine++;
                        scanOffset = nextNL + 1;
                    }
                    else break;
                }

                // Проверяем, находится ли курсор Visual Studio (g_lastLine) внутри всего многострочного вызова
                if (g_lastLine >= startLiveLine && g_lastLine <= foundEndLine) {
                    cachedResult = true;
                }
            }
        }

        return cachedResult;
    }


    inline std::atomic<bool> g_IsLptPdbWarmupCompleted{ false };

    // Вспомогательная фоновая функция для рекурсивного парсинга вложенных типов из PDB
    inline void DeepWarmupTypeFromPdb(const std::wstring& wTypeName, std::unordered_map<std::wstring, std::vector<StructMemberDesc>>& cache) {
        // Защита от бесконечной рекурсии (если тип уже обработан)
        if (cache.find(wTypeName) != cache.end()) return;

        std::vector<std::string> fNames;
        std::vector<DWORD> fOffsets;
        std::vector<DWORD> fSizes;
        std::vector<std::string> fTypes;

        // Прямой фоновый запрос к DbgHelp — абсолютно безопасен и не фризит UI-поток
        if (LoadStructMetadataDirect(wTypeName.c_str(), fNames, fOffsets, fSizes, fTypes)) {
            std::vector<StructMemberDesc> loadedMembers;
            loadedMembers.reserve(fOffsets.size());

            for (size_t k = 0; k < fOffsets.size(); ++k) {
                loadedMembers.push_back({ fNames[k], fOffsets[k], fSizes[k], fTypes[k] });
            }

            // Записываем структуру в локальный кэш потока
            cache[wTypeName] = loadedMembers;

            // РЕКУРСИВНЫЙ АНАЛИЗ ПОЛЕЙ: Ищем скрытые подструктуры
            for (const auto& member : loadedMembers) {
                // Если тип поля не является базовым примитивом C++ — уходим вглубь
                if (member.typeName != "float" && member.typeName != "int" &&
                    member.typeName != "bool" && member.typeName != "double" &&
                    member.typeName != "char" && member.typeName != "unsigned char")
                {
                    std::string cleanNestedName = member.typeName;
                    if (cleanNestedName.rfind("struct ", 0) == 0) cleanNestedName.erase(0, 7);
                    if (cleanNestedName.rfind("class ", 0) == 0) cleanNestedName.erase(0, 6);

                    size_t len = cleanNestedName.length();
                    std::wstring wNestedName(len, L'\0');
                    MultiByteToWideChar(CP_ACP, 0, cleanNestedName.c_str(), static_cast<int>(len), wNestedName.data(), static_cast<int>(len));

                    // Рекурсивно выкачиваем внутренности (например, pos2, size2, color3)
                    DeepWarmupTypeFromPdb(wNestedName, cache);
                }
            }
        }
    }

    inline void WarmupAllDatabaseParams() {
        static bool isWarmedUp = false;
        if (isWarmedUp) return;

        auto& params = LivePT::getParamDesc();
        const size_t totalParams = params.size();
        if (totalParams == 0) return;

        isWarmedUp = true;

        std::thread warmupThread([totalParams]() {
            auto& localParams = LivePT::getParamDesc();
            std::unordered_map<std::wstring, std::vector<StructMemberDesc>> localMetadataCache;
            localMetadataCache.reserve(16);

            for (size_t i = 0; i < totalParams; ++i) {
                auto& p = localParams[i];
                if (!p.value.has_value()) continue;

                // [ИСПРАВЛЕНО]: Если это чистый энум — прогреваем его метаданные из PDB прямо сейчас!
                if (p.enumInfo.isEnum && p.enumInfo.elements.empty()) {
                    std::string rawTypeName = p.value.type().name();
                    const char* typePtr = rawTypeName.c_str();
                    if (strncmp(typePtr, "enum ", 5) == 0) typePtr += 5;
                    else if (strncmp(typePtr, "class ", 6) == 0) typePtr += 6;

                    size_t len = strlen(typePtr);
                    std::wstring wEnumName(len, L'\0');
                    MultiByteToWideChar(CP_ACP, 0, typePtr, static_cast<int>(len), wEnumName.data(), static_cast<int>(len));

                    std::vector<std::string> parsedNames;
                    std::vector<int> parsedValues;
                    if (LoadEnumMetadataDirect(wEnumName.c_str(), parsedNames, parsedValues)) {
                        for (size_t eIdx = 0; eIdx < parsedNames.size(); ++eIdx) {
                            p.enumInfo.elements.push_back({ parsedValues[eIdx], parsedNames[eIdx] });
                        }
                    }
                    continue;
                }

                std::string typeNameAnsi = p.value.type().name();
                if (typeNameAnsi.empty()) continue;

                const char* typePtr = typeNameAnsi.c_str();
                if (strncmp(typePtr, "struct ", 7) == 0) typePtr += 7;
                else if (strncmp(typePtr, "class ", 6) == 0) typePtr += 6;

                size_t len = strlen(typePtr);
                std::wstring wTypeName(len, L'\0');
                MultiByteToWideChar(CP_ACP, 0, typePtr, static_cast<int>(len), wTypeName.data(), static_cast<int>(len));

                DeepWarmupTypeFromPdb(wTypeName, localMetadataCache);
            }

            for (size_t i = 0; i < totalParams; ++i) {
                auto& p = localParams[i];
                if (!p.value.has_value()) continue;

                std::string typeNameAnsi = p.value.type().name();

                // Пропускаем энумы, их элементы мы уже обработали на Шаге 1
                if (p.enumInfo.isEnum || typeNameAnsi.find("enum ") != std::string::npos) {
                    continue;
                }

                const char* typePtr = typeNameAnsi.c_str();
                if (strncmp(typePtr, "struct ", 7) == 0) typePtr += 7;
                else if (strncmp(typePtr, "class ", 6) == 0) typePtr += 6;

                size_t len = strlen(typePtr);
                std::wstring wTypeName(len, L'\0');
                MultiByteToWideChar(CP_ACP, 0, typePtr, static_cast<int>(len), wTypeName.data(), static_cast<int>(len));

                auto it = localMetadataCache.find(wTypeName);
                if (it != localMetadataCache.end()) {
                    p.structInfo.members = it->second;
                    p.structInfo.isStruct = true;
                }
            }

            // Шаг 3: Сохраняем выкачанные типы структур в изолированный глобальный реестр схем
            auto& globalSchemaRegistry = GetGlobalTypeSchemaRegistry();
            for (const auto& [wName, members] : localMetadataCache) {
                std::string ansiName(wName.begin(), wName.end());
                globalSchemaRegistry[ansiName] = members;
            }

            // Сигнализируем основному циклу, что вся база данных типов готова к работе
            g_IsLptPdbWarmupCompleted = true;
            Log("[LivePT Deep Warmup] Background recursive PDB parsing completed. Enums and Structs isolated successfully.");
            });

        warmupThread.detach();
    }


    inline void Warmup() {
        static bool isFirstFrame = true;
        if (isFirstFrame) {
            auto startTime = std::chrono::high_resolution_clock::now();

            LivePT::WarmupAllDatabaseParams();

            auto endTime = std::chrono::high_resolution_clock::now();
            auto durationMicro = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            double durationMilli = durationMicro / 1000.0;

            LivePT::Log("[LivePT External Profile] Warmup execution time: " +
                std::to_string(durationMilli) + " ms (" + std::to_string(durationMicro) + " us).");

            isFirstFrame = false;
        }
    }


}
