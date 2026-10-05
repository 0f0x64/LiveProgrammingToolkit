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

    struct LptEnumContext {
        HANDLE hProcess;
        DWORD64 moduleBase;
        const wchar_t* targetEnumName;
        std::vector<std::string>* outNames;
        std::vector<int>* outValues;
    };

    static BOOL CALLBACK LptEnumTypesCallback(PSYMBOL_INFOW pSymInfo, ULONG SymbolSize, PVOID UserContext) {
        if (!pSymInfo || !UserContext) return TRUE;

        LptEnumContext* ctx = reinterpret_cast<LptEnumContext*>(UserContext);
        std::wstring pSymNameW(pSymInfo->Name);

        if (pSymNameW.find(L"lambda") != std::wstring::npos || pSymNameW.find(L"anonymous-namespace") != std::wstring::npos) {
            return TRUE;
        }

        if (pSymNameW.find(ctx->targetEnumName) != std::wstring::npos) {
            DWORD childrenCount = 0;
            if (SymGetTypeInfo(ctx->hProcess, ctx->moduleBase, pSymInfo->TypeIndex, TI_GET_CHILDRENCOUNT, &childrenCount) && childrenCount > 0) {

                ULONG mallocSize = sizeof(TI_FINDCHILDREN_PARAMS) + (childrenCount * sizeof(ULONG));
                TI_FINDCHILDREN_PARAMS* pChildren = (TI_FINDCHILDREN_PARAMS*)malloc(mallocSize);
                if (pChildren) {
                    memset(pChildren, 0, mallocSize);
                    pChildren->Count = childrenCount;

                    if (SymGetTypeInfo(ctx->hProcess, ctx->moduleBase, pSymInfo->TypeIndex, TI_FINDCHILDREN, pChildren)) {
                        for (DWORD i = 0; i < pChildren->Count; i++) {
                            ULONG childIndex = pChildren->ChildId[i];

                            wchar_t* pChildName = nullptr;
                            SymGetTypeInfo(ctx->hProcess, ctx->moduleBase, childIndex, TI_GET_SYMNAME, &pChildName);

                            VARIANT varValue;
                            VariantInit(&varValue);
                            SymGetTypeInfo(ctx->hProcess, ctx->moduleBase, childIndex, TI_GET_VALUE, &varValue);

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

                            if (pChildName) {
                                ctx->outNames->push_back(DbgHelpWideToUtf8(pChildName));
                                LocalFree(pChildName);
                            }
                            ctx->outValues->push_back(extractedValue);
                            VariantClear(&varValue);
                        }
                    }
                    free(pChildren);
                }
            }
            return FALSE;
        }
        return TRUE;
    }
    inline bool LoadEnumMetadataDirect(const wchar_t* targetEnumName, std::vector<std::string>& outNames, std::vector<int>& outValues) {
        if (!EnsureDbgHelpInitialized()) return false;

        HANDLE hProcess = g_hDbgHelpGlobalProcess;
        DWORD64 moduleBase = g_DbgHelpGlobalModuleBase;

        std::wstring cleanEnumName(targetEnumName);
        size_t lastColon = cleanEnumName.rfind(L"::");
        if (lastColon != std::wstring::npos) {
            cleanEnumName = cleanEnumName.substr(lastColon + 2);
        }

        cleanEnumName.erase(0, cleanEnumName.find_first_not_of(L" \t\r\n"));
        cleanEnumName.erase(cleanEnumName.find_last_not_of(L" \t\r\n") + 1);

        LptEnumContext ctx = { hProcess, moduleBase, cleanEnumName.c_str(), &outNames, &outValues };
        SymEnumTypesW(hProcess, moduleBase, LptEnumTypesCallback, &ctx);

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
                            }

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

        DbgHelpStructContext ctx = { g_hDbgHelpGlobalProcess, g_DbgHelpGlobalModuleBase, targetStructName, &outNames, &outOffsets, &outSizes, &outTypeNames };
        SymEnumTypesW(g_hDbgHelpGlobalProcess, g_DbgHelpGlobalModuleBase, DbgHelpStructTypesCallback, &ctx);

        return !outNames.empty();
    }

} // namespace LivePT
