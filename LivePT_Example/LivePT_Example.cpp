#include <windows.h>

// ---------------- LivePT integration section ----------------
#define LivePT_EditMode true // true for activate
#include "LivePT/LivePT.h" // incude it for using lib
// ------------------------------------------------------------

#define TIMER_ID 1

//simple class for demo purposes
enum class ptype { circle, box, roundbox };

struct color3 {
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

#include "Widgets\colorPicker.h"
LPT_REGISTER_TYPE(color3, LivePT::MyColorPickerCallback);

struct pos2 {
    float x;
    float y;
};

#include "Widgets\posController.h"
LPT_REGISTER_TYPE(pos2, LivePT::MyPosPickerCallback);

struct size2 {
    float x;
    float y;
};

typedef float angle;
#include "Widgets\AngleController.h"
LPT_REGISTER_TYPE(angle, LivePT::MyAnglePickerCallback);

class Primitive {
public:


    pos2 pos;
    size2 size;
    angle Angle;
    ptype type = ptype::circle;
    bool show;
    color3 color;

    // Unified setters using C++20 aggregate initialization rules
    void Set(pos2 pos, size2 size, angle Angle,ptype form, bool showObj, color3 color) {
        *this = { pos, size, Angle, form, showObj, color };
    }
    void Set(const Primitive& in) { *this = in; }

    template <size_t N>
    static void DrawScene(HWND hwnd, HDC hdc, const Primitive(&arr)[N]) {
        RECT r;
        GetClientRect(hwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        SetGraphicsMode(memDC, GM_ADVANCED);

        HBRUSH hDarkBrush = CreateSolidBrush(RGB(30, 30, 32));
        FillRect(memDC, &r, hDarkBrush);
        DeleteObject(hDarkBrush);

        for (const auto& p : arr) {
            if (!p.show) continue;

            int posX = (w / 2) + p.pos.x;
            int posY = (h / 2) + p.pos.y;
            size2 size = p.size;

            HBRUSH hBrush = CreateSolidBrush(RGB(p.color.r, p.color.g, p.color.b));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBrush);

            float angleVal = p.Angle; 
            float radians = angleVal * (3.14159265f / 180.0f);
            float cosA = std::cos(radians);
            float sinA = std::sin(radians);

            XFORM xForm;

            xForm.eM11 = cosA;  xForm.eM12 = sinA;
            xForm.eM21 = -sinA; xForm.eM22 = cosA;
            xForm.eDx = (float)posX;
            xForm.eDy = (float)posY;

            XFORM oldForm;
            GetWorldTransform(memDC, &oldForm);

            SetWorldTransform(memDC, &xForm);

            switch (p.type) {
            case ptype::circle:   Ellipse(memDC, -size.x, -size.y, size.x, size.y); break;
            case ptype::box:      Rectangle(memDC, -size.x, -size.y, size.x, size.y); break;
            case ptype::roundbox: RoundRect(memDC, -size.x, -size.y, size.x, size.y, 40, 40); break;
            }

            SetWorldTransform(memDC, &oldForm);

            SelectObject(memDC, hOldBrush);
            DeleteObject(hBrush);
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
    }

};

// Global instance array of primitives
Primitive primitive[3];

// =================== USER SPACE ===================

void UpdateSceneParams() {

    primitive[0].Set({
        .pos = eval(pos2{-259.5996f,-292.6638f}),
        .size = eval(size2{110.0f,101.0f}),
        .Angle = eval(0),
        .type = eval(ptype::roundbox),
        .show = eval(true),
        .color = eval(color3{175,25,35})
        });

    
#include "test.h"

    primitive[2].Set({
        .pos = eval(pos2{86.98f,155.33f}),
        .size = eval(size2{68.0787f,99.8398f}),
        .Angle = eval(angle(310.9f)),
        .type = eval(ptype::circle),
        .show = eval(true),
        .color = eval(color3{64,54,215})
        });

}

// ================ END OF USER SPACE ================

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        SetTimer(hwnd, TIMER_ID, 16, NULL);
        return 0;

    case WM_TIMER:
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        UpdateSceneParams();
        Primitive::DrawScene(hwnd, hdc, primitive);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

    const char CLASS_NAME[] = "WindowClass";
    
    auto hUser32 = GetModuleHandleA("user32.dll");
    typedef BOOL(WINAPI* PfnSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

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

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};

    DWORD lastUpdate = GetTickCount();
    while (true) {

        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            DWORD currentTick = GetTickCount();
            if (currentTick - lastUpdate > 16) { // ~60 FPS update limit
                lastUpdate = currentTick;
                LivePT::ProcessEdit();// don't forget this call
            }
        }
    }

    return 0;
}
