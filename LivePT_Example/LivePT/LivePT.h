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
    inline std::atomic<bool> g_IsLptPdbWarmupCompleted{ false };

    

    inline void WarmupAllDatabaseParams() {
        static bool isWarmedUp = false;
        if (isWarmedUp) return;

        auto& params = LivePT::getParamDesc();
        const size_t totalParams = params.size();
        if (totalParams == 0) return;

        isWarmedUp = true;

        // Запускаем тяжелый разбор PDB в фоновом потоке
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

                // Пропускаем то, что уже загружено или не инициализировано
                if (p.enumInfo.isEnum || p.structInfo.isStruct || !p.value.has_value()) {
                    continue;
                }

                std::type_index currentTypeInfo = p.value.type();

                // Примитивы не требуют разбора структуры
                if (currentTypeInfo == typeFloat || currentTypeInfo == typeInt ||
                    currentTypeInfo == typeBool || currentTypeInfo == typeDouble ||
                    currentTypeInfo == typeChar || currentTypeInfo == typeUChar) {
                    continue;
                }

                // Быстрый кэш, если структура такого типа уже разбиралась
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

                // Этот вызов DbgHelp теперь не фризит UI-поток отрисовки кадра!
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
            g_IsLptPdbWarmupCompleted = true;
            Log("[LivePT Async Warmup] Background PDB parsing completed. Library is fully ready.");
            });

        // Отвязываем поток, чтобы он работал асинхронно
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

    inline void ProcessEdit()
    {
        AlignDatabaseFastFromDisk();
        Warmup();

#if LivePT_WindowManagement
        GetWindowManager().Tick();
#endif

        if (!g_IsLptPdbWarmupCompleted) return;

#if LivePT_Mouse
        // 1. Выгружаем обычные мышиные буферы одиночных чисел
        FlushPendingWritesToVS();

        // Флаг того, зажата ли сейчас основная управляющая кнопка мыши (ЛКМ)
        bool isTriggerButtonDown = (GetAsyncKeyState(LivePT_TriggerButton) & 0x8000) != 0;
        static bool s_WidgetTransactionActive = false;

        // === ИНКАПСУЛЯЦИЯ UNDO В БИБЛИОТЕКЕ ===
        // Если виджет пушит изменения, и транзакция еще не открыта — библиотека открывает её сама!
        if (g_widgetBuffer.hasChanges && !s_WidgetTransactionActive) {
            StartUndoTransaction(L"LivePT Widget Realtime Update");
            s_WidgetTransactionActive = true;
        }

        // 2. ВЫГРУЖАЕМ БУФЕР ВИДЖЕТОВ: Роботизированная замена по живым EnvDTE-якорям
        if (g_widgetBuffer.hasChanges && !g_widgetBuffer.codeText.empty() && pWidgetEndEditPoint && pWidgetStartEditPoint) {

            VARIANT vtActiveDocLocal; VariantInit(&vtActiveDocLocal);
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDocLocal, pDTE, L"ActiveDocument", 0)) && vtActiveDocLocal.pdispVal) {

                VARIANT vtCloseLine, vtCloseOffset;
                VariantInit(&vtCloseLine); VariantInit(&vtCloseOffset);

                AutoWrap(DISPATCH_PROPERTYGET, &vtCloseLine, pWidgetEndEditPoint, L"Line", 0);
                AutoWrap(DISPATCH_PROPERTYGET, &vtCloseOffset, pWidgetEndEditPoint, L"DisplayColumn", 0);

                long actualCloseLine = (vtCloseLine.vt == VT_I4) ? vtCloseLine.lVal : g_widgetBuffer.line;
                long actualCloseOffset = (vtCloseOffset.vt == VT_I4) ? vtCloseOffset.lVal : g_dragState.dragStartCol;

                if (g_widgetBuffer.line == actualCloseLine) {
                    // Однострочная быстрая замена
                    long newCursorPhysicalCol = g_dragState.dragStartCol + static_cast<long>(g_widgetBuffer.codeText.length());
                    ReplaceTextInActiveVS(g_widgetBuffer.line, g_dragState.dragStartCol, actualCloseOffset, g_widgetBuffer.codeText, newCursorPhysicalCol);
                }
                else {
                    // Многострочный исправленный мемори-буфер (Защита открывающей скобки)
                    CComVariant pSafeStartPoint;
                    HRESULT hr = AutoWrap(DISPATCH_METHOD, &pSafeStartPoint, pWidgetStartEditPoint, L"CreateEditPoint", 0);

                    if (SUCCEEDED(hr) && pSafeStartPoint.vt == VT_DISPATCH && pSafeStartPoint.pdispVal) {
                        VARIANT vtStartLineNum, vtStartCharOffset;
                        VariantInit(&vtStartLineNum); VariantInit(&vtStartCharOffset);

                        AutoWrap(DISPATCH_PROPERTYGET, &vtStartLineNum, pWidgetStartEditPoint, L"Line", 0);
                        AutoWrap(DISPATCH_PROPERTYGET, &vtStartCharOffset, pWidgetStartEditPoint, L"LineCharOffset", 0);

                        long startL = vtStartLineNum.lVal;
                        long startO = vtStartCharOffset.lVal + 1; // Хирургический сдвиг внутрь скобки {

                        AutoWrap(DISPATCH_METHOD, NULL, pSafeStartPoint.pdispVal, L"MoveToLineAndOffset", 2, CComVariant(startL), CComVariant(startO));

                        std::wstring wText(g_widgetBuffer.codeText.begin(), g_widgetBuffer.codeText.end());
                        CComBSTR bstrText(wText.c_str());

                        AutoWrap(DISPATCH_METHOD, NULL, pSafeStartPoint.pdispVal, L"ReplaceText", 3,
                            CComVariant(pWidgetEndEditPoint),
                            CComVariant(bstrText),
                            CComVariant(1L));

                        VariantClear(&vtStartCharOffset); VariantClear(&vtStartLineNum);
                    }

                    // Синхронизируем текстовый курсор, чтобы избежать прыжков
                    VARIANT vtSelection; VariantInit(&vtSelection);
                    if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, vtActiveDocLocal.pdispVal, L"Selection", 0)) && vtSelection.pdispVal) {
                        VARIANT vtNewCursorLine, vtNewCursorOffset;
                        VariantInit(&vtNewCursorLine); VariantInit(&vtNewCursorOffset);

                        AutoWrap(DISPATCH_PROPERTYGET, &vtNewCursorLine, pWidgetEndEditPoint, L"Line", 0);
                        AutoWrap(DISPATCH_PROPERTYGET, &vtNewCursorOffset, pWidgetEndEditPoint, L"LineCharOffset", 0);

                        long finalLine = (vtNewCursorLine.vt == VT_I4) ? vtNewCursorLine.lVal : g_widgetBuffer.line;
                        long finalOffset = (vtNewCursorOffset.vt == VT_I4) ? vtNewCursorOffset.lVal : 1;

                        AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"MoveToLineAndOffset", 3,
                            CComVariant(finalLine),
                            CComVariant(finalOffset),
                            CComVariant(0L));

                        VariantClear(&vtNewCursorOffset); VariantClear(&vtNewCursorLine);
                        VariantClear(&vtSelection);
                    }
                }

                VariantClear(&vtCloseOffset); VariantClear(&vtCloseLine);
                VariantClear(&vtActiveDocLocal);
            }
            g_widgetBuffer.hasChanges = false;
        }

        // === АВТОМАТИЧЕСКОЕ ЗАКРЫТИЕ ТРАНЗАКЦИИ БИБЛИОТЕКОЙ ===
        // Как только пользователь отпустил кнопку мыши после взаимодействия с виджетом,
        // библиотека сама закрывает транзакцию и атомарно сохраняет документ.
        if (!isTriggerButtonDown && s_WidgetTransactionActive) {
            EndUndoTransaction();
            SaveActiveDocument();
            s_WidgetTransactionActive = false;
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