namespace LivePT {

    inline std::string DbgHelpWideToUtf8(const wchar_t* wstr) {
        if (!wstr) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) return "";
        std::string str(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str.data(), size, nullptr, nullptr);
        return str;
    }

    inline bool LoadEnumMetadataDirect(const wchar_t* targetEnumName, std::vector<std::string>& outNames, std::vector<int>& outValues) {
        HANDLE hProcess = GetCurrentProcess();
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        std::wstring searchPath(exePath);
        size_t slashPos = searchPath.find_last_of(L"\\/");
        if (slashPos != std::wstring::npos) searchPath = searchPath.substr(0, slashPos);

        if (!SymInitialize(hProcess, DbgHelpWideToUtf8(searchPath.c_str()).c_str(), TRUE)) return false;
        SymSetOptions( SYMOPT_DEFERRED_LOADS | SYMOPT_ALLOW_ZERO_ADDRESS);

        DWORD64 moduleBase = SymLoadModuleExW(hProcess, nullptr, exePath, nullptr, 0, 0, nullptr, 0);
        if (!moduleBase) {
            SymCleanup(hProcess);
            return false;
        }

        std::string enumNameAnsi = DbgHelpWideToUtf8(targetEnumName);
        SYMBOL_INFO si = { 0 };
        si.SizeOfStruct = sizeof(SYMBOL_INFO);

        // ТОЧЕЧНЫЙ ФИКС: Используем системный SymGetTypeFromName. Он железно находит "Primitive::ptype" в PDB!
        bool found = SymGetTypeFromName(hProcess, moduleBase, enumNameAnsi.c_str(), &si);
        if (!found) {
            std::string fallback = "enum " + enumNameAnsi;
            found = SymGetTypeFromName(hProcess, moduleBase, fallback.c_str(), &si);
        }

        if (found) {
            DWORD childrenCount = 0;
            if (SymGetTypeInfo(hProcess, moduleBase, si.TypeIndex, TI_GET_CHILDRENCOUNT, &childrenCount)) {
                ULONG mallocSize = sizeof(TI_FINDCHILDREN_PARAMS) + (childrenCount * sizeof(ULONG));
                TI_FINDCHILDREN_PARAMS* pChildren = (TI_FINDCHILDREN_PARAMS*)malloc(mallocSize);
                if (pChildren) {
                    memset(pChildren, 0, mallocSize);
                    pChildren->Count = childrenCount;

                    if (SymGetTypeInfo(hProcess, moduleBase, si.TypeIndex, TI_FINDCHILDREN, pChildren)) {
                        for (DWORD i = 0; i < pChildren->Count; i++) {
                            ULONG childIndex = pChildren->ChildId[i];

                            wchar_t* pChildName = nullptr;
                            SymGetTypeInfo(hProcess, moduleBase, childIndex, TI_GET_SYMNAME, &pChildName);

                            VARIANT varValue;
                            VariantInit(&varValue);
                            SymGetTypeInfo(hProcess, moduleBase, childIndex, TI_GET_VALUE, &varValue);

                            int extractedValue = 0;
                            if (varValue.vt == VT_I4 || varValue.vt == VT_INT) {
                                extractedValue = varValue.lVal;
                            }
                            else if (varValue.vt == VT_UI4 || varValue.vt == VT_UINT) {
                                extractedValue = static_cast<int>(varValue.ulVal);
                            }
                            else {
                                int* rawDataPtr = reinterpret_cast<int*>(&varValue.lVal);
                                extractedValue = *rawDataPtr;
                            }

                            outNames.push_back(DbgHelpWideToUtf8(pChildName));
                            outValues.push_back(extractedValue);

                            if (pChildName) LocalFree(pChildName);
                            VariantClear(&varValue);
                        }
                    }
                    free(pChildren);
                }
            }
        }

        SymUnloadModule64(hProcess, moduleBase);
        SymCleanup(hProcess);
        return !outNames.empty();
    }

    struct DbgHelpStructContext {
        HANDLE hProcess;
        ULONG64 modBase;
        const wchar_t* targetName;
        std::vector<std::string>* pNames;
        std::vector<DWORD>* pOffsets;
        std::vector<DWORD>* pSizes;
        std::vector<std::string>* pTypeNames;
    };

    // Функция для получения текстового имени типа поля по его TypeIndex (внутри PDB)
    inline std::string DbgHelpGetTypeName(HANDLE hProcess, ULONG64 modBase, DWORD typeIndex) {
        wchar_t* pTypeName = nullptr;
        if (SymGetTypeInfo(hProcess, modBase, typeIndex, TI_GET_SYMNAME, &pTypeName) && pTypeName) {
            std::string res = DbgHelpWideToUtf8(pTypeName);
            LocalFree(pTypeName);
            return res;
        }

        DWORD baseType = 0;
        SymGetTypeInfo(hProcess, modBase, typeIndex, TI_GET_BASETYPE, &baseType);

        ULONG64 length = 0;
        SymGetTypeInfo(hProcess, modBase, typeIndex, TI_GET_LENGTH, &length);

        switch (baseType) {
        case 1:  return "void";
        case 2:  return "char";
        case 3:  return "wchar_t";
        case 6:  return (length == 8) ? "long long" : "int";
        case 7:  return (length == 8) ? "unsigned long long" : "unsigned int";
        case 8:  return (length == 4) ? "float" : "double";
        case 10: return "bool";
        default: return "unknown_type";
        }
    }

    inline BOOL CALLBACK DbgHelpStructTypesCallback(PSYMBOL_INFOW pSymInfo, ULONG SymbolSize, PVOID UserContext) {
        DbgHelpStructContext* ctx = (DbgHelpStructContext*)UserContext;

        if (wcscmp(pSymInfo->Name, ctx->targetName) == 0) {
            DWORD childrenCount = 0;
            if (SymGetTypeInfo(ctx->hProcess, ctx->modBase, pSymInfo->TypeIndex, TI_GET_CHILDRENCOUNT, &childrenCount) && childrenCount > 0) {
                ULONG mallocSize = sizeof(TI_FINDCHILDREN_PARAMS) + (childrenCount * sizeof(ULONG));
                TI_FINDCHILDREN_PARAMS* pChildren = (TI_FINDCHILDREN_PARAMS*)malloc(mallocSize);

                if (pChildren) {
                    memset(pChildren, 0, mallocSize);
                    pChildren->Count = childrenCount;

                    if (SymGetTypeInfo(ctx->hProcess, ctx->modBase, pSymInfo->TypeIndex, TI_FINDCHILDREN, pChildren)) {
                        for (DWORD i = 0; i < pChildren->Count; i++) {
                            ULONG childIndex = pChildren->ChildId[i];

                            DWORD symTag = 0;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, childIndex, TI_GET_SYMTAG, &symTag);
                            if (symTag != 7) continue; // 7 == SymTagData (нас интересуют только поля данных)

                            wchar_t* pChildName = nullptr;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, childIndex, TI_GET_SYMNAME, &pChildName);

                            DWORD offset = 0;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, childIndex, TI_GET_OFFSET, &offset);

                            DWORD fieldTypeIndex = 0;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, childIndex, TI_GET_TYPEID, &fieldTypeIndex);

                            ULONG64 fieldSize = 0;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, fieldTypeIndex, TI_GET_LENGTH, &fieldSize);

                            std::string fieldTypeName = DbgHelpGetTypeName(ctx->hProcess, ctx->modBase, fieldTypeIndex);

                            ctx->pNames->push_back(DbgHelpWideToUtf8(pChildName));
                            ctx->pOffsets->push_back(offset);
                            ctx->pSizes->push_back(static_cast<DWORD>(fieldSize));
                            ctx->pTypeNames->push_back(fieldTypeName);

                            if (pChildName) LocalFree(pChildName);
                        }
                    }
                    free(pChildren);
                }
            }
            return FALSE; // Структуру нашли, останавливаем сканирование
        }
        return TRUE;
    }

    // ГЛАВНЫЙ СИ-МЕТОД СБОРА АНАТОМИИ ЛЮБОЙ СТРУКТУРЫ ИЗ PDB Модуля
    inline bool LoadStructMetadataDirect(const wchar_t* targetStructName,
        std::vector<std::string>& outNames,
        std::vector<DWORD>& outOffsets,
        std::vector<DWORD>& outSizes,
        std::vector<std::string>& outTypeNames) {
        HANDLE hProcess = GetCurrentProcess();
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        std::wstring searchPath(exePath);
        size_t slashPos = searchPath.find_last_of(L"\\/");
        if (slashPos != std::wstring::npos) searchPath = searchPath.substr(0, slashPos);

        std::string searchPathAnsi = DbgHelpWideToUtf8(searchPath.c_str());
        if (!SymInitialize(hProcess, searchPathAnsi.c_str(), FALSE)) return false;

        SymSetOptions( SYMOPT_DEFERRED_LOADS | SYMOPT_ALLOW_ZERO_ADDRESS);

        DWORD64 moduleBase = SymLoadModuleExW(hProcess, nullptr, exePath, nullptr, 0, 0, nullptr, 0);
        if (!moduleBase) {
            SymCleanup(hProcess);
            return false;
        }

        DbgHelpStructContext ctx = { hProcess, moduleBase, targetStructName, &outNames, &outOffsets, &outSizes, &outTypeNames };
        SymEnumTypesW(hProcess, moduleBase, DbgHelpStructTypesCallback, &ctx);

        SymUnloadModule64(hProcess, moduleBase);
        SymCleanup(hProcess);
        return !outNames.empty();
    }

}