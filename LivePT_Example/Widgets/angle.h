#pragma once
#include <windows.h>
#include <windowsx.h>
#include <cmath>
#include <functional>
#include <string>
#include <sstream>
#include <any>
#include <type_traits>

namespace Widgets {

    struct AnglePickerContext {
        HWND hWindow = NULL;
        HWND hRadarStatic = NULL;

        float currentAngle = 0.0f;
        std::function<void(std::string)> vsUpdaterCallback = nullptr;
        std::function<void(float)> textGenerator = nullptr;

        bool isTracking = false;

        // ЭТАЛОННЫЕ БАЗОВЫЕ РАЗМЕРЫ (для 100% масштаба / 96 DPI)
        const int BASE_width = 120;
        const int BASE_height = 120;
        const int BASE_centerX = 60;
        const int BASE_centerY = 60;
        const int BASE_radius = 50;
    };

    static AnglePickerContext g_angleCtx;

    inline void UpdateAngleCode() {
        float visualAngle = std::fmod(g_angleCtx.currentAngle, 360.0f);
        if (visualAngle < 0.0f) visualAngle += 360.0f;

        if (g_angleCtx.textGenerator) {
            g_angleCtx.textGenerator(visualAngle);
        }
        InvalidateRect(g_angleCtx.hWindow, NULL, FALSE);
    }

    inline void ProcessRadarClick(HWND hwnd, int mouseX, int mouseY) {
        // Динамически вычисляем центр и масштаб на основе переданного HWND окна
        float scale = GetWidgetScale(hwnd);
        int currentCenterX = static_cast<int>(g_angleCtx.BASE_centerX * scale);
        int currentCenterY = static_cast<int>(g_angleCtx.BASE_centerY * scale);

        int dx = mouseX - currentCenterX;
        int dy = mouseY - currentCenterY;

        if (dx == 0 && dy == 0) return;

        float angleRad = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
        float targetDeg = angleRad * (180.0f / 3.14159265f);

        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
            float step = 15;
            g_angleCtx.currentAngle = floor(targetDeg / step + .5) * step;
        }
        else {
            g_angleCtx.currentAngle = targetDeg;
        }

