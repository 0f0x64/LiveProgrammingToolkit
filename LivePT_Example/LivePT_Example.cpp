#include <windows.h>
#include <vector>
#include <source_location>
#include <string>
#include <cmath>
#include <initializer_list>

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
#include "Widgets\color3.h"
LPT_REGISTER_TYPE("color3", Widgets::color3Callback);
LPT_REGISTER_DRAG("color3", DrugLogic::color3Drag);

struct pos2 {
    float x;
    float y;
};
#include "Widgets\pos2.h"
LPT_REGISTER_TYPE("pos2", Widgets::pos2Callback);
LPT_REGISTER_DRAG("pos2", DrugLogic::pos2Drag);

struct size2 {
    float w;
    float h;
};
LPT_REGISTER_TYPE("size2", Widgets::pos2Callback);
LPT_REGISTER_DRAG("size2", DrugLogic::size2Drag);

typedef float angle;
#include "Widgets\angle.h"
LPT_REGISTER_TYPE("angle", Widgets::angleCallback);

struct {
    HWND hWnd;
    HDC memDC;
    int w;
    int h;
} gc;

struct Primitive {
    pos2 pos;
    size2 size;
    angle Angle;
    ptype type = ptype::circle;
    bool show;
    color3 color;
};

int drawCounter = 0;
int curSel = -1;

bool IsPrimitiveSelected(int posX, int posY, HWND hwnd) {
    POINT mousePos;
    if (!GetCursorPos(&mousePos)) {
        return false;
    }

    if (!ScreenToClient(hwnd, &mousePos)) {
        return false;
    }

    int dx = mousePos.x - posX;
    int dy = mousePos.y - posY;

    if ((dx * dx + dy * dy) > 100) {
        return false;
    }

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {

        //while (GetAsyncKeyState(VK_LBUTTON))
        {
            //  Sleep(16);
        }
        curSel = drawCounter;
        return true;
    }

    return false;
}



// Новая структура параметров, принимающая список инициализации точек
struct HeavyPoint {
    bool isVisible;          // 1 байт (+ 3 байта padding)
    pos2 coordinates;        // 8 байт (float x, float y)
    unsigned char thickness; // 1 байт (+ 3 байта padding)
    int customUid;           // 4 байта
    color3 segmentColor;     // 3 байта (r, g, b) -> Тяжелый тест вложенности структур!
};                           // Общий размер будет выровнен компилятором до ~28-32 байт

struct LinePrimitive {
    std::vector<HeavyPoint> points;
};

// main.cpp

// Полностью исправленная функция DrawLine в main.cpp
// Полностью исправленная, рабочая отрисовка в main.cpp
void DrawLine(LinePrimitive lp, std::source_location location = std::source_location::current()) {
    BOOL highlight = LivePT::IsCursorInsideFunctionCall(location);
    if (lp.points.size() < 2) return;

    int centerX = gc.w / 2;
    int centerY = gc.h / 2;

    // Храним указатель на предыдущую валидную (видимую) точку
    const HeavyPoint* pPrevPoint = nullptr;

    for (const auto& pt : lp.points) {
        // Выводим текст координат и UID ВСЕГДА, чтобы видеть реальные числа в рантайме
        int targetX = centerX + (int)pt.coordinates.x;
        int targetY = centerY + (int)pt.coordinates.y;

        std::string infoStr = "#" + std::to_string(pt.customUid) + " (" + std::to_string((int)pt.coordinates.x) + "," + std::to_string((int)pt.coordinates.y) + ")";
        SetTextColor(gc.memDC, RGB(pt.segmentColor.r, pt.segmentColor.g, pt.segmentColor.b));
        SetBkMode(gc.memDC, TRANSPARENT);
        TextOutA(gc.memDC, targetX + 8, targetY - 12, infoStr.c_str(), (int)infoStr.length());

        // Если точка невидима — линию через нее не ведем, но кэш плоттера не ломаем
        if (!pt.isVisible) continue;

        if (pPrevPoint == nullptr) {
            // Это первая видимая точка — просто запоминаем её как стартовую
            pPrevPoint = &pt;
        }
        else {
            // Рисуем линию от предыдущей видимой точки к текущей
            int startX = centerX + (int)pPrevPoint->coordinates.x;
            int startY = centerY + (int)pPrevPoint->coordinates.y;

            int currentThickness = highlight ? (pt.thickness + 2) : pt.thickness;
            HPEN hPen = CreatePen(PS_SOLID, currentThickness, RGB(pt.segmentColor.r, pt.segmentColor.g, pt.segmentColor.b));
            HPEN hOldPen = (HPEN)SelectObject(gc.memDC, hPen);

            // Чертим строго между двумя конкретными точками
            MoveToEx(gc.memDC, startX, startY, NULL);
            LineTo(gc.memDC, targetX, targetY);

            SelectObject(gc.memDC, hOldPen);
            DeleteObject(hPen);

            // Переставляем указатель на текущую точку
            pPrevPoint = &pt;
        }
    }
}



