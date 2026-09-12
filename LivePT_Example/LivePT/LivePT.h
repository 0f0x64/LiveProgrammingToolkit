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

#include <atlbase.h>
#include <tlhelp32.h>

namespace LivePT {

    inline void Log(const std::string& text, bool nl = true) {
        OutputDebugStringA(text.c_str());
        if (nl) OutputDebugStringA("\n");
    }
}

    #include "eval.h"
    #include "vsEditor.h"
    #include "liveWheelEdit.h"

namespace LivePT {

    class LptRuntimeLifetimeManager {
    private:
        HRESULT m_coInitResult = E_FAIL;

    public:
        LptRuntimeLifetimeManager() {

            m_coInitResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

            if (SUCCEEDED(m_coInitResult)) {
                LivePT::Log("[LPT] COM Infrastructure automatically initialized.");
            }
        }

        ~LptRuntimeLifetimeManager() {

            if (pDTE) {
                pDTE->Release();
                pDTE = nullptr;
                LivePT::Log("[LPT] EnvDTE Interface released.");
            }

            if (m_coInitResult == S_OK || m_coInitResult == S_FALSE) {
                CoUninitialize();
                LivePT::Log("[LPT] COM Infrastructure automatically uninitialized.");
            }
        }
    };

    static LptRuntimeLifetimeManager g_runtimeLifetimeManager;

    void ProcessEdit()
    {
    #if LivePT_EditMode

        vsEditor();

        #if LivePT_WheelEditMode
        Update();
        #endif

    #endif
    }

} 

#else

    #define eval(val) val

#endif