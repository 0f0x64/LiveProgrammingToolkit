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

    int GetParamIndexByTextOrder(const std::wstring & fileText, const std::string & currentActiveFile, long cursorLine, size_t evalIdxInLine) {

            std::vector<std::wstring> lines;
            std::wstring currentLine;
            const size_t TAB_SIZE = 4; // VS TAB SIZE

            for (size_t i = 0; i < fileText.length(); ++i) {
                wchar_t ch = fileText[i];
                if (ch == L'\r') {
                    continue;
                }
                if (ch == L'\n') {
                    lines.push_back(currentLine);
                    currentLine.clear();
                }
                else if (ch == L'\t') {

                    size_t spacesToAdd = TAB_SIZE - (currentLine.length() % TAB_SIZE);
                    currentLine.append(spacesToAdd, L' ');
                }
                else {
                    currentLine.push_back(ch);
                }
            }
            lines.push_back(currentLine);

            if (cursorLine < 1 || cursorLine > static_cast<long>(lines.size())) return -1;

            size_t globalFileCounter = 0;

            for (long l = 1; l < cursorLine; ++l) {
                const std::wstring& lineText = lines[l - 1];

                size_t firstNonSpace = lineText.find_first_not_of(L" \t");
                if (firstNonSpace != std::wstring::npos && lineText[firstNonSpace] == L'#') continue;
                if (lineText.find(L"//") != std::wstring::npos) continue;

                size_t searchPos = 0;
                while ((searchPos = lineText.find(L"eval", searchPos)) != std::wstring::npos) {

                    bool validLeft = (searchPos == 0 || !iswalnum(lineText[searchPos - 1]) && lineText[searchPos - 1] != L'_');
                    bool validRight = (searchPos + 4 >= lineText.length() || !iswalnum(lineText[searchPos + 4]) && lineText[searchPos + 4] != L'_');

                    if (validLeft && validRight) {
                        globalFileCounter++;
                    }
                    searchPos += 4;
                }
            }

            size_t finalGlobalFileIndex = globalFileCounter + evalIdxInLine;

            return static_cast<int>(finalGlobalFileIndex);
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

    inline bool CheckCursorInsideEval(const std::wstring & fileText, long line, long column, size_t & outEvalIdxInLine, size_t & outTargetEvalAbsolutePos) {
            size_t lineStartOffset = GetLineStartOffset(fileText, line);
            size_t lineEndOffset = fileText.find(L'\n', lineStartOffset);
            if (lineEndOffset == std::wstring::npos) lineEndOffset = fileText.length();

            std::wstring wholeLineText = fileText.substr(lineStartOffset, lineEndOffset - lineStartOffset);
            size_t posInLine = 0; outEvalIdxInLine = 0;

            while ((posInLine = wholeLineText.find(L"eval", posInLine)) != std::wstring::npos) {
                bool validLeft = (posInLine == 0 || !iswalnum(wholeLineText[posInLine - 1]) && wholeLineText[posInLine - 1] != L'_');
                bool validRight = (posInLine + 4 >= wholeLineText.length() || !iswalnum(wholeLineText[posInLine + 4]) && wholeLineText[posInLine + 4] != L'_');

                if (validLeft && validRight) {
                    size_t openBracket = wholeLineText.find(L'(', posInLine + 4);
                    size_t closeBracket = FindCloseBracket(wholeLineText, openBracket);

                    if (openBracket != std::wstring::npos && closeBracket != std::wstring::npos) {
                        long evalStartCol = GetVisualColumn(wholeLineText, posInLine);
                        long evalEndCol = GetVisualColumn(wholeLineText, closeBracket);

                        if (column >= evalStartCol && column <= evalEndCol) {
                            outTargetEvalAbsolutePos = lineStartOffset + posInLine;
                            return true;
                        }
                    }
                    outEvalIdxInLine++;
                }
                posInLine += 4;
            }
            return false;
        }

    inline void ParseAndStoreParamValue(const std::wstring & fileText, const std::string & currentActiveFile, long line, size_t evalIdxInLine, size_t targetEvalAbsolutePos) {
            int counterID = GetParamIndexByTextOrder(fileText, currentActiveFile, line, evalIdxInLine);
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

    bool isMouseDragging();

    void vsEditor() {
            //if (isMouseDragging()) return;
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