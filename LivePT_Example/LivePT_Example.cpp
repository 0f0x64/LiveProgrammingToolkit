#include <windows.h>
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

#define LPT_LIVE_FUNCTION(ReturnType, FuncName, StructType) \
    ReturnType FuncName##_Live(StructType r, int runtimeId, std::source_location location); \
    template <typename T = StructType> \
    inline ReturnType FuncName(T&& r, const char* file = __builtin_FILE(), int line = __builtin_LINE(), int col = __builtin_COLUMN(), std::source_location loc = std::source_location::current()) { \
        std::string absPath = LivePT::NormalizePath(file); \
        int rId = LivePT::RegisterEvalPreMain(absPath.c_str(), #FuncName, std::any{ r }, line, col, LivePT::EnumTypeDesc{ false }); \
        \
        /* ГАРАНТИРУЕМ ИНИЦИАЛИЗАЦИЮ ПАРСЕРА СТРУКТУРЫ БЕЗ МАКРОСА EVAL */ \
        if (rId >= 0 && rId < static_cast<int>(LivePT::paramDesc.size()) && !LivePT::paramDesc[rId].loaded) { \
            LivePT::paramDesc[rId].value = std::any{ static_cast<StructType>(r) }; \
            LivePT::paramDesc[rId].stringUpdater = [rId](std::any& targetAny, const std::string& textValue) { \
                std::string innerArgs; \
                size_t openBrace = textValue.find('{'); \
                size_t closeBrace = textValue.rfind('}'); \
                if (openBrace != std::string::npos && closeBrace != std::string::npos && closeBrace > openBrace) { \
                    std::string rawInner = textValue.substr(openBrace + 1, closeBrace - openBrace - 1); \
                    innerArgs.reserve(rawInner.length()); \
                    bool inComment = false; \
                    for (size_t i = 0; i < rawInner.length(); ++i) { \
                        char c = rawInner[i]; \
                        char nextC = (i + 1 < rawInner.length()) ? rawInner[i + 1] : '\0'; \
                        if (inComment) { if (c == '\n' || c == '\r') inComment = false; continue; } \
                        if ((c == '/' && nextC == '/') || c == '#') { inComment = true; if (c == '/') i++; continue; } \
                        innerArgs += c; \
                    } \
                } else { \
                    if (!LivePT::ExtractAndStripComments(textValue, innerArgs)) return; \
                } \
                StructType* pStructInstance = std::any_cast<StructType>(&targetAny); \
                if (!pStructInstance) return; \
                char* byteBase = reinterpret_cast<char*>(pStructInstance); \
                std::string mainTypeName = typeid(StructType).name(); \
                if (mainTypeName.rfind("struct ", 0) == 0) mainTypeName.erase(0, 7); \
                if (mainTypeName.rfind("class ", 0) == 0) mainTypeName.erase(0, 6); \
                LivePT::WriteStructBytes(byteBase, mainTypeName, innerArgs, 0, rId, sizeof(StructType)); \
            }; \
            LivePT::paramDesc[rId].loaded = true; \
        } \
        \
        /* АВТОМАТИЧЕСКИЙ ПРОБРОС ИЗ КЭША В ПЕРЕМЕННУЮ 'RAW_R' ПЕРЕД ВЫЗОВОМ */ \
        StructType live_r = std::forward<T>(r); \
        if (rId >= 0 && rId < static_cast<int>(LivePT::paramDesc.size())) { \
            std::string actualKey = absPath + ":" + std::to_string(LivePT::paramDesc[rId].counterID); \
            int real_id = LivePT::getID(actualKey); \
            if (real_id >= 0 && real_id < static_cast<int>(LivePT::paramDesc.size()) && LivePT::paramDesc[real_id].loaded) { \
                if (auto pCachedVal = std::any_cast<StructType>(&LivePT::paramDesc[real_id].value)) { \
                    char* targetBase = reinterpret_cast<char*>(&live_r); \
                    const char* cacheBase = reinterpret_cast<const char*>(pCachedVal); \
                    size_t totalSz = sizeof(StructType); \
                    const auto& mask = LivePT::paramDesc[real_id].validFieldsMask; \
                    if (mask.size() == totalSz) { \
                        for (size_t b = 0; b < totalSz; ++b) { \
                            if (mask[b]) targetBase[b] = cacheBase[b]; \
                        } \
                    } \
                } \
            } \
        } \
        return FuncName##_Live(std::move(live_r), rId, loc); \
    } \
    ReturnType FuncName##_Live(StructType r, int runtimeId, std::source_location location)


struct TestRect {
    pos2 pos;
    size2 size;
    angle Angle;
    color3 color;
    bool fill;
};


LPT_LIVE_FUNCTION(void, DrawTestRect, TestRect) {
 
    int posX = (gc.w / 2) + r.pos.x;
    int posY = (gc.h / 2) + r.pos.y;
    if (IsPrimitiveSelected(posX, posY, gc.hWnd)) {
        LivePT::OpenFileAndMoveCursorToLocation(location);
    }


    BOOL highlight = LivePT::IsCursorInsideFunctionCall(location);

    HBRUSH hBrush = r.fill ? CreateSolidBrush(RGB(r.color.r, r.color.g, r.color.b)) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN hPen = CreatePen(PS_SOLID, highlight ? 4 : 2, RGB(r.color.r, r.color.g, r.color.b));

    HBRUSH hOldBrush = (HBRUSH)SelectObject(gc.memDC, hBrush);
    HPEN hOldPen = (HPEN)SelectObject(gc.memDC, hPen);

    float radians = r.Angle * (3.14159265f / 180.0f);
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

    Rectangle(gc.memDC, -r.size.w, -r.size.h, r.size.w, r.size.h);

    SelectObject(gc.memDC, hOldBrush);
    SelectObject(gc.memDC, hOldPen);
    DeleteObject(hPen);
    if (r.fill) DeleteObject(hBrush);

    hBrush = highlight ? CreateSolidBrush(RGB(255, 255, 255)) : CreateSolidBrush(RGB(122, 122, 122));
    hOldBrush = (HBRUSH)SelectObject(gc.memDC, hBrush);
    Ellipse(gc.memDC, -12, -12, 12, 12);
    SelectObject(gc.memDC, hOldBrush);
    DeleteObject(hBrush);

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

    HBRUSH hBrush = CreateSolidBrush(RGB(p.color.r, p.color.g, p.color.b));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(gc.memDC, hBrush);

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

    SelectObject(gc.memDC, hOldBrush);
    DeleteObject(hBrush);

    //if (curSel == drawCounter || highlight)
    if (highlight)
    {
        hBrush = CreateSolidBrush(RGB(255, 255, 255));
    }
    else
    {
        hBrush = CreateSolidBrush(RGB(122, 122, 122));
    }
    hOldBrush = (HBRUSH)SelectObject(gc.memDC, hBrush);
    Ellipse(gc.memDC, -12, -12, 12, 12);
    SelectObject(gc.memDC, hOldBrush);
    DeleteObject(hBrush);


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

        Draw({
            .pos = pos2{
                .x = -55.00f,
                .y = -133.00f
                }
            ,
            .size = eval(size2{210.50f, 121.56f}),
            .Angle = eval(angle(110.6f)),
            .type = eval(ptype::box),
            .show = eval(true),
            .color = eval(color3{
        .r = 131,
        .g = 52,
        .b = 60
        })
            });

        Draw({
            .pos = eval(pos2{
				.x = -289.10f,
				.y = -335.99f
				}
            ),
            .size = eval(size2{221.59f, 128.58f}),
            .Angle = eval(angle(110.6f)),
            .type = eval(ptype::box),
            .show = eval(true),
            .color = eval(color3{
				.r=66, 
				.g=75,  
				.b=138
				})
            });

        float rr = rand() % 20 + 66;

        Draw(eval(Primitive{
            .pos = pos2{
				.x = 273.31f,
				.y = -444.24f
				},
            .size = size2{211.06f, rr},
            .Angle = angle(-13.5f),
            .type = ptype::circle,
            .show = true,
            .color = color3{0, 172, 50}
            }));




        DrawLine({
            .points = eval(std::vector<HeavyPoint>{
                    { true, pos2{-430.70f, 223.70f}, 58, 944, color3{239, 61, 125} },
                    { true, pos2{-261.80f, 319.80f}, 20, 956, color3{95, 35, 59} },
                    { true, pos2{-9.40f, 142.80f}, 21, 956, color3{75, 34, 99} },
                    { true, pos2{154.40f, 262.20f}, 19, 956, color3{73, 56, 158} },
                    { true, pos2{257.40f, 71.20f}, 19, 956, color3{72, 60, 125} },
                    { true, pos2{406.40f, 176.20f}, 30, 956, color3{60, 50, 120} },
                    { true, pos2{497.40f, 356.20f}, 15, 956, color3{103, 82, 183} },
                    { true, pos2{409.40f, 506.20f}, 15, 956, color3{103, 82, 183} },
                })
            });







        DrawTestRect({
                .pos = pos2{
				    .x = 372.00f,
				    .y = -151.13f
				    },
                .size = size2{86.33f, 113.94f},
                .Angle = angle(45.0f),
                .color = color3{220, 76, 153},
                .fill = true
            });

        DrawTestRect({
                .pos = pos2{
                    .x = -190.00f,
                    .y = 78.87f
                    },
                .size = size2{80.81f, 50.47f},
                .Angle = angle(45.0f),
                .color = color3{20, 118, 220},
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
