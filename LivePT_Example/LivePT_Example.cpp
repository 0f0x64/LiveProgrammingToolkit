#include <windows.h>
#include <vector>
#include <source_location>
#include <string>
#include <cmath>

// ---------------- LivePT integration section ----------------
#define LivePT_EditMode true // true for activate
#define LivePT_SHADER_COMPILE_FUNC recompileShader
#include "LivePT/LivePT.h" // incude it for using lib
// ------------------------------------------------------------

struct {
    HWND hWnd;
    HDC memDC;
    int w;
    int h;
} gc;

#include "engine\timer.h"
#include "engine\gfx.h"
#include "engine\sound.h"
#include "engine\watcher.h"
#include "app\app.h"

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    switch (uMsg) {

        case WM_SIZE:
        {
            dx11::processResizeMessage(lParam);
            return 0;
        }

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }

    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

    const char CLASS_NAME[] = "WindowClass";

    dx11::screen.width = GetSystemMetrics(SM_CXSCREEN);
    dx11::screen.height = GetSystemMetrics(SM_CYSCREEN);

    auto hUser32 = GetModuleHandleA("user32.dll");
    typedef BOOL(WINAPI* PfnSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST,
        CLASS_NAME,
        "LivePT Test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        600, 400,
        NULL, NULL,
        hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    dx11::Init(hwnd);
    App::init();

    ShowWindow(hwnd, nCmdShow);

    MSG msg = { 0 };

    while (msg.message != WM_QUIT)
    {

    #if EditMode

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

    #else

        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        //PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE);
        //if (GetAsyncKeyState(VK_ESCAPE) || timer::frameBeginTime / 1000. > DEMO_DURATION) { break; }

    #endif

        if (IsIconic(hwnd))
        {
            return 0;
        }

        App::Loop();

        LivePT::ProcessEdit();// don't forget this call
        WatchForRecompilation();

    }

    dx11::Audio::ReleaseAudioEngine();

    return 0;
}