struct TestRect {
    pos2 pos;
    size2 size;
    float Angle;
    ptype type;   // <-- Убеждаемся, что это поле присутствует
    bool show;
    color3 color;
    bool fill;
};


LPT_LIVE_FUNCTION(void, DrawTestRect, TestRect, in) {

    int posX = (gc.w / 2) + in.pos.x;
    int posY = (gc.h / 2) + in.pos.y;
    if (IsPrimitiveSelected(posX, posY, gc.hWnd)) {
        LivePT::OpenFileAndMoveCursorToLocation(location);
    }

    BOOL highlight = LivePT::IsCursorInsideFunctionCall(location);

    // 1. Создаем уникальные хэндлы для тела примитива
    HBRUSH hBodyBrush = in.fill ? CreateSolidBrush(RGB(in.color.r, in.color.g, in.color.b)) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN hBodyPen = CreatePen(PS_SOLID, highlight ? 4 : 2, RGB(in.color.r, in.color.g, in.color.b));

    HBRUSH hOldBrush = (HBRUSH)SelectObject(gc.memDC, hBodyBrush);
    HPEN hOldPen = (HPEN)SelectObject(gc.memDC, hBodyPen);

    float radians = in.Angle * (3.14159265f / 180.0f);
    float cosA = std::cos(radians);
    float sinA = std::sin(radians);

    XFORM xForm;
    xForm.eM11 = cosA;  xForm.eM12 = sinA;
    xForm.eM21 = -sinA; xForm.eM22 = cosA;
    xForm.eDx = (float)posX;
    xForm.eDy = (float)posY;

    XFORM oldForm;
    GetWorldTransform(gc.memDC, &oldForm);
    SetWorldTransform(gc.memDC, &xForm);

    if (in.show) switch (in.type)
    {
    case ptype::box:
        Rectangle(gc.memDC, -in.size.w, -in.size.h, in.size.w, in.size.h);
        break;

    case ptype::circle:
        Ellipse(gc.memDC, -in.size.w, -in.size.w, in.size.w, in.size.w);
        break;

    case ptype::roundbox:
        RoundRect(gc.memDC, -in.size.w, -in.size.h, in.size.w, in.size.h, 40, 40);
        break;

    default:
        Rectangle(gc.memDC, -in.size.w, -in.size.h, in.size.w, in.size.h);
        break;
    }

    // Восстанавливаем исходные объекты отрисовки
    SelectObject(gc.memDC, hOldBrush);
    SelectObject(gc.memDC, hOldPen);

    // Удаляем созданные объекты тела (защита от утечки GDI)
    DeleteObject(hBodyPen);
    if (in.fill) DeleteObject(hBodyBrush);

    // 2. Создаем СОВЕРШЕННО ОТДЕЛЬНУЮ кисть для центрального маркера подсветки
    HBRUSH hMarkerBrush = highlight ? CreateSolidBrush(RGB(255, 255, 255)) : CreateSolidBrush(RGB(122, 122, 122));
    HBRUSH hOldMarkerBrush = (HBRUSH)SelectObject(gc.memDC, hMarkerBrush);

    Ellipse(gc.memDC, -12, -12, 12, 12);

    SelectObject(gc.memDC, hOldMarkerBrush);
    DeleteObject(hMarkerBrush); // Безопасно удаляем только маркер

    SetWorldTransform(gc.memDC, &oldForm);
    drawCounter++;
}

