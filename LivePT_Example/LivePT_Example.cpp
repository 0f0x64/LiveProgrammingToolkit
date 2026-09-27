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

struct pos2 {
    float x;
    float y;
};

struct size2 {
    float x;
    float y;
};

class Primitive {
public:


    pos2 pos;
    size2 size;
    ptype type = ptype::circle;
    bool show;
    color3 color;

    // Unified setters using C++20 aggregate initialization rules
    void Set(pos2 pos, size2 size, ptype form, bool showObj, color3 color) {
        *this = { pos, size, form, showObj, color };
    }
    void Set(const Primitive& in) { *this = in; }

    // Fully encapsulated rendering method (accepts any array size)
    template <size_t N>
    static void DrawScene(HWND hwnd, HDC hdc, const Primitive(&arr)[N]) {
        RECT r;
        GetClientRect(hwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;

        // Double buffering initialization
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        // Clear background
        HBRUSH hDarkBrush = CreateSolidBrush(RGB(30, 30, 30));

        // 2. Заливаем прямоугольник
        FillRect(memDC, &r, hDarkBrush);

        // 3. Удаляем кисть, когда она больше не нужна
        DeleteObject(hDarkBrush);

        // Render loop for all visible primitives in the array
        for (const auto& p : arr) {
            if (!p.show) continue;

            int posX = (w / 2) + p.pos.x;
            int posY = (h / 2) + p.pos.y;

            size2 size = p.size;

            // ДИНАМИЧЕСКИЙ ЦВЕТ: Передаем раздельные байты r, g, b в системный макрос Win32
            HBRUSH hBrush = CreateSolidBrush(RGB(p.color.r, p.color.g, p.color.b));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBrush);

            switch (p.type) {
                case ptype::circle:   Ellipse(memDC, posX - size.x, posY - size.y, posX + size.x, posY + size.y); break;
                case ptype::box:      Rectangle(memDC, posX - size.x, posY - size.y, posX + size.x, posY + size.y); break;
                case ptype::roundbox: RoundRect(memDC, posX - size.x, posY - size.y, posX + size.x, posY + size.y, 75, 75); break;
            }

            // Освобождаем ресурсы GDI сразу после отрисовки фигуры
            SelectObject(memDC, hOldBrush);
            DeleteObject(hBrush);
        }

        // Blit buffer to screen and release GDI resources
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
    }
};


// Global instance array of primitives
Primitive primitive[3];

#include "colorPicker.h"

// =================== USER SPACE ===================

void UpdateSceneParams() {

    primitive[0].Set(Primitive{
        .pos = eval(pos2{-227.88f,-367.14f}),
        .size = eval(size2{119.435f,113.675f}),
        .type = eval(ptype::circle),
        .show = eval(true),

        .color = eval(color3{210,147,2})
        });

    
#include "test.h"

    primitive[2].Set(Primitive{
        .pos = eval(pos2{229,149}),
        .size = eval(size2{115.3995f,106.2135f}),
        .type = eval(ptype::roundbox),
        .show = eval(true),
        .color = eval(color3{117,137,197})
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

    LivePT::RegisterTypeDoubleClickCallback<color3>(MyColorPickerCallback);

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
