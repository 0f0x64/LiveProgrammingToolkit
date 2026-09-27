// User space settings

#define LivePT_WheelEditMode true // true for mouse drag, switch, enums with context menu
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
                // Нашли маркер в собственном тексте
                if (line.find(marker) != std::string::npos) {
                    // ИСПРАВЛЕНО: Читаем файл дальше в цикле, пока не найдем строку с "#define"
                    while (std::getline(file, line)) {
                        // Чистим пробелы и табы по краям строки
                        line.erase(0, line.find_first_not_of(" \t\r\n"));
                        if (line.empty()) continue; // Пропускаем пустые строки

                        // Если это строка макроса
                        if (line.rfind("#define", 0) == 0) {
                            line.erase(0, 7); // Стираем "#define"
                            line.erase(0, line.find_first_not_of(" \t\r\n"));

                            // Вырезаем токен до первой скобки, пробела или таба
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
    #include "vsEditor.h"

    #if LivePT_WheelEditMode
        #include "liveWheelEdit.h"
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

    void ProcessEdit()
    {

        #if LivePT_WindowManagement
            GetWindowManager().Tick();
        #endif

        #if LivePT_WheelEditMode
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