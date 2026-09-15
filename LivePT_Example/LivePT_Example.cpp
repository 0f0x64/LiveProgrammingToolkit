#include <windows.h>

// ---------------- LivePT integration section ----------------
#define LivePT_EditMode true // true for live text editing
#define LivePT_WheelEditMode false // true for mouse mButton drag, switch, enums with context menu
#define LivePT_WindowManagement true // 50/50 VisualStudio/your App split mode
#define LivePT_AppToSecondaryDisplay false 
#include "LivePT/LivePT.h" // incude it for using lib
// ------------------------------------------------------------

#define TIMER_ID 1

// 2. НАШ НОВЫЙ ТЕСТОВЫЙ АГРЕГАТНЫЙ ТИП
struct color {
    unsigned char r, g, b;
};

class Primitive {
public:
    enum class ptype { circle, box, roundbox };

    float x = 0;
    float y = 0;
    ptype type = ptype::circle;
    bool show = true;

    // Поле для хранения агрегата цвета
    color clr = { 0, 120, 215 };

    // ОБЩИЙ МЕТОД: позиция раздельно, тип енам, цвет как агрегат
    void Set(float xPos, float yPos, ptype form, color objectColor, bool showObj = true) {
        x = xPos;
        y = yPos;
        type = form;
        clr = objectColor;
        show = showObj;
    }

    // Полностью инкапсулированный метод рендеринга
    template <size_t N>
    static void DrawScene(HWND hwnd, HDC hdc, const Primitive(&arr)[N]) {
        RECT r;
        GetClientRect(hwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        FillRect(memDC, &r, (HBRUSH)(COLOR_WINDOW + 1));

        for (const auto& p : arr) {
            if (!p.show) continue;

            int posX = (w / 2) + p.x;
            int posY = (h / 2) + p.y;
            int size = 20;

            // ДИНАМИЧЕСКИЙ ЦВЕТ: Берем r, g, b прямо из нашего агрегата clr
            HBRUSH hBrush = CreateSolidBrush(RGB(p.clr.r, p.clr.g, p.clr.b));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBrush);

            switch (p.type) {
            case ptype::circle:   Ellipse(memDC, posX - size, posY - size, posX + size, posY + size); break;
            case ptype::box:      Rectangle(memDC, posX - size, posY - size, posX + size, posY + size); break;
            case ptype::roundbox: RoundRect(memDC, posX - size, posY - size, posX + size, posY + size, 25, 25); break;
            }

            SelectObject(memDC, hOldBrush);
            DeleteObject(hBrush);
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
    }
};

// Глобальный массив экземпляров примитивов
Primitive primitive[3];

// =================== USER SPACE ===================

void UpdateSceneParams() {

    // ПЕРВЫЙ ПРИМИТИВ: Тестируем кастомный агрегат цвета в одном eval
    primitive[0].Set(
        eval(-116.2f),
        eval(-18),
        eval(Primitive::ptype::roundbox),
        eval(color{210, 10, 15 }), // <--- Наш целевой тестовый вызов
        eval(true)
    );

    // ВТОРОЙ ПРИМИТИВ: Проверка обратной совместимости (другой цвет)
    primitive[1].Set(
        eval(-6),
        eval(-28),
        eval(Primitive::ptype::box),
        eval(color{ 15, 110, 0 }),eval(true)
    );

    // ТРЕТИЙ ПРИМИТИВ: Традиционный агрегатный синтаксис инициализации полей (снаружи eval)
    // Он гарантированно продолжит работать, так как внутри eval только примитивы
    unsigned char g = rand()%255;
    int x = eval(110)+rand() % 25;
    primitive[2].Set(
        eval(x),
        eval(-59),
        eval(Primitive::ptype::circle),
        eval(color{ 222, g, 21 }),
        eval(true)
    );
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
                #if LivePT_EditMode
                    LivePT::ProcessEdit();// don't forget this call
                #endif
            }
        }
    }

    return 0;
}
