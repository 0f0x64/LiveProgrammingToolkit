#include <windows.h>

// ---------------- LivePT integration section ----------------
#define LivePT_EditMode true // true for live text editing
#define LivePT_WheelEditMode false // true for mouse mButton drag, switch, enums with context menu
#include "LivePT/LivePT.h" // incude it for using lib
// ------------------------------------------------------------

#define TIMER_ID 1

//simple class for demo purposes
class Primitive {
public:
    enum class ptype { circle, box, roundbox };

    // Вложенная структура цвета, спроектированная как чистый агрегат для авто-парсинга
    struct Color4 {
        enum class show_type { none, solid, gradient };

        show_type show = show_type::solid;
        int r = 0;
        int g = 0;
        int b = 0;
    };

    int x = 0;
    int y = 0;
    ptype type = ptype::circle;
    bool show = true;
    Color4 color; // Добавленное поле пользовательского агрегатного типа

    // Унифицированные сеттеры, расширенные поддержкой структуры Color4
    void Set(int xPos, int yPos, ptype form, bool showObj, Color4 col) {
        *this = { xPos, yPos, form, showObj, col };
    }
    void Set(const Primitive& in) { *this = in; }

    // Полностью инкапсулированный метод рендеринга с учетом кастомных цветов GDI
    template <size_t N>
    static void DrawScene(HWND hwnd, HDC hdc, const Primitive(&arr)[N]) {
        RECT r;
        GetClientRect(hwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;

        // Инициализация двойной буферизации
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        // Очистка фона
        FillRect(memDC, &r, (HBRUSH)(COLOR_WINDOW + 1));

        // Цикл рендеринга всех примитивов в массиве
        for (const auto& p : arr) {
            if (!p.show) continue;

            int posX = (w / 2) + p.x;
            int posY = (h / 2) + p.y;
            int size = 20; // Радиус фигуры

            // Динамическое создание кисти на основе полей вложенной структуры Color4
            HBRUSH hBrush = nullptr;
            if (p.color.show == Color4::show_type::none) {
                hBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            }
            else {
                // Для solid и gradient (в качестве базового цвета) берем пользовательские RGB
                hBrush = CreateSolidBrush(RGB(p.color.r, p.color.g, p.color.b));
            }
            HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBrush);

            switch (p.type) {
            case ptype::circle:   Ellipse(memDC, posX - size, posY - size, posX + size, posY + size); break;
            case ptype::box:      Rectangle(memDC, posX - size, posY - size, posX + size, posY + size); break;
            case ptype::roundbox: RoundRect(memDC, posX - size, posY - size, posX + size, posY + size, 25, 25); break;
            }

            SelectObject(memDC, hOldBrush);
            if (p.color.show != Color4::show_type::none) {
                DeleteObject(hBrush);
            }
        }

        // Вывод буфера на экран и освобождение ресурсов GDI
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

    // 1. Плоский вызов скаляров (базовый тест)
    primitive[0].Set(eval(-116), eval(-118), eval(Primitive::ptype::box), eval(true), eval(Primitive::Color4{ Primitive::Color4::show_type::solid, 220, 220, 25 }));

    // 2. Тест с плавающей точкой
    primitive[1].Set(eval(-116), eval(-28), eval(Primitive::ptype::roundbox), eval(true), eval(Primitive::Color4{ Primitive::Color4::show_type::solid, 0, 20, 15 }));

    // 3. Агрегатная инициализация всего примитива И вложенного цвета под раздельными eval!
    primitive[2].Set(Primitive{
        .x = eval(-191),
        .y = eval(36),
        .type = eval(Primitive::ptype::circle),
        .show = eval(true),
        // ЗАГОНЯЕМ ВЕСЬ ЦВЕТ ПОД СВОЙ EVAL — ТЕПЕРЬ ЭТО НЕЗАВИСИМЫЙ АГРЕГАТНЫЙ ПАРАМЕТР!
        .color = eval(Primitive::Color4{
            .show = Primitive::Color4::show_type::solid,
            .r = 225,
            .g = 20,
            .b = 0
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
