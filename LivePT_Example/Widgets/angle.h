#pragma once
#include <windows.h>
#include <cmath>
#include <functional>
#include <string>
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
        bool isCursorHidden = false;

        const int width = 120;
        const int height = 120;
        const int centerX = 60;
        const int centerY = 60;
        const int radius = 50;
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

    inline void ProcessRadarClick(int mouseX, int mouseY) {
        int dx = mouseX - g_angleCtx.centerX;
        int dy = mouseY - g_angleCtx.centerY;

        if (dx == 0 && dy == 0) return;

        float angleRad = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
        float targetDeg = angleRad * (180.0f / 3.14159265f);

        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
            float diff = targetDeg - g_angleCtx.currentAngle;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;
            g_angleCtx.currentAngle += (diff > 0 ? 1.0f : -1.0f);
        }
        else {
            g_angleCtx.currentAngle = targetDeg;
        }

        UpdateAngleCode();
    }
}
namespace Widgets {

    inline LRESULT CALLBACK AnglePickerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SETCURSOR:
            if (g_angleCtx.isTracking) {
                SetCursor(NULL);
                return TRUE;
            }
            SetCursor(LoadCursor(NULL, IDC_CROSS));
            return TRUE;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
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

            HPEN hRadarPen = CreatePen(PS_SOLID, 1, RGB(55, 55, 58));
            HPEN oldPen = (HPEN)SelectObject(memDC, hRadarPen);
            HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hNull);

            Ellipse(memDC, g_angleCtx.centerX - g_angleCtx.radius, g_angleCtx.centerY - g_angleCtx.radius,
                g_angleCtx.centerX + g_angleCtx.radius, g_angleCtx.centerY + g_angleCtx.radius);
            Ellipse(memDC, g_angleCtx.centerX - g_angleCtx.radius / 2, g_angleCtx.centerY - g_angleCtx.radius / 2,
                g_angleCtx.centerX + g_angleCtx.radius / 2, g_angleCtx.centerY + g_angleCtx.radius / 2);

            MoveToEx(memDC, g_angleCtx.centerX - g_angleCtx.radius, g_angleCtx.centerY, NULL);
            LineTo(memDC, g_angleCtx.centerX + g_angleCtx.radius, g_angleCtx.centerY);
            MoveToEx(memDC, g_angleCtx.centerX, g_angleCtx.centerY - g_angleCtx.radius, NULL);
            LineTo(memDC, g_angleCtx.centerX, g_angleCtx.centerY + g_angleCtx.radius);

            SelectObject(memDC, hOldB);
            SelectObject(memDC, oldPen);
            DeleteObject(hRadarPen);

            HPEN hNeedlePen = CreatePen(PS_SOLID, 2, RGB(110, 110, 115));
            oldPen = (HPEN)SelectObject(memDC, hNeedlePen);

            float rad = g_angleCtx.currentAngle * (3.14159265f / 180.0f);
            int needleX = g_angleCtx.centerX + static_cast<int>(std::cos(rad) * g_angleCtx.radius);
            int needleY = g_angleCtx.centerY + static_cast<int>(std::sin(rad) * g_angleCtx.radius);

            MoveToEx(memDC, g_angleCtx.centerX, g_angleCtx.centerY, NULL);
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
            int dx = pt.x - g_angleCtx.centerX;
            int dy = pt.y - g_angleCtx.centerY;
            float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));

            if (dist <= g_angleCtx.radius + 6) {
                g_angleCtx.isTracking = true;
                SetCapture(hwnd);
                if (!g_angleCtx.isCursorHidden) {
                    g_angleCtx.isCursorHidden = true;
                }
                ProcessRadarClick(pt.x, pt.y);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_angleCtx.isTracking) {
                POINT pt = { LOWORD(lParam), HIWORD(lParam) };
                ProcessRadarClick(pt.x, pt.y);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_angleCtx.isTracking) {
                g_angleCtx.isTracking = false;
                ReleaseCapture();
                if (g_angleCtx.isCursorHidden) {
                    ShowCursor(TRUE);
                    g_angleCtx.isCursorHidden = false;
                }
                SetCursor(LoadCursor(NULL, IDC_CROSS));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_DESTROY:
            if (g_angleCtx.isCursorHidden) {
                ShowCursor(TRUE);
                g_angleCtx.isCursorHidden = false;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            g_angleCtx.hWindow = NULL;
            return 0;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
}
namespace Widgets {

    template <typename T>
    inline void angleCallback(const std::any& anyValue, std::function<void(std::string)> vsUpdater) {
        const T* pInstance = std::any_cast<T>(&anyValue);
        if (!pInstance) return;

        float valExtracted = 0.0f;

        if constexpr (std::is_class_v<T>) {
            auto [val] = *pInstance;
            valExtracted = static_cast<float>(val);
        }
        else {
            valExtracted = static_cast<float>(*pInstance);
        }

        g_angleCtx.currentAngle = valExtracted;
        g_angleCtx.vsUpdaterCallback = vsUpdater;

        g_angleCtx.textGenerator = [vsUpdater](float finalAngle) {
            char buf[64]{};
            std::string typeName = typeid(T).name();
            if (typeName.rfind("struct ", 0) == 0) typeName = typeName.substr(7);
            if (typeName.rfind("class ", 0) == 0)  typeName = typeName.substr(6);

            if constexpr (std::is_fundamental_v<T>) {
                if (typeName == "float" || typeName == "double") {
                    typeName = "angle";
                }
                sprintf_s(buf, "%s(%.1ff)", typeName.c_str(), finalAngle);
            }
            else {
                using FieldType = decltype(valExtracted);
                if constexpr (std::is_floating_point_v<FieldType>) {
                    sprintf_s(buf, "%s{%.1ff}", typeName.c_str(), finalAngle);
                }
                else {
                    sprintf_s(buf, "%s{%d}", typeName.c_str(), static_cast<int>(finalAngle));
                }
            }
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

        int calculatedWindowY = mousePos.y - (g_angleCtx.height + 14);

        g_angleCtx.hWindow = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className, NULL,
            WS_POPUP | WS_VISIBLE,
            mousePos.x - (g_angleCtx.width / 2), calculatedWindowY, g_angleCtx.width, g_angleCtx.height,
            NULL, NULL, hInst, NULL
        );

        if (!g_angleCtx.hWindow) return;

        g_angleCtx.hRadarStatic = CreateWindowExA(
            0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, g_angleCtx.width, g_angleCtx.height, g_angleCtx.hWindow, NULL, hInst, NULL
        );

        SetForegroundWindow(g_angleCtx.hWindow);
        SetFocus(g_angleCtx.hWindow);
    }
}
