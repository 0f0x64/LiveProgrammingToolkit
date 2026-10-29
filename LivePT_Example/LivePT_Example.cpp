#include <windows.h>
#include <vector>
#include <source_location>
#include <string>
#include <cmath>

#define LPT_SHADER_COMPILE_FUNC recompileShader()
void recompileShader();
// ---------------- LivePT integration section ----------------
#define LivePT_EditMode true // true for activate
#include "LivePT/LivePT.h" // incude it for using lib

// ---------------------- d3d renderer ------------------------
#define EditMode true
#define DebugMode true
#define ExternalResourcesMode true
#include "dx11/dx.h"
// ------------------------------------------------------------

#define TIMER_ID 1




struct {
    HWND hWnd;
    HDC memDC;
    int w;
    int h;
} gc;


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

#include "renderer\timer.h"
#include "renderer\gAPI.h"
#include "app.h"

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

        // Проверяем сообщения Windows
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        //PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE);
        //if (GetAsyncKeyState(VK_ESCAPE) || timer::frameBeginTime / 1000. > DEMO_DURATION) { break; }

#endif

        App::Loop();

        LivePT::ProcessEdit();// don't forget this call

    }

    return 0;
}
