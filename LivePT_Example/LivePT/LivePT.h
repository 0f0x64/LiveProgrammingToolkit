// User space settings

#define LivePT_Mouse true // true for mouse drag, switch, enums with context menu
#define LivePT_WindowManagement true //auto split screen on single monitor
#define LivePT_AppToSecondaryDisplay false //auto move your app to second monitor
#define LivePT_TriggerButton VK_LBUTTON  // you can use VK_MBUTTON for alternative

// -------------------

#if LivePT_EditMode

    #include <vector>
    #include <string>
    #include <unordered_map>
    #include <map>
    #include <variant>
    #include <cstdlib>
    #include <algorithm>
    #include <sstream>
    #include <type_traits>
    #include <cctype>
    #include <string_view>
    #include <array>
    #include <charconv>
    #include <limits>
    #include <any>
    #include <fstream>
    #include <source_location>
    #include <atlbase.h>
    #include <tlhelp32.h>
    #include <functional>
    #include <unordered_set>
    #include <typeindex>
    #include <chrono>
    #include <thread>

    #pragma comment(lib, "dbghelp.lib")
    #include <dbghelp.h>


namespace LivePT {

    inline std::wstring GetConfiguredMacroName() {
        static const std::wstring cachedMacroName = []() {
            constexpr auto location = std::source_location::current();
            std::string selfPath = location.file_name();

            std::ifstream file(selfPath);
            if (!file.is_open()) {
                return std::wstring(L"eval");
            }

            std::string line;
            std::string marker = "//EVAL_NAME_MARKER - DO'NT TOUCH THIS LINE";

            while (std::getline(file, line)) {

                if (line.find(marker) != std::string::npos) {

                    while (std::getline(file, line)) {

                        line.erase(0, line.find_first_not_of(" \t\r\n"));
                        if (line.empty()) continue; 

                        if (line.rfind("#define", 0) == 0) {
                            line.erase(0, 7); 
                            line.erase(0, line.find_first_not_of(" \t\r\n"));

                            size_t endPos = line.find_first_of(" \t(");
                            if (endPos != std::string::npos) {
                                std::string name = line.substr(0, endPos);
                                return std::wstring(name.begin(), name.end());
                            }
                        }
                    }
                    break;
                }
            }
            return std::wstring(L"eval");
            }();

        return cachedMacroName;
    }

    inline void Log(const std::string& text, bool nl = true) {
        OutputDebugStringA(text.c_str());
        if (nl) OutputDebugStringA("\n");
    }
}

    #include "DbgHelpRef.h"
    #include "eval.h"
    #include "uiCallBackBridge.h"
    #include "vsEditor.h"
    #include "consistency.h"

    #if LivePT_Mouse
        #include "mouse.h"
        #include "drugLogic.h"
    #endif

    #if LivePT_WindowManagement
        #include "windowManagement.h"
    #endif

namespace LivePT {

    class LptRuntimeLifetimeManager {
    private:
        HRESULT m_coInitResult = E_FAIL;

    public:
        LptRuntimeLifetimeManager() {

            m_coInitResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

            if (FAILED(m_coInitResult)) {
                LivePT::Log("COM Infrastructure error");
            }
        }

        ~LptRuntimeLifetimeManager() {

            if (pDTE) {
                pDTE->Release();
                pDTE = nullptr;
            }

            if (m_coInitResult == S_OK || m_coInitResult == S_FALSE) {
                CoUninitialize();
            }
        }
    };

