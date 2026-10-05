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


void Draw(Primitive p, std::source_location location = std::source_location::current()) {

    int posX = (gc.w / 2) + p.pos.x;
    int posY = (gc.h / 2) + p.pos.y;

    auto sel = IsPrimitiveSelected(posX,posY, gc.hWnd);

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

        Draw({
            .pos =      eval(pos2{
				.x = -137.24f,
				.y = -264.18f
				}),
            .size =     eval(size2{154, 157}),
            .Angle =    eval(angle(110.0f)),
            .type =     eval(ptype::circle),
            .show =     eval(true),
            .color = eval(color3{
				.r=99, 
				.g=48,  
				.b=31
				}) 
         });

        Draw({
            .pos =      eval(pos2{268.01f, 14.10f}),
            .size =     eval(size2{101.89f,94.56f}),
            .Angle =    eval(angle(111.7f)),
            .type =     eval(ptype::box),
            .show =     eval(true),
            .color =    eval(color3{102, 129, 42})
        });

        Draw({
            .pos =      eval(pos2{-24.90f, 189.16f}),
            .size =     eval(size2{168.1575f, 78.1812f}),
            .Angle =    eval(angle(54.3f)),
            .type =     eval(ptype::roundbox),
            .show =     eval(true),
            .color =    eval(color3{45, 180, 71})
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

    //LivePT::InitLivePTCallbacks();
    
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
