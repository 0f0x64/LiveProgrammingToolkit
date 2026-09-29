#pragma once
#include <windows.h>
#include <cmath>
#include <functional>
#include <string>
#include <any>
#include <type_traits>

// Базовая структура, которая используется внутри WinAPI для отрисовки

namespace Widgets {

    struct PosPickerContext {
        HWND hWindow = NULL;
        HWND hPadStatic = NULL;

        pos2 currentPos = { 0.0f, 0.0f };
        std::function<void(std::string)> vsUpdaterCallback = nullptr;
        std::function<void(float, float)> textGenerator = nullptr; // Динамический генератор текста кода

        bool isTracking = false;
        bool isCursorHidden = false;
        POINT lastMousePos = { 0, 0 };

        const int width = 140;
        const int height = 140;
    };

    static PosPickerContext g_posCtx;

    inline void UpdatePosCode() {
        if (g_posCtx.textGenerator) {
            // Вызываем генератор, который помнит исходный тип структуры и правила ее форматирования
            g_posCtx.textGenerator(g_posCtx.currentPos.x, g_posCtx.currentPos.y);
        }
        InvalidateRect(g_posCtx.hWindow, NULL, FALSE);
    }

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

            HBRUSH hBg = CreateSolidBrush(RGB(30, 30, 32));
            FillRect(memDC, &lpDrawItem->rcItem, hBg);
            DeleteObject(hBg);

            HPEN hMutedPen = CreatePen(PS_SOLID, 1, RGB(55, 55, 58));
            HPEN oldPen = (HPEN)SelectObject(memDC, hMutedPen);

            for (int i = 20; i < w; i += 20) {
                MoveToEx(memDC, i, 0, NULL); LineTo(memDC, i, h);
                MoveToEx(memDC, 0, i, NULL); LineTo(memDC, w, i);
            }

            int cx = w / 2;
            int cy = h / 2;
            MoveToEx(memDC, cx - 8, cy, NULL); LineTo(memDC, cx + 8, cy);
            MoveToEx(memDC, cx, cy - 8, NULL); LineTo(memDC, cx, cy + 8);

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

    template <typename T>
    inline void pos2Callback(const std::any& anyValue, std::function<void(std::string)> vsUpdater) {
        const T* pInstance = std::any_cast<T>(&anyValue);
        if (!pInstance) return;

        // 1. Автоматическая распаковка полей структуры (будь то x/y, w/h или a/b)
        auto [val1, val2] = *pInstance;

        // 2. Инициализация состояния
        g_posCtx.currentPos.x = static_cast<float>(val1);
        g_posCtx.currentPos.y = static_cast<float>(val2);
        g_posCtx.vsUpdaterCallback = vsUpdater;

        // 3. Создаем лямбду, которая запоминает исходный тип данных T
        g_posCtx.textGenerator = [vsUpdater](float newX, float newY) {
            char buf[128]{};
            using FieldType = decltype(val1);

            // Получаем чистое имя типа структуры для подстановки в строку кода
            std::string typeName = typeid(T).name();
            if (typeName.rfind("struct ", 0) == 0) typeName = typeName.substr(7);
            if (typeName.rfind("class ", 0) == 0)  typeName = typeName.substr(6);

            // Компилятор сам выберет нужный формат строки в зависимости от типа полей
            if constexpr (std::is_floating_point_v<FieldType>) {
                sprintf_s(buf, "%s{%.2ff,%.2ff}", typeName.c_str(), newX, newY);
            }
            else {
                sprintf_s(buf, "%s{%d,%d}", typeName.c_str(), static_cast<int>(newX), static_cast<int>(newY));
            }

            vsUpdater(buf);
            };

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
