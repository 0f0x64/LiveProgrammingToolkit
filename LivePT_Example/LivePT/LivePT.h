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

#include <atlbase.h>
#include <tlhelp32.h>

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
        GetWindowManager().Tick();
#endif

#if LivePT_WheelEditMode
        // 1. Сначала всегда обрабатываем мышь, чтобы выставить флаг драга
        Update();
#endif

        HWND hForeground = ::GetForegroundWindow();
        bool shouldProcessEditor = true;

#if LivePT_WheelEditMode
        // Если идет драг или открыто окно-щит — полностью пропускаем фоновый опрос,
        // чтобы vsEditor() не конфликтовал с живым изменением текста ползунком
        if (isMouseDragging() || (hForeground == g_hShieldWnd)) {
            shouldProcessEditor = false;
        }
#endif

        if (shouldProcessEditor) {
            if (hForeground != NULL) {
                // ПУЛЕНЕПРОБИВАЕМЫЙ ЧЕК ПРОЦЕССА СТУДИИ:
                // Узнаем, какому конкретно Process ID принадлежит активное окно
                DWORD activeProcessId = 0;
                ::GetWindowThreadProcessId(hForeground, &activeProcessId);

                // Получаем истинный PID Visual Studio, который мы нашли при старте (GetStudioProcessId)
                DWORD targetStudioPid = GetStudioProcessId();

                // Если активное окно принадлежит НЕ Visual Studio (например, ты переключился на игру) —
                // полностью отключаем фоновый опрос буфера, освобождая 100% CPU в игровом цикле.
                if (activeProcessId != targetStudioPid) {
                    shouldProcessEditor = false;
                }
            }
            else {
                // Если активного окна вообще нет (фокус потерян в ОС) — тоже отключаем опрос
                shouldProcessEditor = false;
            }
        }

        // Вызываем фоновый инспектор ручного ввода с клавиатуры 
        // ТОЛЬКО когда фокус гарантированно внутри процесса Visual Studio
        if (shouldProcessEditor) {
            vsEditor();
        }

#endif
    }


}

#else

#define eval(...) __VA_ARGS__ 

#endif