    static LptRuntimeLifetimeManager g_runtimeLifetimeManager;

    
    inline void WarmupAllDatabaseParams() {
        static bool isWarmedUp = false;
        if (isWarmedUp) return;

        auto& params = LivePT::getParamDesc();
        const size_t totalParams = params.size();
        if (totalParams == 0) return;

        isWarmedUp = true;

        std::thread warmupThread([totalParams]() {

            auto& localParams = LivePT::getParamDesc();

            std::unordered_map<std::type_index, decltype(localParams[0].structInfo.members)> structMetadataCache;
            structMetadataCache.reserve(8);

            static const std::type_index typeFloat = typeid(float);
            static const std::type_index typeInt = typeid(int);
            static const std::type_index typeBool = typeid(bool);
            static const std::type_index typeDouble = typeid(double);
            static const std::type_index typeChar = typeid(char);
            static const std::type_index typeUChar = typeid(unsigned char);

            for (size_t i = 0; i < totalParams; ++i) {
                auto& p = localParams[i];

                if (p.enumInfo.isEnum || p.structInfo.isStruct || !p.value.has_value()) {
                    continue;
                }

                std::type_index currentTypeInfo = p.value.type();

                if (currentTypeInfo == typeFloat || currentTypeInfo == typeInt ||
                    currentTypeInfo == typeBool || currentTypeInfo == typeDouble ||
                    currentTypeInfo == typeChar || currentTypeInfo == typeUChar) {
                    continue;
                }

                auto it = structMetadataCache.find(currentTypeInfo);
                if (it != structMetadataCache.end()) {
                    p.structInfo.members = it->second;
                    p.structInfo.isStruct = true;
                    continue;
                }

                std::string typeNameAnsi = currentTypeInfo.name();
                if (typeNameAnsi.empty()) continue;

                const char* typePtr = typeNameAnsi.c_str();
                if (strncmp(typePtr, "struct ", 7) == 0) typePtr += 7;
                else if (strncmp(typePtr, "class ", 6) == 0) typePtr += 6;

                size_t len = strlen(typePtr);
                std::wstring wTypeName(len, L'\0');
                MultiByteToWideChar(CP_ACP, 0, typePtr, static_cast<int>(len), wTypeName.data(), static_cast<int>(len));

                std::vector<std::string> fNames;
                std::vector<DWORD> fOffsets;
                std::vector<DWORD> fSizes;
                std::vector<std::string> fTypes;

                // Тяжелый вызов DbgHelp выполняется в фоне и не фризит UI-поток отрисовки кадра!
                if (LoadStructMetadataDirect(wTypeName.c_str(), fNames, fOffsets, fSizes, fTypes)) {
                    decltype(p.structInfo.members) loadedMembers;
                    const size_t fieldsCount = fOffsets.size();
                    loadedMembers.reserve(fieldsCount);

                    for (size_t k = 0; k < fieldsCount; ++k) {
                        loadedMembers.push_back({ std::move(fNames[k]), fOffsets[k], fSizes[k], std::move(fTypes[k]) });
                    }

                    p.structInfo.members = loadedMembers;
                    p.structInfo.isStruct = true;

                    structMetadataCache.emplace(currentTypeInfo, std::move(loadedMembers));
                }
            }
            Log("[LivePT Async Warmup] Background PDB parsing completed. Library is fully ready.");
            });

        // Отвязываем поток, чтобы он работал независимо и завершился сам
        warmupThread.detach();
    }

    void Warmup()
    {
        static bool isFirstFrame = true;
        if (isFirstFrame) {
            auto startTime = std::chrono::high_resolution_clock::now();

            // Сам вызов чистой функции прогрева
            LivePT::WarmupAllDatabaseParams();

            auto endTime = std::chrono::high_resolution_clock::now();
            auto durationMicro = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            double durationMilli = durationMicro / 1000.0;

            LivePT::Log("[LivePT External Profile] Warmup execution time: " +
                std::to_string(durationMilli) + " ms (" + std::to_string(durationMicro) + " us).");

            isFirstFrame = false;
        }
    }

