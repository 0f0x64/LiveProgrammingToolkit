// posPicker.h — Часть 1 (Логика трекпада)
#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include <cmath>

// Макрос авторегистрации для типа pos2 (вызывается глобально под структурой)
#define LPT_REGISTER_POS(Type, Callback) \
    namespace LivePT { void MyPosPickerCallback(const pos2&, std::function<void(std::string)>); } \
    static inline bool _lpt_init_##Type = []() { \
        LivePT::RegisterTypeDoubleClickCallback<Type>(Callback); \
        return true; \
    }();

namespace LivePT {

    struct PosPickerContext {
        HWND hWindow = NULL;
        HWND hPadStatic = NULL;

        pos2 currentPos = { 0.0f, 0.0f };
        std::function<void(std::string)> vsUpdaterCallback = nullptr;

        bool isTracking = false;
        bool isCursorHidden = false; // Флаг для контроля состояния курсора
        POINT lastMousePos = { 0, 0 };

        const int width = 140;
        const int height = 140;
    };

    static PosPickerContext g_posCtx;

    inline void UpdatePosCode() {
        if (g_posCtx.vsUpdaterCallback) {
            char buf[64]{};
            sprintf_s(buf, "pos2{%.2ff,%.2ff}", g_posCtx.currentPos.x, g_posCtx.currentPos.y);
            g_posCtx.vsUpdaterCallback(buf);
        }
        InvalidateRect(g_posCtx.hWindow, NULL, FALSE);
    }
}
// posPicker.h — Часть 2 (Интерфейс Win32 API)
// posPicker.h — Часть 2 (Интерфейс Win32 API)
namespace LivePT {

    inline LRESULT CALLBACK PosPickerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SETCURSOR: {
            if (g_posCtx.isTracking) {
                SetCursor(NULL);
                return TRUE;
            }
            SetCursor(LoadCursor(NULL, IDC_CROSS));
            return TRUE;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                g_posCtx.hWindow = NULL;
                return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                DestroyWindow(hwnd);
                g_posCtx.hWindow = NULL;
                return 0;
            }
            break;
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT)lParam;
            HDC hdc = lpDrawItem->hDC;
            int w = lpDrawItem->rcItem.right - lpDrawItem->rcItem.left;
            int h = lpDrawItem->rcItem.bottom - lpDrawItem->rcItem.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

            // ФОН: Глубокий матовый темно-серый цвет
            HBRUSH hBg = CreateSolidBrush(RGB(30, 30, 32));
            FillRect(memDC, &lpDrawItem->rcItem, hBg);
            DeleteObject(hBg);

            // ТОЧЕЧНЫЙ ФИКС: Создаем ОДНУ сплошную тонкую кисть нейтрального серого цвета
            // для всей графики (и для сетки, и для центрального перекрестия)
            HPEN hMutedPen = CreatePen(PS_SOLID, 1, RGB(55, 55, 58));
            HPEN oldPen = (HPEN)SelectObject(memDC, hMutedPen);

            // 1. Рисуем строгую однотонную сетку с шагом 20 пикселей (без пунктиров)
            for (int i = 20; i < w; i += 20) {
                MoveToEx(memDC, i, 0, NULL); LineTo(memDC, i, h);
                MoveToEx(memDC, 0, i, NULL); LineTo(memDC, w, i);
            }

            // 2. Рисуем центральное перекрестие прицела той же самой серой кистью
            int cx = w / 2;
            int cy = h / 2;
            MoveToEx(memDC, cx - 8, cy, NULL); LineTo(memDC, cx + 8, cy);
            MoveToEx(memDC, cx, cy - 8, NULL); LineTo(memDC, cx, cy + 8);

            // 3. Рисуем внешнюю контурную рамку окна для идеальной геометрии
            RECT borderRect = { 0, 0, w, h };
            FrameRect(memDC, &borderRect, (HBRUSH)GetStockObject(NULL_BRUSH));

            SelectObject(memDC, oldPen);
            DeleteObject(hMutedPen);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBM);
            DeleteObject(memBM);
            DeleteDC(memDC);
            return TRUE;
        }
        case WM_LBUTTONDOWN: {
            g_posCtx.isTracking = true;
            SetCapture(hwnd);
            GetCursorPos(&g_posCtx.lastMousePos);

            if (!g_posCtx.isCursorHidden) {
                ShowCursor(FALSE);
                g_posCtx.isCursorHidden = true;
            }

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_posCtx.isTracking) {
                POINT currentMouse;
                GetCursorPos(&currentMouse);

                int dx = currentMouse.x - g_posCtx.lastMousePos.x;
                int dy = currentMouse.y - g_posCtx.lastMousePos.y;

                if (dx != 0 || dy != 0) {
                    float speedScale = 1.0f;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) speedScale = 10.0f;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)   speedScale = 0.1f;

                    g_posCtx.currentPos.x += static_cast<float>(dx) * speedScale;
                    g_posCtx.currentPos.y += static_cast<float>(dy) * speedScale;

                    UpdatePosCode();
                }

                RECT rc; GetWindowRect(hwnd, &rc);
                int cx = rc.left + (rc.right - rc.left) / 2;
                int cy = rc.top + (rc.bottom - rc.top) / 2;

                g_posCtx.lastMousePos = { cx, cy };
                SetCursorPos(cx, cy);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_posCtx.isTracking) {
                g_posCtx.isTracking = false;
                ReleaseCapture();

                if (g_posCtx.isCursorHidden) {
                    ShowCursor(TRUE);
                    g_posCtx.isCursorHidden = false;
                }

                SetCursor(LoadCursor(NULL, IDC_CROSS));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_DESTROY:
            if (g_posCtx.isCursorHidden) {
                ShowCursor(TRUE);
                g_posCtx.isCursorHidden = false;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            g_posCtx.hWindow = NULL;
            return 0;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    inline void MyPosPickerCallback(const pos2& initialPos, std::function<void(std::string)> vsUpdater) {
        g_posCtx.currentPos = initialPos;
        g_posCtx.vsUpdaterCallback = vsUpdater;

        if (g_posCtx.hWindow && IsWindow(g_posCtx.hWindow)) {
            SetActiveWindow(g_posCtx.hWindow);
            return;
        }

        POINT mousePos;
        GetCursorPos(&mousePos);

        HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_Custom2DTrackpadWin";

        static bool registered = [hInst, className]() {
            WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
            wc.lpfnWndProc = PosPickerWndProc;
            wc.hInstance = hInst;
            wc.lpszClassName = className;
            wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 32));
            return RegisterClassExA(&wc) != 0;
            }();

        int calculatedWindowY = mousePos.y - (g_posCtx.height + 14);

        g_posCtx.hWindow = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className, NULL,
            WS_POPUP | WS_VISIBLE,
            mousePos.x - (g_posCtx.width / 2), calculatedWindowY, g_posCtx.width, g_posCtx.height,
            NULL, NULL, hInst, NULL
        );

        if (!g_posCtx.hWindow) return;

        g_posCtx.hPadStatic = CreateWindowExA(
            0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, g_posCtx.width, g_posCtx.height, g_posCtx.hWindow, NULL, hInst, NULL
        );

        SetForegroundWindow(g_posCtx.hWindow);
        SetFocus(g_posCtx.hWindow);
    }
}
