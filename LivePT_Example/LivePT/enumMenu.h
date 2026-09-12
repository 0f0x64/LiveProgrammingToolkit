namespace LivePT {

    inline LRESULT CALLBACK TinyMenuWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    inline int showEnum(const std::vector<std::string>& enumMenu) {
        if (enumMenu.empty()) return -1;

        HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_StaticMenuWin";

        static bool classRegistered = [hInst, className]() {
            WNDCLASSEXA wcex = { sizeof(WNDCLASSEXA) };
            wcex.lpfnWndProc = TinyMenuWindowProc;
            wcex.hInstance = hInst;
            wcex.lpszClassName = className;
            return ::RegisterClassExA(&wcex) != 0;
            }();

        HWND hDummyWnd = CreateWindowExA(
            0, className, "Dummy", WS_POPUP,
            0, 0, 1, 1,
            NULL, NULL, hInst, NULL
        );

        if (!hDummyWnd) return -1;

        static bool hUxthemeLoaded = []() {
            HMODULE hUxtheme = ::GetModuleHandleA("uxtheme.dll");
            if (hUxtheme) {
                typedef enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max } PreferredAppMode;
                typedef PreferredAppMode(WINAPI* PfnSetPreferredAppMode)(PreferredAppMode);
                typedef void (WINAPI* PfnFlushMenuThemes)();

                auto SetPreferredAppMode = (PfnSetPreferredAppMode)::GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
                auto FlushMenuThemes = (PfnFlushMenuThemes)::GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136));
                if (SetPreferredAppMode && FlushMenuThemes) {
                    SetPreferredAppMode(ForceDark);
                    FlushMenuThemes();
                    return true;
                }
            }
            return false;
            }();

        HMENU hMenu = ::CreatePopupMenu();
        for (size_t i = 0; i < enumMenu.size(); ++i) {
            AppendMenuA(hMenu, MF_STRING, (UINT_PTR)(i + 1), enumMenu[i].c_str());
        }

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hDummyWnd);

        UINT flags = TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY;
        int selectedId = ::TrackPopupMenu(hMenu, flags, pt.x, pt.y, 0, hDummyWnd, NULL);

        PostMessageA(hDummyWnd, WM_NULL, 0, 0);
        DestroyMenu(hMenu);
        DestroyWindow(hDummyWnd);

        if (selectedId == 0) return -1; 

        return selectedId - 1;
    }

}