    inline void LogAllEvalCoordinates() {
        // Получаем доступ к нашей глобальной мапе, собранной на этапе Pre-Main
        auto& fileCompileTree = GetFileCompileTree();

        Log("=================== [LivePT Map Dump Start] ===================");

        int totalFiles = 0;
        int totalEvals = 0;

        // 1. Обходим файлы (порядок файлов случайный из-за unordered_map)
        for (const auto& [filePath, fileMap] : fileCompileTree) {
            totalFiles++;

            Log("File: " + filePath);

            // 2. Обходим внутренний map (гарантирует проход строго сверху вниз)
            for (const auto& [staticKey, paramId] : fileMap) {
                totalEvals++;

                // Формируем красивую строку лога с координатами из мапы и Runtime ID
                std::string logLine = "  -> [eval] Line: " + std::to_string(staticKey.line) +
                    ", Column: " + std::to_string(staticKey.column) +
                    " | ParamDescID: " + std::to_string(paramId);

                Log(logLine);
            }
        }

        Log("Total unique files: " + std::to_string(totalFiles) + ", Total eval macros: " + std::to_string(totalEvals));
        Log("===================  [LivePT Map Dump End]  ===================");
    }

    inline void LogAllEvalCoordinates2() {
        auto& fileCompileTree = GetFileCompileTree();
        auto& params = LivePT::getParamDesc(); // Достаем наш рабочий вектор

        Log("=================== [LivePT Map Dump Start] ===================");

        int totalFiles = 0;
        int totalEvals = 0;

        for (const auto& [filePath, fileMap] : fileCompileTree) {
            totalFiles++;
            Log("File: " + filePath);

            for (const auto& [staticKey, paramId] : fileMap) {
                if (paramId < 0 || paramId >= static_cast<int>(params.size())) continue;

                totalEvals++;

                // БЕРЕМ ДАННЫЕ ИЗ ВЕКТОРА ПАРАМЕТРОВ, ГДЕ СРАБОТАЛА КОРРЕКЦИЯ!
                const auto& p = params[paramId];

                std::string logLine = "  -> [eval] Line: " + std::to_string(p.line) +
                    ", Column: " + std::to_string(p.column) +
                    " (Raw key was: " + std::to_string(staticKey.line) + ":" + std::to_string(staticKey.column) + ")" +
                    " | ParamDescID: " + std::to_string(paramId) +
                    " | Status: " + (p.loaded ? "ALIGNED" : "RAW");

                Log(logLine);
            }
        }

        Log("Total unique files: " + std::to_string(totalFiles) + ", Total eval macros: " + std::to_string(totalEvals));
        Log("===================  [LivePT Map Dump End]  ===================");
    }

