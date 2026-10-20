// User space settings

#define LivePT_Mouse true // true for mouse drag, switch, enums with context menu
#define LivePT_WindowManagement true //auto split screen on single monitor
#define LivePT_AppToSecondaryDisplay false //auto move your app to second monitor
#define LivePT_TriggerButton VK_LBUTTON  // you can use VK_MBUTTON for alternative
#define LivePT_ShowEditPointsInLog true  // you can use VK_MBUTTON for alternative

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
        #include "dragOverlay.h"
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

    inline void ProcessEdit()
    {
        AlignDatabaseFastFromDisk();
        Warmup();

#if LivePT_WindowManagement
        GetWindowManager().Tick();
#endif

        if (!g_IsLptPdbWarmupCompleted) return;

        static DWORD lastUpdate = 0;

        DWORD currentTick = GetTickCount();
        if (currentTick - lastUpdate < 16) { // ~60 FPS update limit
            return;
        }
        lastUpdate = currentTick;

#if LivePT_Mouse
        // 1. Выгружаем обычные мышиные буферы одиночных чисел
        InitPassiveOverlay();
        g_OverlayShow = g_dragState.isCursorHidden ? true : false;
        UpdateOverlayPosition(g_dragState.lockMousePos.x, g_dragState.lockMousePos.y);

        FlushPendingWritesToVS();

        WidgetBufferUpdate();

        Update();
#endif

        vsEditor();
    }

}

#define eval(...) \
    LivePT::LazyTypeDetector< \
        std::decay_t<decltype(__VA_ARGS__)>, \
        LivePT::FixedString{"eval"}, \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(__VA_ARGS__)

#define LPT_LIVE_FUNCTION(ReturnType, FuncName, StructType, VarName) \
    ReturnType FuncName##_Live(StructType VarName, int runtimeId, std::source_location location); \
    \
    template <typename T = StructType> \
    inline ReturnType FuncName(T&& raw_r, const char* file = __builtin_FILE(), int line = __builtin_LINE(), int col = __builtin_COLUMN(), std::source_location loc = std::source_location::current()) { \
        std::string absPath = LivePT::NormalizePath(file); \
        int rId = LivePT::RegisterEvalPreMain(absPath.c_str(), #FuncName, std::any{ raw_r }, line, col, LivePT::EnumTypeDesc{ false }); \
        \
        /* Инициализируем парсер строк при первом вызове функции */ \
        if (rId >= 0 && rId < static_cast<int>(LivePT::paramDesc.size()) && !LivePT::paramDesc[rId].loaded) { \
            LivePT::paramDesc[rId].value = std::any{ static_cast<StructType>(raw_r) }; \
            LivePT::paramDesc[rId].stringUpdater = [rId](std::any& targetAny, const std::string& textValue) { \
                std::string innerArgs; \
                size_t openBrace = textValue.find('{'); \
                size_t closeBrace = textValue.rfind('}'); \
                if (openBrace != std::string::npos && closeBrace != std::string::npos && closeBrace > openBrace) { \
                    std::string rawInner = textValue.substr(openBrace + 1, closeBrace - openBrace - 1); \
                    innerArgs.reserve(rawInner.length()); \
                    bool inComment = false; \
                    for (size_t i = 0; i < rawInner.length(); ++i) { \
                        char c = rawInner[i]; char nextC = (i + 1 < rawInner.length()) ? rawInner[i + 1] : '\0'; \
                        if (inComment) { if (c == '\n' || c == '\r') inComment = false; continue; } \
                        if ((c == '/' && nextC == '/') || c == '#') { inComment = true; if (c == '/') i++; continue; } \
                        innerArgs += c; \
                    } \
                } else { \
                    if (!LivePT::ExtractAndStripComments(textValue, innerArgs)) return; \
                } \
                StructType* pStructInstance = std::any_cast<StructType>(&targetAny); \
                if (!pStructInstance) return; \
                \
                /* [НОВАЯ АРХИТЕКТУРА]: Сбрасываем карту смещений перед десериализацией */ \
                LivePT::paramDesc[rId].parsedFieldsOffsets.clear(); \
                LivePT::WriteStructBytes(reinterpret_cast<char*>(pStructInstance), typeid(StructType).name(), innerArgs, 0, rId, sizeof(StructType)); \
            }; \
            LivePT::paramDesc[rId].loaded = true; \
        }        \
        /* Пробрасываем измененные данные из базы LivePT в локальную переменную */ \
        StructType live_r = std::forward<T>(raw_r); \
        if (rId >= 0 && rId < static_cast<int>(LivePT::paramDesc.size())) { \
            std::string actualKey = LivePT::NormalizePath(file) + ":" + std::to_string(LivePT::paramDesc[rId].counterID); \
            int real_id = LivePT::getID(actualKey); \
            if (real_id >= 0 && real_id < static_cast<int>(LivePT::paramDesc.size()) && LivePT::paramDesc[real_id].loaded) { \
                if (auto pCachedVal = std::any_cast<StructType>(&LivePT::paramDesc[real_id].value)) { \
                    char* targetBase = reinterpret_cast<char*>(&live_r); \
                    const char* cacheBase = reinterpret_cast<const char*>(pCachedVal); \
                    \
                    /* [НОВАЯ АРХИТЕКТУРНЫЙ ПОДХОД]: Наложение байт по PDB-смещениям */ \
                    const auto& offsets = LivePT::paramDesc[real_id].parsedFieldsOffsets; \
                    size_t totalSz = sizeof(StructType); \
                    for (DWORD offset : offsets) { \
                        if (offset < totalSz) { \
                            targetBase[offset] = cacheBase[offset]; \
                        } \
                    } \
                } \
            } \
        } \
        return FuncName##_Live(std::move(live_r), rId, loc); \
    } \
    ReturnType FuncName##_Live(StructType VarName, int runtimeId, std::source_location location)




#else

#define eval(...) __VA_ARGS__

#define LPT_LIVE_FUNCTION(ReturnType, FuncName, StructType, VarName) \
        inline ReturnType FuncName(StructType VarName, int runtimeId = 0, std::source_location location = std::source_location::current())

#define LPT_REGISTER_TYPE(TypeNameString, CallbackFunction)
#define LPT_REGISTER_DRAG(TypeNameString, CallbackFunction)

namespace LivePT {
    inline void ProcessEdit() {}
    inline bool IsCursorInsideFunctionCall(const std::source_location& location) { return false; };
    inline void OpenFileAndMoveCursorToLocation(const std::source_location& location) {};
}


#endif