void Draw(Primitive p, std::source_location location = std::source_location::current()) {

    int posX = (gc.w / 2) + p.pos.x;
    int posY = (gc.h / 2) + p.pos.y;

    auto sel = IsPrimitiveSelected(posX, posY, gc.hWnd);

    if (sel) {
        LivePT::OpenFileAndMoveCursorToLocation(location);
    }

    BOOL highlight = LivePT::IsCursorInsideFunctionCall(location);

    // 1. Создаем уникальную кисть для тела примитива
    HBRUSH hBodyBrush = CreateSolidBrush(RGB(p.color.r, p.color.g, p.color.b));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(gc.memDC, hBodyBrush);

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
    GetWorldTransform(gc.memDC, &oldForm);
    SetWorldTransform(gc.memDC, &xForm);

    if (p.show)
    {
        switch (p.type) {
        case ptype::circle:   Ellipse(gc.memDC, -p.size.w, -p.size.h, p.size.w, p.size.h); break;
        case ptype::box:      Rectangle(gc.memDC, -p.size.w, -p.size.h, p.size.w, p.size.h); break;
        case ptype::roundbox: RoundRect(gc.memDC, -p.size.w, -p.size.h, p.size.w, p.size.h, 40, 40); break;
        }
    }

    // Восстанавливаем исходную системную кисть и удаляем кисть тела
    SelectObject(gc.memDC, hOldBrush);
    DeleteObject(hBodyBrush);

    // 2. Создаем изолированную кисть для центрального маркера подсветки
    HBRUSH hMarkerBrush = highlight ? CreateSolidBrush(RGB(255, 255, 255)) : CreateSolidBrush(RGB(122, 122, 122));
    HBRUSH hOldMarkerBrush = (HBRUSH)SelectObject(gc.memDC, hMarkerBrush);

    Ellipse(gc.memDC, -12, -12, 12, 12);

    // Восстанавливаем контекст устройства и удаляем только маркер
    SelectObject(gc.memDC, hOldMarkerBrush);
    DeleteObject(hMarkerBrush);

    SetWorldTransform(gc.memDC, &oldForm);

    drawCounter++;
}

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

        RECT r;
        GetClientRect(hwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        gc = { hwnd, memDC, w, h };

        SetGraphicsMode(memDC, GM_ADVANCED);

        HBRUSH hDarkBrush = CreateSolidBrush(RGB(30, 30, 32));
        FillRect(memDC, &r, hDarkBrush);
        DeleteObject(hDarkBrush);

        drawCounter = 0;

        float x = eval(292.0f);
        float y = eval(-1.0f);

        float rr = rand() % 20 + eval(191);

        Draw((Primitive{
                .pos = pos2{
                    .x = 486.31f,
                    .y = -432.24f
                    },
                .size = size2{236.06f, rr},
                .Angle = angle(-10.5f),
                .type = eval(ptype::box),
                .show = true,
                .color = color3{0, 172, 50}
            }));



        Draw(eval(Primitive{
            .pos = pos2{
				.x = -82.69f,
				.y = 67.76f
				},
            .size = size2{136.06f, rr},
            .Angle = angle(397.9f),
            .type = ptype::roundbox,
            .show = true,
            .color = color3{26, 161, 21}
            }));

  

        DrawLine({
            .points = eval(std::vector<HeavyPoint>{
                    { true, pos2{-531.70f, 315.70f}, 58, 944, color3{247, 61, 125} },
                    { true, pos2{-246.80f, 317.80f}, 20, 956, color3{95, 35, 59} },
                    { true, pos2{-9.40f, 142.80f}, 21, 956, color3{75, 34, 99} },
                    { true, pos2{188.40f, 310.20f}, 19, 956, color3{73, 56, 158} },
                    { true, pos2{373.40f, 616.20f}, 15, 956, color3{103, 82, 183} },
                    { true, pos2{454.40f, 366.20f}, 15, 956, color3{103, 82, 183} },
                })
            });

        DrawTestRect({
                .pos = pos2{
                .x = 592.58f,
                .y = 64.87f
                },
                .size = size2{109.45f, 144.41f},
                .Angle = angle(66.0f),
                .type = ptype::box,
                .show = true,
                .color = color3{84, 128, 0},
                .fill = true
            });

        DrawTestRect({
                .pos = pos2{
                    .x = -436.00f,
                    .y = -242.13f
                    },
                .size = size2{134.95f, 145.87f},
                .Angle = angle(228.0f),
                .type = ptype::roundbox,
                .show = false,
                .color = color3{18, 19, 114},
                .fill = true
            });

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

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

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};

    while (true) {

        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            LivePT::ProcessEdit();// don't forget this call
        }
    }

    return 0;
}
