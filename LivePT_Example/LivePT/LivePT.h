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

        WidgetBufferUpdate();

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