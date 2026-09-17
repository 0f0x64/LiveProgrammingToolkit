#include <windows.h>

// ---------------- LivePT integration section ----------------
#define LivePT_EditMode true // true for live text editing
#define LivePT_WheelEditMode false // true for mouse mButton drag, switch, enums with context menu
#include "LivePT/LivePT.h" // incude it for using lib
// ------------------------------------------------------------

#define TIMER_ID 1

//simple class for demo purposes
#pragma once
#include <windows.h>

class Primitive {
public:
    enum class ptype { circle, box, roundbox };

    // АДСКАЯ ПЕРЕМЕШКА ТИПОВ ДЛЯ ПРОВЕРКИ ВЫРАВНИВАНИЯ MSVC:
    struct Color4 {
        enum class show_type : int { none = 0, solid = 1, gradient = 2 };
        enum class blending_type : int { normal = 0, additive = 1 };

        unsigned char r = 0;       // 1 байт
        show_type show = show_type::solid; // 4 байта (MSVC вставит 3 байта padding ПЕРЕД show!)
        unsigned char g = 0;       // 1 байт
        blending_type blend = blending_type::normal; // 4 байта (MSVC вставит 3 байта padding ПЕРЕД blend!)
        unsigned char b = 0;       // 1 байт
        // В конце MSVC докинет еще 3 байта padding, чтобы весь размер Color4 делился на 4!
    };

    int x = 0;
    int y = 0;
    ptype type = ptype::circle;
    bool show = true;
    Color4 color; // Наша перемешанная структура

    void Set(int xPos, int yPos, ptype form, bool showObj, Color4 col) {
        *this = { xPos, yPos, form, showObj, col };
    }
    void Set(const Primitive& in) { *this = in; }

    template <size_t N>
    static void DrawScene(HWND hwnd, HDC hdc, const Primitive(&arr)[N]) {
        RECT r; GetClientRect(hwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        FillRect(memDC, &r, (HBRUSH)(COLOR_WINDOW + 1));

        for (const auto& p : arr) {
            if (!p.show) continue;
            int posX = (w / 2) + p.x; int posY = (h / 2) + p.y; int size = 20;
            HBRUSH hBrush = nullptr;
            if (p.color.show == Color4::show_type::none) hBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            else hBrush = CreateSolidBrush(RGB(p.color.r, p.color.g, p.color.b));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBrush);

            switch (p.type) {
            case ptype::circle:   Ellipse(memDC, posX - size, posY - size, posX + size, posY + size); break;
            case ptype::box:      Rectangle(memDC, posX - size, posY - size, posX + size, posY + size); break;
            case ptype::roundbox: RoundRect(memDC, posX - size, posY - size, posX + size, posY + size, 25, 25); break;
            }
            SelectObject(memDC, hOldBrush); if (p.color.show != Color4::show_type::none) DeleteObject(hBrush);
        }
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
    }
};

inline Primitive primitive[3];


// =================== USER SPACE ===================

void UpdateSceneParams() {

    // Сценарий 1: Навешивание eval на отдельные скалярные аргументы
    primitive[0].Set(
        eval(-215),
        eval(-100),
        eval(Primitive::ptype::box),
        eval(true),
        eval(Primitive::Color4{ 220, Primitive::Color4::show_type::solid, 0, Primitive::Color4::blending_type::normal, 0 })
    );

    // Сценарий 2: Тест с вещественными суффиксами и неявным кастингом типов на стороне хоста
    primitive[1].Set(
        eval(-110.45f),
        eval(-10.99f),
        eval(Primitive::ptype::roundbox),
        eval(true),
        eval(Primitive::Color4{ 0, Primitive::Color4::show_type::solid, 220, Primitive::Color4::blending_type::normal, 30 })
    );

    // Сценарий 3: Агрегатная инициализация всего разнородного примитива 
    // И вложенного асимметричного цвета под раздельными макросами eval
    primitive[2].Set(Primitive{
        .x = eval(0),
        .y = eval(100),
        .type = eval(Primitive::ptype::circle),
        .show = eval(true),
        // ЦВЕТ ПОЛНОСТЬЮ ПОД СВОИМ EVAL: MSVC рассчитает размеры и типы полей,
        // а рекурсивный AutoMapper в eval.h разложит токены, полностью проигнорировав пустые байты выравнивания!
        .color = eval(Primitive::Color4{
            .r = 0,
            .show = Primitive::Color4::show_type::solid,
            .g = 0,
            .blend = Primitive::Color4::blending_type::normal,
            .b = 220
        })
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
