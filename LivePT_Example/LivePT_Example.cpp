#include <windows.h>

// ---------------- LivePT integration section ----------------
#define LivePT_EditMode true // true for live text editing
#define LivePT_WheelEditMode true // true for mouse mButton drag, switch, enums with context menu
#define LivePT_WindowManagement true
#define LivePT_AppToSecondaryDisplay false
#define LPT_TRIGGER_BUTTON    VK_LBUTTON  
#include "LivePT/LivePT.h" // incude it for using lib
// ------------------------------------------------------------

#define TIMER_ID 1

//simple class for demo purposes
class Primitive {
public:
    enum class ptype { circle, box, roundbox };
    enum class show_t { on, off };
    float x = 0;
    int y = 0;
    ptype type = ptype::circle;
    show_t show;

    // ОТДЕЛЬНЫЕ КОМПОНЕНТЫ ЦВЕТА (По умолчанию — наш красивый синий: 0, 120, 215)
    struct color3 {
        unsigned char r;
        unsigned char g;
        unsigned char b;
    };

    color3 color;

    // Unified setters using C++20 aggregate initialization rules
    void Set(float xPos, int yPos, ptype form, show_t showObj, color3 color = { 100,0,0 }) {
        *this = { xPos, yPos, form, showObj, color };
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
            if (p.show == show_t::off) continue;

            int posX = (w / 2) + p.x;
            int posY = (h / 2) + p.y;
            int size = 20; // shape radius

            // ДИНАМИЧЕСКИЙ ЦВЕТ: Передаем раздельные байты r, g, b в системный макрос Win32
            HBRUSH hBrush = CreateSolidBrush(RGB(p.color.r, p.color.g, p.color.b));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBrush);

            switch (p.type) {
            case ptype::circle:   Ellipse(memDC, posX - size, posY - size, posX + size, posY + size); break;
            case ptype::box:      Rectangle(memDC, posX - size, posY - size, posX + size, posY + size); break;
            case ptype::roundbox: RoundRect(memDC, posX - size, posY - size, posX + size, posY + size, 25, 25); break;
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
Primitive primitive[4];

// =================== USER SPACE ===================

void UpdateSceneParams() {

    // drag numbers by pressing&move mButton inside eval, click mButton for switch / context menu

    primitive[0].Set(eval(-300), 
        eval(-299), eval(Primitive::ptype::box), 
        eval(Primitive::show_t::on), eval(Primitive::color3{ 204,28,110 }));

    /*    primitive[0].Set(eval(-294),
            eval(-174), eval(Primitive::ptype::box),
            eval(Primitive::show_t::on), eval(Primitive::color3{ 176,28,8 }));*/


    // floating point numbers allowed (will be casted to int in this case, but you can modify class to use native floats)
    primitive[1].Set(eval(-123), eval(-40), 
        eval(Primitive::ptype::box), 
        eval(Primitive::show_t::on), eval(Primitive::color3{ 175,27,91 }));

    unsigned char x = rand() % eval(23);
    unsigned char y = rand() % eval(9);



    // aggregate init alternative
    primitive[2].Set(Primitive{
        .x = eval(   112.06  ),
        .y = eval(-264),
        .type = eval(Primitive::ptype::circle),
        .show = eval(Primitive::show_t::on),
        .color = eval(Primitive::color3{
            .r = x,

            .g = 134,//.g=22 aaaaaa
            .b = y})
        });

    
    primitive[3].Set(Primitive{

    .x = eval(-139.9f),
    .y = eval(-249.26),
    .type = eval(Primitive::ptype::roundbox),
    .show = eval(Primitive::show_t::on),
    .color = eval(Primitive::color3{39,166,102})
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
