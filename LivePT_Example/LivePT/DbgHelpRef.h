#pragma once

#include <windows.h>
#include <iostream>
#include <vector>
#include <string>

#pragma comment(lib, "dbghelp.lib")
#include <dbghelp.h>

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
    SymSetOptions(SYMOPT_DEBUG | SYMOPT_DEFERRED_LOADS | SYMOPT_ALLOW_ZERO_ADDRESS);

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
