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

#include <atlbase.h>
#include <tlhelp32.h>

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

            if (SUCCEEDED(m_coInitResult)) {
                LivePT::Log("COM Infrastructure automatically initialized.");
            }
        }

        ~LptRuntimeLifetimeManager() {

            if (pDTE) {
                pDTE->Release();
                pDTE = nullptr;
                LivePT::Log("EnvDTE Interface released.");
            }

            if (m_coInitResult == S_OK || m_coInitResult == S_FALSE) {
                CoUninitialize();
                LivePT::Log("COM Infrastructure automatically uninitialized.");
            }
        }
    };

    static LptRuntimeLifetimeManager g_runtimeLifetimeManager;

    void ProcessEdit()
    {
#if LivePT_EditMode

#if LivePT_WindowManagement
        GetWindowManager().Tick(); // [2]
#endif

#if LivePT_WheelEditMode
        // 1. Всегда обрабатываем координаты мыши и драг
        Update(); // [2]
#endif

        HWND hForeground = ::GetForegroundWindow(); // [2]
        bool shouldProcessEditor = true; // [2]

        if (hForeground != NULL) { // [2]
            DWORD activeProcessId = 0; // [2]
            ::GetWindowThreadProcessId(hForeground, &activeProcessId); // [2]

            DWORD targetStudioPid = GetStudioProcessId(); // [2]

            // Если ушли из Студии (например, в игру) — глушим опрос, чтобы освободить CPU
            if (activeProcessId != targetStudioPid) { // [2]
                shouldProcessEditor = false; // [2]
            }
        }
        else {
            shouldProcessEditor = false; // [2]
        }

        // БЛОКИРОВКА НА ВРЕМЯ ДРАГА ПОЛНОСТЬЮ ВЫРЕЗАНА!
        // Вызываем фоновый инспектор vsEditor() параллельно с драгом, 
        // пока фокус гарантированно внутри процесса Студии.
        //if (shouldProcessEditor) {
            vsEditor(); // [2]
        //}

#endif
    }



}

#else

#define eval(...) __VA_ARGS__ 

#endif