    void ProcessEdit()
    {
        AlignDatabaseFastFromDisk();

        Warmup();

#if LivePT_WindowManagement
        GetWindowManager().Tick();
#endif

#if LivePT_Mouse
        // 1. Сначала выгружаем обычные мышиные буферы
        FlushPendingWritesToVS();

        if (g_widgetBuffer.hasChanges && !g_widgetBuffer.codeText.empty()) {

            VARIANT vtActiveDoc; VariantInit(&vtActiveDoc);
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0)) && vtActiveDoc.pdispVal) {
                std::wstring fullFileText = DownloadDocumentText(vtActiveDoc.pdispVal);

                // Находим физическое смещение начала целевой строки g_widgetBuffer.line
                size_t lineOffset = 0; long currentLineIdx = 1;
                while (currentLineIdx < g_widgetBuffer.line && lineOffset < fullFileText.length()) {
                    size_t nextNL = fullFileText.find(L'\n', lineOffset);
                    if (nextNL != std::wstring::npos) { lineOffset = nextNL + 1; currentLineIdx++; }
                    else break;
                }

                std::string targetTypeNameA = g_dragState.oldValueStr;
                std::wstring targetTypeNameW(targetTypeNameA.begin(), targetTypeNameA.end());

                // Ищем ключевое слово типа на нашей строке
                size_t typeKeywordIdx = fullFileText.find(targetTypeNameW, lineOffset);

                if (typeKeywordIdx != std::wstring::npos) {
                    size_t openBracketIdx = std::wstring::npos;
                    wchar_t opChar = L'\0'; wchar_t clChar = L'\0';

                    for (size_t k = typeKeywordIdx + targetTypeNameW.length(); k < fullFileText.length(); ++k) {
                        if (fullFileText[k] == L'{') { opChar = L'{'; clChar = L'}'; openBracketIdx = k; break; }
                        if (fullFileText[k] == L'(') { opChar = L'('; clChar = L')'; openBracketIdx = k; break; }
                        if (!iswspace(fullFileText[k])) break;
                    }

                    if (openBracketIdx != std::wstring::npos) {
                        size_t closeBracketIdx = std::wstring::npos;
                        int bracketCount = 1;
                        for (size_t k = openBracketIdx + 1; k < fullFileText.length(); ++k) {
                            if (fullFileText[k] == opChar) bracketCount++;
                            if (fullFileText[k] == clChar) {
                                bracketCount--;
                                if (bracketCount == 0) { closeBracketIdx = k; break; }
                            }
                        }

                        if (closeBracketIdx != std::wstring::npos) {
                            // Вычисляем физическую строку закрывающей скобки
                            long closeLine = g_widgetBuffer.line;
                            size_t closeLineOffset = lineOffset;
                            size_t scanNL = fullFileText.find(L'\n', lineOffset);
                            while (scanNL != std::wstring::npos && scanNL < closeBracketIdx) {
                                closeLine++;
                                closeLineOffset = scanNL + 1;
                                scanNL = fullFileText.find(L'\n', closeLineOffset);
                            }

                            // Вычисляем визуальные колонки строго ВНУТРИ скобок (заменяем только потроха!)
                            size_t nextNL = fullFileText.find(L'\n', lineOffset);
                            if (nextNL == std::wstring::npos) nextNL = fullFileText.length();
                            std::wstring startLineText = fullFileText.substr(lineOffset, nextNL - lineOffset);

                            size_t endNL = fullFileText.find(L'\n', closeLineOffset);
                            if (endNL == std::wstring::npos) endNL = fullFileText.length();
                            std::wstring endLineText = fullFileText.substr(closeLineOffset, endNL - closeLineOffset);

                            // Заменяем строго диапазон ПЕРЕД первым символом аргумента и ДО закрывающей скобки
                            long visualStartCol = GetVisualColumn(startLineText, openBracketIdx + 1 - lineOffset);
                            long visualEndCol = GetVisualColumn(endLineText, closeBracketIdx - closeLineOffset);

                            StartUndoTransaction(L"LivePT Widget Realtime Update");

                            if (g_widgetBuffer.line == closeLine) {
                                long newCursorPhysicalCol = visualStartCol + static_cast<long>(g_widgetBuffer.codeText.length());
                                ReplaceTextInActiveVS(g_widgetBuffer.line, visualStartCol, visualEndCol, g_widgetBuffer.codeText, newCursorPhysicalCol);
                            }
                            else {
                                // Безопасное DTE-удаление многострочного блока аргументов
                                CComVariant vtSelection;
                                if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, vtActiveDoc.pdispVal, L"Selection", 0)) && vtSelection.pdispVal) {
                                    AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"MoveToLineAndOffset", 3, CComVariant(g_widgetBuffer.line), CComVariant(visualStartCol), CComVariant(0L));
                                    AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"MoveToLineAndOffset", 3, CComVariant(closeLine), CComVariant(visualEndCol), CComVariant(1L)); // 1L = Extend/Выделить

                                    std::wstring wText(g_widgetBuffer.codeText.begin(), g_widgetBuffer.codeText.end());
                                    CComBSTR bstrText(wText.c_str());
                                    AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"Insert", 1, CComVariant(bstrText));
                                }
                            }

                            EndUndoTransaction();
                            g_dragState.currentTextLength = g_widgetBuffer.codeText.length();
                        }
                    }
                }
                VariantClear(&vtActiveDoc);
            }
            g_widgetBuffer.hasChanges = false;
        }

        Update();
#endif

        vsEditor();
    }


}

//EVAL_NAME_MARKER - DO'NT TOUCH THIS LINE
#define eval(...) \
    LivePT::LazyTypeDetector< \
        std::decay_t<decltype(__VA_ARGS__)>, \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(__VA_ARGS__)


#else

#define eval(...) __VA_ARGS__

namespace LivePT {
    void ProcessEdit() {}
}

#endif