        UpdateAngleCode();
    }

    inline LRESULT CALLBACK AnglePickerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_CROSS));
            return TRUE;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_angleCtx.isTracking = false;
                ReleaseCapture();
                DestroyWindow(hwnd);
                g_angleCtx.hWindow = NULL;
                return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                DestroyWindow(hwnd);
                g_angleCtx.hWindow = NULL;
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

            // РАСЧЕТ ДИНАМИЧЕСКИХ РАЗМЕРОВ ДЛЯ ОТРИСОВКИ С УЧЕТОМ ТЕКУЩЕГО DPI
            float scale = GetWidgetScale(hwnd);
            int cx = static_cast<int>(g_angleCtx.BASE_centerX * scale);
            int cy = static_cast<int>(g_angleCtx.BASE_centerY * scale);
            int r = static_cast<int>(g_angleCtx.BASE_radius * scale);

            HPEN hRadarPen = CreatePen(PS_SOLID, 1, RGB(55, 55, 58));
            HPEN oldPen = (HPEN)SelectObject(memDC, hRadarPen);
            HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hNull);

            // Отрисовка шкал радара по отмасштабированным координатам
            Ellipse(memDC, cx - r, cy - r, cx + r, cy + r);
            Ellipse(memDC, cx - r / 2, cy - r / 2, cx + r / 2, cy + r / 2);

            MoveToEx(memDC, cx - r, cy, NULL);
            LineTo(memDC, cx + r, cy);
            MoveToEx(memDC, cx, cy - r, NULL);
            LineTo(memDC, cx, cy + r);

            SelectObject(memDC, hOldB);
            SelectObject(memDC, oldPen);
            DeleteObject(hRadarPen);

            HPEN hNeedlePen = CreatePen(PS_SOLID, 2, RGB(110, 110, 115));
            oldPen = (HPEN)SelectObject(memDC, hNeedlePen);

            float rad = g_angleCtx.currentAngle * (3.14159265f / 180.0f);
            int needleX = cx + static_cast<int>(std::cos(rad) * r);
            int needleY = cy + static_cast<int>(std::sin(rad) * r);

            MoveToEx(memDC, cx, cy, NULL);
            LineTo(memDC, needleX, needleY);

            SelectObject(memDC, oldPen);
            DeleteObject(hNeedlePen);

            RECT borderRect = { 0, 0, w, h };
            FrameRect(memDC, &borderRect, (HBRUSH)GetStockObject(NULL_BRUSH));

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBM);
            DeleteObject(memBM);
            DeleteDC(memDC);
            return TRUE;
        }
        case WM_LBUTTONDOWN: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };

            float scale = GetWidgetScale(hwnd);
            int cx = static_cast<int>(g_angleCtx.BASE_centerX * scale);
            int cy = static_cast<int>(g_angleCtx.BASE_centerY * scale);
            int r = static_cast<int>(g_angleCtx.BASE_radius * scale);

            int dx = pt.x - cx;
            int dy = pt.y - cy;
            float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));

            // Скейлим зону валидного клика (+6 пикселей скейлятся пропорционально)
            if (dist <= r + static_cast<int>(6 * scale)) {
                g_angleCtx.isTracking = true;
                SetCapture(hwnd);
                ProcessRadarClick(hwnd, pt.x, pt.y);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_angleCtx.isTracking) {
                POINT pt = { GET_X_LPARAM(lParam),  GET_Y_LPARAM(lParam) };
                ProcessRadarClick(hwnd, pt.x, pt.y);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_angleCtx.isTracking) {
                g_angleCtx.isTracking = false;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_DESTROY:
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            g_angleCtx.hWindow = NULL;
            return 0;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    inline void angleCallback(const std::any& anyValue, std::function<void(std::string)> vsUpdater) {
        const std::string* pRawText = std::any_cast<std::string>(&anyValue);
        if (!pRawText) return;

        std::string cleanText = *pRawText;
        while (!cleanText.empty() && (cleanText.back() == 'f' || cleanText.back() == 'F')) {
            cleanText.pop_back();
        }

        float valExtracted = 0.0f;
        std::stringstream ss(cleanText);
        ss >> valExtracted;

        g_angleCtx.currentAngle = valExtracted;
        g_angleCtx.vsUpdaterCallback = vsUpdater;

        g_angleCtx.textGenerator = [vsUpdater](float finalAngle) {
            char buf[64]{};
            sprintf_s(buf, "%.1ff", finalAngle);
            vsUpdater(buf);
            };

        if (g_angleCtx.hWindow && IsWindow(g_angleCtx.hWindow)) {
            InvalidateRect(g_angleCtx.hWindow, NULL, FALSE);
            SetActiveWindow(g_angleCtx.hWindow);
            return;
        }

        POINT mousePos;
        GetCursorPos(&mousePos);
        HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_CustomAngleRadarWin";

        static bool registered = [hInst, className]() {
            WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
            wc.lpfnWndProc = AnglePickerWndProc;
            wc.hInstance = hInst;
            wc.lpszClassName = className;
            wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 32));
            return RegisterClassExA(&wc) != 0;
            }();

        // ДИНАМИЧЕСКИЙ РАСЧЕТ РАЗМЕРОВ ОКНА ПРИ ИНИЦИАЛИЗАЦИИ
        float scale = GetWidgetScale();
        int scaledWidth = static_cast<int>(g_angleCtx.BASE_width * scale);
        int scaledHeight = static_cast<int>(g_angleCtx.BASE_height * scale);

        // Корректируем смещение окна по Y с учетом скейла
        int calculatedWindowY = mousePos.y - (scaledHeight + static_cast<int>(14 * scale));

        g_angleCtx.hWindow = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className, NULL,
            WS_POPUP | WS_VISIBLE,
            mousePos.x - (scaledWidth / 2),
            calculatedWindowY,
            scaledWidth,
            scaledHeight,
            NULL, NULL, hInst, NULL
        );

        if (!g_angleCtx.hWindow) return;

        // Создаем дочерний элемент STATIC с применением масштабированных габаритов
        g_angleCtx.hRadarStatic = CreateWindowExA(
            0, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, scaledWidth, scaledHeight,
            g_angleCtx.hWindow, NULL, hInst, NULL
        );
        SetForegroundWindow(g_angleCtx.hWindow);
        SetFocus(g_angleCtx.hWindow);
    }
}