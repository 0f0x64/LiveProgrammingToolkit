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
        std::vector<std::wstring> lines; std::wstring currentLine; const size_t TAB_SIZE = 4;
        for (size_t i = 0; i < fileText.length(); ++i) {
            wchar_t ch = fileText[i]; if (ch == L'\r') continue;
            if (ch == L'\n') { lines.push_back(currentLine); currentLine.clear(); }
            else if (ch == L'\t') { currentLine.append(TAB_SIZE - (currentLine.length() % TAB_SIZE), L' '); }
            else currentLine.push_back(ch);
        }
        lines.push_back(currentLine);
        if (cursorLine < 1 || cursorLine > static_cast<long>(lines.size())) return -1;

        size_t globalFileCounter = 0; bool inBlockComment = false;
        for (long l = 1; l < cursorLine; ++l) {
            const std::wstring& lineText = lines[l - 1];
            size_t firstNonSpace = lineText.find_first_not_of(L" \t");
            if (firstNonSpace != std::wstring::npos && lineText[firstNonSpace] == L'#') continue;

            size_t searchPos = 0;
            while (searchPos < lineText.length()) {
                if (inBlockComment) {
                    size_t endCommentPos = lineText.find(L"*/", searchPos);
                    if (endCommentPos != std::wstring::npos) { inBlockComment = false; searchPos = endCommentPos + 2; }
                    else break;
                    continue;
                }
                size_t lineCommentPos = lineText.find(L"//", searchPos);
                size_t startBlockCommentPos = lineText.find(L"/*", searchPos);
                size_t evalPos = lineText.find(L"eval", searchPos);
                size_t minPos = (std::wstring::npos); int tokenType = 0;
                if (lineCommentPos != std::wstring::npos && lineCommentPos < minPos) { minPos = lineCommentPos; tokenType = 1; }
                if (startBlockCommentPos != std::wstring::npos && startBlockCommentPos < minPos) { minPos = startBlockCommentPos; tokenType = 2; }
                if (evalPos != std::wstring::npos && evalPos < minPos) { minPos = evalPos; tokenType = 3; }
                if (minPos == std::wstring::npos) break;

                if (tokenType == 1) break;
                else if (tokenType == 2) { inBlockComment = true; searchPos = minPos + 2; }
                else if (tokenType == 3) {
                    bool validLeft = (minPos == 0 || (!iswalnum(lineText[minPos - 1]) && lineText[minPos - 1] != L'_'));
                    bool validRight = (minPos + 4 >= lineText.length() || (!iswalnum(lineText[minPos + 4]) && lineText[minPos + 4] != L'_'));
                    if (validLeft && validRight) globalFileCounter++;
                    searchPos = minPos + 4;
                }
            }
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

    
    // Новый метод извлечения координат: берет абсолютное смещение символа из DTE
        // 1. Извлекаем абсолютное смещение символа напрямую из Automation API VS
    bool GetCursorAbsoluteOffset(IDispatch* pActiveDoc, long& outAbsoluteOffset) {
        VARIANT vtSelection; VariantInit(&vtSelection);
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, pActiveDoc, L"Selection", 0)) || vtSelection.vt != VT_DISPATCH || !vtSelection.pdispVal) {
            VariantClear(&vtSelection); return false;
        }

        VARIANT vtActivePoint; VariantInit(&vtActivePoint);
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtActivePoint, vtSelection.pdispVal, L"ActivePoint", 0)) || vtActivePoint.vt != VT_DISPATCH || !vtActivePoint.pdispVal) {
            VariantClear(&vtActivePoint); VariantClear(&vtSelection); return false;
        }

        VARIANT vtAbsoluteCharOffset; VariantInit(&vtAbsoluteCharOffset);
        if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtAbsoluteCharOffset, vtActivePoint.pdispVal, L"AbsoluteCharOffset", 0))) {
            VARIANT vtTarget; VariantInit(&vtTarget);
            if (SUCCEEDED(VariantChangeType(&vtTarget, &vtAbsoluteCharOffset, 0, VT_I4))) {
                outAbsoluteOffset = vtTarget.lVal;
            }
            VariantClear(&vtTarget);
        }

        VariantClear(&vtAbsoluteCharOffset); VariantClear(&vtActivePoint); VariantClear(&vtSelection);
        return (outAbsoluteOffset > 0);
    }

    // 2. Универсальный сквозной подсчет ID. Бежит от 0 до cursorOffset.
    // Идеально фильтрует ЛЮБЫЕ комментарии и директивы во всем файле, игнорируя табы и переносы!
    int GetParamIndexByAbsoluteOffset(const std::wstring& fileText, size_t targetEvalAbsolutePos) {
        size_t globalFileCounter = 0;
        bool inBlockComment = false;
        size_t searchPos = 0;

        while (searchPos < fileText.length()) {
            if (inBlockComment) {
                size_t endCommentPos = fileText.find(L"*/", searchPos);
                if (endCommentPos != std::wstring::npos) {
                    inBlockComment = false;
                    searchPos = endCommentPos + 2;
                }
                else {
                    break;
                }
                continue;
            }

            size_t lineCommentPos = fileText.find(L"//", searchPos);
            size_t startBlockCommentPos = fileText.find(L"/*", searchPos);
            size_t pragmaPos = fileText.find(L"\n#", searchPos);
            size_t evalPos = fileText.find(L"eval", searchPos);

            size_t minPos = (std::wstring::npos);
            int tokenType = 0;

            if (lineCommentPos != std::wstring::npos && lineCommentPos < minPos) { minPos = lineCommentPos; tokenType = 1; }
            if (startBlockCommentPos != std::wstring::npos && startBlockCommentPos < minPos) { minPos = startBlockCommentPos; tokenType = 2; }
            if (pragmaPos != std::wstring::npos && pragmaPos < minPos) { minPos = pragmaPos; tokenType = 3; }
            if (evalPos != std::wstring::npos && evalPos < minPos) { minPos = evalPos; tokenType = 4; }

            if (minPos == std::wstring::npos) break;

            // Если нашли токен, который находится ДАЛЬШЕ целевого макроса, прекращаем подсчет
            if (minPos > targetEvalAbsolutePos) break;

            if (tokenType == 1) {
                size_t nextNL = fileText.find(L'\n', minPos);
                searchPos = (nextNL != std::wstring::npos) ? nextNL + 1 : fileText.length();
            }
            else if (tokenType == 2) {
                inBlockComment = true;
                searchPos = minPos + 2;
            }
            else if (tokenType == 3) {
                size_t nextNL = fileText.find(L'\n', minPos + 1);
                searchPos = (nextNL != std::wstring::npos) ? nextNL + 1 : fileText.length();
            }
            else if (tokenType == 4) {
                bool validLeft = (minPos == 0 || (!iswalnum(fileText[minPos - 1]) && fileText[minPos - 1] != L'_'));
                bool validRight = (minPos + 4 >= fileText.length() || (!iswalnum(fileText[minPos + 4]) && fileText[minPos + 4] != L'_'));

                if (validLeft && validRight) {
                    globalFileCounter++;
                    // Если дошли ровно до целевого макроса — это финиш
                    if (minPos == targetEvalAbsolutePos) break;
                }
                searchPos = minPos + 4;
            }
        }

        return (globalFileCounter > 0) ? static_cast<int>(globalFileCounter) - 1 : -1;
    }




    // Новый метод проверки границ: работает строго по абсолютному смещению символов в std::wstring
        // Полностью отлаженный лексер: без рекурсивных find и коллизий границ слова
        // Полностью отлаженный лексер: без рекурсивных find и коллизий границ слова
    bool IsOffsetInsideEval(const std::wstring& fileText, long cursorAbsoluteOffset, size_t& outEvalIdxInLine, size_t& outTargetEvalAbsolutePos, long& outLine) {
        size_t searchPos = 0;
        bool inBlockComment = false;

        outTargetEvalAbsolutePos = std::wstring::npos;
        outLine = 1;
        outEvalIdxInLine = 0;

        while (searchPos < fileText.length()) {
            if (inBlockComment) {
                size_t endCommentPos = fileText.find(L"*/", searchPos);
                if (endCommentPos != std::wstring::npos) {
                    inBlockComment = false;
                    searchPos = endCommentPos + 2;
                }
                else {
                    break;
                }
                continue;
            }

            size_t lineCommentPos = fileText.find(L"//", searchPos);
            size_t startBlockCommentPos = fileText.find(L"/*", searchPos);
            size_t pragmaPos = fileText.find(L"\n#", searchPos);
            size_t evalPos = fileText.find(L"eval", searchPos);

            size_t minPos = (std::wstring::npos);
            int tokenType = 0;

            if (lineCommentPos != std::wstring::npos && lineCommentPos < minPos) { minPos = lineCommentPos; tokenType = 1; }
            if (startBlockCommentPos != std::wstring::npos && startBlockCommentPos < minPos) { minPos = startBlockCommentPos; tokenType = 2; }
            if (pragmaPos != std::wstring::npos && pragmaPos < minPos) { minPos = pragmaPos; tokenType = 3; }
            if (evalPos != std::wstring::npos && evalPos < minPos) { minPos = evalPos; tokenType = 4; }

            if (minPos == std::wstring::npos) break;

            if (tokenType == 1) {
                size_t nextNL = fileText.find(L'\n', minPos);
                searchPos = (nextNL != std::wstring::npos) ? nextNL + 1 : fileText.length();
            }
            else if (tokenType == 2) {
                inBlockComment = true;
                searchPos = minPos + 2;
            }
            else if (tokenType == 3) {
                size_t nextNL = fileText.find(L'\n', minPos + 1);
                searchPos = (nextNL != std::wstring::npos) ? nextNL + 1 : fileText.length();
            }
            else if (tokenType == 4) {
                bool validLeft = (minPos == 0 || (!iswalnum(fileText[minPos - 1]) && fileText[minPos - 1] != L'_'));
                bool validRight = (minPos + 4 >= fileText.length() || (!iswalnum(fileText[minPos + 4]) && fileText[minPos + 4] != L'_'));

                if (validLeft && validRight) {
                    size_t openBracket = fileText.find(L'(', minPos + 4);
                    size_t closeBracket = FindCloseBracket(fileText, openBracket);

                    if (openBracket != std::wstring::npos && closeBracket != std::wstring::npos) {
                        if (static_cast<size_t>(cursorAbsoluteOffset) >= minPos && static_cast<size_t>(cursorAbsoluteOffset) <= closeBracket) {
                            outTargetEvalAbsolutePos = minPos;

                            outLine = 1;
                            for (size_t i = 0; i < minPos; ++i) { if (fileText[i] == L'\n') outLine++; }

                            size_t lineStartOffset = 0;
                            for (size_t i = minPos; i > 0; --i) { if (fileText[i] == L'\n') { lineStartOffset = i + 1; break; } }

                            outEvalIdxInLine = 0;
                            size_t checkPos = lineStartOffset;
                            while ((checkPos = fileText.find(L"eval", checkPos)) != std::wstring::npos && checkPos < minPos) {
                                bool cLeft = (checkPos == 0 || (!iswalnum(fileText[checkPos - 1]) && fileText[checkPos - 1] != L'_'));
                                bool cRight = (checkPos + 4 >= fileText.length() || (!iswalnum(fileText[checkPos + 4]) && fileText[checkPos + 4] != L'_'));
                                if (cLeft && cRight) {
                                    outEvalIdxInLine++;
                                }
                                checkPos += 4;
                            }
                            return true;
                        }
                    }
                }
                searchPos = minPos + 4;
            }
        }
        return false;
    }



    

    inline void ParseAndStoreParamValue(const std::wstring& fileText, const std::string& currentActiveFile, long line, size_t evalIdxInLine, size_t targetEvalAbsolutePos) {
        int counterID = GetParamIndexByAbsoluteOffset(fileText, targetEvalAbsolutePos);
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

            // АВТОМАТИЧЕСКАЯ РАСПАКОВКА СТРУКТУР И АГРЕГАТОВ {...}
            size_t openBrace = innerValueA.find('{');
            if (openBrace != std::string::npos) {
                size_t closeBrace = innerValueA.rfind('}');
                if (closeBrace != std::string::npos && closeBrace > openBrace) {
                    std::string content = innerValueA.substr(openBrace + 1, closeBrace - openBrace - 1);
                    std::stringstream ss(content); std::string token; std::string cleanValuesStr = "";

                    while (std::getline(ss, token, ',')) {
                        size_t eqPos = token.find('=');
                        std::string rawValue = (eqPos != std::string::npos) ? token.substr(eqPos + 1) : token;

                        // Чистим литерал поля от мусора
                        rawValue.erase(0, rawValue.find_first_not_of(" \t\r\n"));
                        rawValue.erase(rawValue.find_last_not_of(" \t\r\n") + 1);

                        // Удаляем префиксы областей видимости типа "Primitive::ptype::" внутри полей
                        size_t tokenCols = rawValue.rfind("::");
                        if (tokenCols != std::string::npos) rawValue = rawValue.substr(tokenCols + 2);

                        if (!rawValue.empty()) {
                            if (!cleanValuesStr.empty()) cleanValuesStr += " ";
                            cleanValuesStr += rawValue;
                        }
                    }
                    UpdateParamValue(targetId, cleanValuesStr);
                    return;
                }
            }

            // ОЧИСТКА БАЗОВЫХ ТИПОВ: Удаляем префиксы "::" (превращаем "Primitive::ptype::box" -> "box")
            size_t lastCols = innerValueA.rfind("::");
            if (lastCols != std::string::npos) {
                innerValueA = innerValueA.substr(lastCols + 2);
            }

            UpdateParamValue(targetId, innerValueA);
        }
    }

    bool isMouseDragging();

    void vsEditor() {
        if (!initVsEditor()) return;

        VARIANT vtActiveDoc; VariantInit(&vtActiveDoc);
        HRESULT hr = pDTE ? AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0) : E_FAIL;
        if (hr == CO_E_OBJNOTCONNECTED || hr == RPC_E_DISCONNECTED || hr == E_ACCESSDENIED) { ResetDTEConnection(); return; }
        if (hr == RPC_E_CALL_REJECTED || hr == 0x8001010A || FAILED(hr) || !vtActiveDoc.pdispVal) { VariantClear(&vtActiveDoc); return; }

        IDispatch* pActiveDoc = vtActiveDoc.pdispVal;
        std::string currentActiveFile = GetActiveDocumentPath(pActiveDoc);
        if (currentActiveFile.empty()) { VariantClear(&vtActiveDoc); return; }

        long absoluteCharOffset1Based = 0;
        if (!GetCursorAbsoluteOffset(pActiveDoc, absoluteCharOffset1Based)) { VariantClear(&vtActiveDoc); return; }
        size_t cursorAbsoluteOffset = static_cast<size_t>(absoluteCharOffset1Based) - 1;

        std::wstring rawFileText = DownloadDocumentText(pActiveDoc);
        if (rawFileText.empty()) { VariantClear(&vtActiveDoc); return; }

        // СИНХРОНИЗАЦИЯ: убираем \r, чтобы AbsoluteCharOffset из DTE совпал с индексами std::wstring
        std::wstring fileText = L""; fileText.reserve(rawFileText.length());
        for (wchar_t ch : rawFileText) { if (ch != L'\r') fileText.push_back(ch); }

        size_t evalIdxInLine = 0; size_t targetEvalAbsolutePos = std::wstring::npos; long line = 0;

        // Передаем управление вашему отлаженному лексеру на смещениях
        if (!IsOffsetInsideEval(fileText, cursorAbsoluteOffset, evalIdxInLine, targetEvalAbsolutePos, line)) {
            VariantClear(&vtActiveDoc); return;
        }

        ParseAndStoreParamValue(fileText, currentActiveFile, line, evalIdxInLine, targetEvalAbsolutePos);
        VariantClear(&vtActiveDoc);
    }


    
}