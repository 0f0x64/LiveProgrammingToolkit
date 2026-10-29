#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

#ifndef SymTagArrayType
#define SymTagArrayType 15
#endif

namespace LivePT {

    static bool g_isDbgHelpGlobalReady = false;
    static HANDLE g_hDbgHelpGlobalProcess = NULL;
    static DWORD64 g_DbgHelpGlobalModuleBase = 0;

    struct DbgHelpGlobalCleaner {
        ~DbgHelpGlobalCleaner() {
            if (g_isDbgHelpGlobalReady && g_DbgHelpGlobalModuleBase && g_hDbgHelpGlobalProcess) {
                SymUnloadModule64(g_hDbgHelpGlobalProcess, g_DbgHelpGlobalModuleBase);
                SymCleanup(g_hDbgHelpGlobalProcess);
            }
        }
    };

    inline std::string DbgHelpWideToUtf8(const wchar_t* wstr) {
        if (!wstr) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) return "";
        std::string str(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str.data(), size, nullptr, nullptr);
        return str;
    }

    inline bool EnsureDbgHelpInitialized() {
        if (g_isDbgHelpGlobalReady) return true;

        g_hDbgHelpGlobalProcess = GetCurrentProcess();
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        std::wstring searchPath(exePath);
        size_t slashPos = searchPath.find_last_of(L"\\/");
        if (slashPos != std::wstring::npos) searchPath = searchPath.substr(0, slashPos);

        std::string searchPathAnsi = DbgHelpWideToUtf8(searchPath.c_str());
        if (!SymInitialize(g_hDbgHelpGlobalProcess, searchPathAnsi.c_str(), FALSE)) return false;

        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_ALLOW_ZERO_ADDRESS);

        g_DbgHelpGlobalModuleBase = SymLoadModuleExW(g_hDbgHelpGlobalProcess, nullptr, exePath, nullptr, 0, 0, nullptr, 0);
        if (!g_DbgHelpGlobalModuleBase) {
            g_DbgHelpGlobalModuleBase = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
        }

        if (!g_DbgHelpGlobalModuleBase) {
            SymCleanup(g_hDbgHelpGlobalProcess);
            return false;
        }

        static DbgHelpGlobalCleaner globalCleaner;
        g_isDbgHelpGlobalReady = true;
        return true;
    }

  
    
    // Добавим структуру для контекста поиска по суффиксу
    struct LptEnumSuffixContext {
        HANDLE hProcess;
        DWORD64 moduleBase;
        std::wstring targetSuffix;       // Что ищем (например, L"MyEnum")
        std::wstring bestMatchedFullName;// Лучшее найденное полное имя
        DWORD bestTypeIndex = 0;         // Индекс лучшего типа
    };

    static BOOL CALLBACK LptEnumSuffixCallback(PSYMBOL_INFOW pSymInfo, ULONG SymbolSize, PVOID UserContext) {
        if (!pSymInfo || !UserContext) return TRUE;
        LptEnumSuffixContext* ctx = reinterpret_cast<LptEnumSuffixContext*>(UserContext);

        std::wstring currentName(pSymInfo->Name);
        if (currentName.find(L"lambda") != std::wstring::npos) return TRUE;

        // Очищаем префиксы PDB
        if (currentName.rfind(L"enum ", 0) == 0)  currentName.erase(0, 5);
        if (currentName.rfind(L"class ", 0) == 0) currentName.erase(0, 6);

        // Проверяем, совпадает ли имя полностью или заканчивается ли на ::MyEnum
        bool isExact = (currentName == ctx->targetSuffix);
        std::wstring suffixToCheck = L"::" + ctx->targetSuffix;
        bool isSuffix = (currentName.length() >= suffixToCheck.length() &&
            currentName.compare(currentName.length() - suffixToCheck.length(), suffixToCheck.length(), suffixToCheck) == 0);

        if (isExact || isSuffix) {
            // Выбираем имя с наименьшей длиной (наименьшее смещение / самый близкий namespace)
            if (ctx->bestTypeIndex == 0 || currentName.length() < ctx->bestMatchedFullName.length()) {
                ctx->bestMatchedFullName = currentName;
                ctx->bestTypeIndex = pSymInfo->TypeIndex;
            }
            // Если нашли абсолютно точное совпадение без namespace, можно тормозить перебор
            if (isExact) return FALSE;
        }
        return TRUE;
    }


    struct CachedPdbEnum {
        std::vector<std::string> names;
        std::vector<int> values;
    };

    // Хэш-карта: "ИмяТипаEnum" -> "Списки имен и значений"
    // Ключом будет оригинальный member->typeName (например, "enum MyNamespace::GameStatus")
    inline std::unordered_map<std::string, CachedPdbEnum> g_PdbEnumCache;

    // Теперь обновляем сам метод LoadEnumMetadataDirect
    inline bool LoadEnumMetadataDirect(const wchar_t* targetEnumName, std::vector<std::string>& outNames, std::vector<int>& outValues) {
        if (!EnsureDbgHelpInitialized()) return false;

        HANDLE hProcess = g_hDbgHelpGlobalProcess;
        DWORD64 moduleBase = g_DbgHelpGlobalModuleBase;

        std::wstring cleanEnumName(targetEnumName);
        // (Ваш код очистки префиксов enum/class оставляем без изменений...)
        size_t enumWord = cleanEnumName.find(L"enum ");
        if (enumWord != std::wstring::npos) cleanEnumName.erase(enumWord, 5);
        size_t classWord = cleanEnumName.find(L"class ");
        if (classWord != std::wstring::npos) cleanEnumName.erase(classWord, 6);
        cleanEnumName.erase(0, cleanEnumName.find_first_not_of(L" \t\r\n"));
        cleanEnumName.erase(cleanEnumName.find_last_not_of(L" \t\r\n") + 1);

        DWORD targetTypeIndex = 0;

        // 1. СТРАТЕГИЯ А: Быстрый точный поиск
        ULONG64 buffer[(sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(WCHAR) + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
        PSYMBOL_INFOW pSymbol = (PSYMBOL_INFOW)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        if (SymFromNameW(hProcess, cleanEnumName.c_str(), pSymbol)) {
            targetTypeIndex = pSymbol->TypeIndex;
        }
        // 2. СТРАТЕГИЯ Б: Фолбек на поиск по суффиксу с наименьшей длиной (работает при using namespace)
        else {
            LptEnumSuffixContext suffixCtx = { hProcess, moduleBase, cleanEnumName, L"", 0 };
            SymEnumTypesW(hProcess, moduleBase, LptEnumSuffixCallback, &suffixCtx);
            if (suffixCtx.bestTypeIndex != 0) {
                targetTypeIndex = suffixCtx.bestTypeIndex;
            }
        }

        if (targetTypeIndex == 0) return false;

        // 3. Вытаскиваем дочерние элементы (Extract Children)
        DWORD childrenCount = 0;
        if (SymGetTypeInfo(hProcess, moduleBase, targetTypeIndex, TI_GET_CHILDRENCOUNT, &childrenCount) && childrenCount > 0) {
            ULONG mallocSize = sizeof(TI_FINDCHILDREN_PARAMS) + (childrenCount * sizeof(ULONG));
            TI_FINDCHILDREN_PARAMS* pChildren = (TI_FINDCHILDREN_PARAMS*)malloc(mallocSize);
            if (pChildren) {
                memset(pChildren, 0, mallocSize);
                pChildren->Count = childrenCount;
                if (SymGetTypeInfo(hProcess, moduleBase, targetTypeIndex, TI_FINDCHILDREN, pChildren)) {
                    for (DWORD i = 0; i < pChildren->Count; i++) {
                        ULONG childIndex = pChildren->ChildId[i];
                        wchar_t* pChildName = nullptr;
                        SymGetTypeInfo(hProcess, moduleBase, childIndex, TI_GET_SYMNAME, &pChildName);

                        VARIANT varValue; VariantInit(&varValue);
                        SymGetTypeInfo(hProcess, moduleBase, childIndex, TI_GET_VALUE, &varValue);

                        int extractedValue = 0;
                        if (varValue.vt == VT_I4 || varValue.vt == VT_INT) extractedValue = varValue.lVal;
                        else if (varValue.vt == VT_UI4 || varValue.vt == VT_UINT) extractedValue = static_cast<int>(varValue.ulVal);
                        else extractedValue = *reinterpret_cast<int*>(&varValue.lVal);

                        if (pChildName) {
                            outNames.push_back(DbgHelpWideToUtf8(pChildName));
                            LocalFree(pChildName);
                        }
                        outValues.push_back(extractedValue);
                        VariantClear(&varValue);
                    }
                }
                free(pChildren);
            }
        }
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
        case 6:  // Знаковые целые типы
            if (length == 8) return "long long";
            if (length == 2) return "short";
            if (length == 1) return "char";
            return "int";
        case 7:  // Беззнаковые целые типы (unsigned)
            if (length == 8) return "unsigned long long";
            if (length == 2) return "unsigned short";
            if (length == 1) return "unsigned char"; 
            return "unsigned int";
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
                            if (symTag != 7) continue; 

                            wchar_t* pChildName = nullptr;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, childIndex, TI_GET_SYMNAME, &pChildName);

                            DWORD offset = 0;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, childIndex, TI_GET_OFFSET, &offset);

                            DWORD fieldTypeIndex = 0;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, childIndex, TI_GET_TYPEID, &fieldTypeIndex);

                            DWORD fieldSymTag = 0;
                            SymGetTypeInfo(ctx->hProcess, ctx->modBase, fieldTypeIndex, TI_GET_SYMTAG, &fieldSymTag);

                            std::string fieldTypeName = "";
                            ULONG64 fieldSize = 0;

                            if (fieldSymTag == SymTagArrayType) {
                                DWORD arrayElementTypeId = 0;
                                SymGetTypeInfo(ctx->hProcess, ctx->modBase, fieldTypeIndex, TI_GET_TYPEID, &arrayElementTypeId);
                                fieldTypeName = DbgHelpGetTypeName(ctx->hProcess, ctx->modBase, arrayElementTypeId);

                                DWORD arrayCount = 0;
                                SymGetTypeInfo(ctx->hProcess, ctx->modBase, fieldTypeIndex, TI_GET_COUNT, &arrayCount);

                                ULONG64 totalArraySize = 0;
                                SymGetTypeInfo(ctx->hProcess, ctx->modBase, fieldTypeIndex, TI_GET_LENGTH, &totalArraySize);

                                fieldTypeName += "[" + std::to_string(arrayCount) + "]";
                                fieldSize = totalArraySize;
                            }
                            else {
                                fieldTypeName = DbgHelpGetTypeName(ctx->hProcess, ctx->modBase, fieldTypeIndex);
                                SymGetTypeInfo(ctx->hProcess, ctx->modBase, fieldTypeIndex, TI_GET_LENGTH, &fieldSize);

                                if (fieldSize == 0) {
                                    bool isEnum = (fieldTypeName.rfind("enum ", 0) == 0);
                                    if (fieldSymTag == 2 || fieldTypeName == "ptype" || isEnum) {
                                    //if (fieldSymTag == 2 || fieldTypeName == "ptype" || fieldTypeName.find("enum") != std::string::npos) {
                                        fieldSize = 4;
                                    }
                                    else {
                                        fieldSize = 1; 
                                    }
                                }
                            }

                            std::string fNameAnsi = DbgHelpWideToUtf8(pChildName);

                            ctx->pNames->push_back(fNameAnsi);
                            ctx->pOffsets->push_back(offset);
                            ctx->pSizes->push_back(static_cast<DWORD>(fieldSize));
                            ctx->pTypeNames->push_back(fieldTypeName);

                            if (pChildName) LocalFree(pChildName);
                        }
                    }
                    free(pChildren);
                }
            }
            return FALSE;
        }
        return TRUE;
    }

    inline bool LoadStructMetadataDirect(const wchar_t* targetStructName,
        std::vector<std::string>& outNames,
        std::vector<DWORD>& outOffsets,
        std::vector<DWORD>& outSizes,
        std::vector<std::string>& outTypeNames)
    {
        if (!EnsureDbgHelpInitialized()) return false;

        std::string targetNameAnsi = DbgHelpWideToUtf8(targetStructName);

        DbgHelpStructContext ctx = { g_hDbgHelpGlobalProcess, g_DbgHelpGlobalModuleBase, targetStructName, &outNames, &outOffsets, &outSizes, &outTypeNames };
        SymEnumTypesW(g_hDbgHelpGlobalProcess, g_DbgHelpGlobalModuleBase, DbgHelpStructTypesCallback, &ctx);

        return !outNames.empty();
    }





} 
