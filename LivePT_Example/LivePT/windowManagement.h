#pragma once
#include <windows.h>
#include <dwmapi.h>

#pragma comment(lib, "Dwmapi.lib")

namespace LivePT {

    // Ссылка на внешнюю функцию получения PID из твоего vsEditor.h
    DWORD GetStudioProcessId();

    class LptWindowManager {
    private:
#if LivePT_WindowManagement
        HWND m_hUserWnd = NULL;
        HWND m_hVsWnd = NULL;
        RECT m_oldVsRc = { 0 };
        bool m_isArranged = false;
        bool m_vsSaved = false;
        bool m_wasMaximized = false; // Запоминаем статус: была ли развернута на старте

        bool isWin11() {
            auto sharedUserData = (BYTE*)0x7FFE0000;
            return (*(ULONG*)(sharedUserData + 0x26c) >= 10) && (*(ULONG*)(sharedUserData + 0x260) >= 22000);
        }

        RECT GetRealWindowRect() {
            RECT rect = { 0 };
            if (isWin11() && SUCCEEDED(DwmGetWindowAttribute(m_hUserWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(RECT)))) {
                return rect;
            }
            GetWindowRect(m_hUserWnd, &rect);
            return rect;
        }

        void ApplyDarkTheme() {
            if (!isWin11() || !m_hUserWnd) return;
            COLORREF DARK_COLOR = 0x00202020;
            DwmSetWindowAttribute(m_hUserWnd, DWMWINDOWATTRIBUTE::DWMWA_CAPTION_COLOR, &DARK_COLOR, sizeof(DARK_COLOR));
        }

        void MoveToSecondary(const DEVMODEA& dm) {
            SetWindowPos(m_hUserWnd, HWND_TOP, dm.dmPosition.x, dm.dmPosition.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);

            MONITORINFO info = { sizeof(MONITORINFO) };
            GetMonitorInfoA(MonitorFromWindow(m_hUserWnd, MONITOR_DEFAULTTONEAREST), &info);

            SetWindowPos(m_hUserWnd, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top, SWP_NOACTIVATE | SWP_FRAMECHANGED);
            ShowWindow(m_hUserWnd, SW_MAXIMIZE);
            ApplyDarkTheme();

            // 🔥 ФИКС МЕРЦАНИЯ: Жестко блокируем повторные вызовы после переноса окна
            m_isArranged = true;
        }

        bool TrySetupDualMonitor() {
            DWORD i = 0;
            DISPLAY_DEVICEA dc = { sizeof(dc) };
            while (EnumDisplayDevicesA(NULL, i++, &dc, EDD_GET_DEVICE_INTERFACE_NAME)) {
                if (!(dc.StateFlags & DISPLAY_DEVICE_ACTIVE) || (dc.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)) continue;

                DEVMODEA dm;
                EnumDisplaySettingsA(dc.DeviceName, ENUM_CURRENT_SETTINGS, &dm);
                if (dm.dmPosition.x != 0 || dm.dmPosition.y != 0) {
                    MoveToSecondary(dm);
                    return true;
                }
            }
            return false;
        }

        void SaveVsState() {
            if (m_vsSaved) return;
            m_wasMaximized = IsZoomed(m_hVsWnd) != 0;
            if (!m_wasMaximized) {
                GetWindowRect(m_hVsWnd, &m_oldVsRc);
            }
            m_vsSaved = true;
        }

        void SetupSingleMonitor() {
            if (!m_hVsWnd || !IsWindow(m_hVsWnd)) return;

            MONITORINFO info = { sizeof(MONITORINFO) };
            GetMonitorInfoA(MonitorFromWindow(m_hUserWnd, MONITOR_DEFAULTTOPRIMARY), &info);
            RECT rc = info.rcWork;
            int halfWidth = (rc.right - rc.left) / 2;

            SaveVsState();

            if (IsZoomed(m_hUserWnd)) ShowWindow(m_hUserWnd, SW_RESTORE);
            if (IsZoomed(m_hVsWnd))   ShowWindow(m_hVsWnd, SW_RESTORE);

            const int PAD = 7;

            // 1. Окно приложения влево
            SetWindowPos(m_hUserWnd, HWND_TOP,
                rc.left - PAD, rc.top,
                halfWidth + (PAD * 2), (rc.bottom - rc.top) + PAD,
                SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED);

            // 2. Окно Visual Studio вправо
            SetWindowPos(m_hVsWnd, HWND_TOP,
                rc.left + halfWidth - PAD, rc.top,
                halfWidth + (PAD * 2), (rc.bottom - rc.top) + PAD,
                SWP_SHOWWINDOW | SWP_FRAMECHANGED);

            ApplyDarkTheme();
            m_isArranged = true;
        }

        void ArrangeWindows() {
            // 🔥 ОБНОВЛЕННЫЙ ДЕФАЙН: LivePT_AppToSecondaryDisplay
#ifndef LivePT_AppToSecondaryDisplay
#define LivePT_AppToSecondaryDisplay true
#endif

            // Если макрос true и мониторов больше одного — пытаемся укинуть на второй
            if (GetSystemMetrics(SM_CMONITORS) > 1 && LivePT_AppToSecondaryDisplay) {
                if (TrySetupDualMonitor()) return;
            }

            // Если макрос false или монитор один — гарантированно разворачиваем сплит на текущем экране
            SetupSingleMonitor();
        }
#endif

    public:
        LptWindowManager() {
#if LivePT_WindowManagement
            DWORD vsPid = GetStudioProcessId();
            if (vsPid != 0) {
                DWORD_PTR result = vsPid;
                EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
                    DWORD wndPid = 0;
                    GetWindowThreadProcessId(hWnd, &wndPid);
                    if (wndPid == *(DWORD*)lParam && IsWindowVisible(hWnd)) {
                        LONG style = GetWindowLong(hWnd, GWL_STYLE);
                        if ((style & WS_MAXIMIZEBOX) && (style & WS_MINIMIZEBOX)) {
                            *(HWND*)lParam = hWnd;
                            return FALSE;
                        }
                    }
                    return TRUE;
                    }, (LPARAM)&result);
                if (result != vsPid) m_hVsWnd = (HWND)result;
            }
#endif
        }

        ~LptWindowManager() {
#if LivePT_WindowManagement
            if (m_vsSaved && m_hVsWnd && IsWindow(m_hVsWnd)) {
                if (m_wasMaximized) {
                    ShowWindow(m_hVsWnd, SW_MAXIMIZE);
                }
                else {
                    SetWindowPos(m_hVsWnd, HWND_TOP,
                        m_oldVsRc.left, m_oldVsRc.top,
                        m_oldVsRc.right - m_oldVsRc.left,
                        m_oldVsRc.bottom - m_oldVsRc.top,
                        SWP_SHOWWINDOW);
                }
            }
#endif
        }

        void SetUserWindow(HWND hwnd) {
#if LivePT_WindowManagement
            m_hUserWnd = hwnd;
#endif
        }

        void Tick() {
#if LivePT_WindowManagement
            if (m_isArranged) return;

            if (!m_hUserWnd) {
                m_hUserWnd = GetActiveWindow();
                if (!m_hUserWnd) m_hUserWnd = GetForegroundWindow();
            }

            if (!m_hUserWnd || !IsWindow(m_hUserWnd) || !IsWindowVisible(m_hUserWnd)) return;

            ArrangeWindows();
#endif
        }
    };

    inline LptWindowManager& GetWindowManager() {
        static LptWindowManager instance;
        return instance;
    }
}
