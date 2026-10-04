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
            LivePT::EndUndoTransaction();
            LivePT::SaveActiveDocument();

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

    inline void pos2Callback(const std::any& anyValue, std::function<void(std::string)> vsUpdater) {
        const std::string* pRawText = std::any_cast<std::string>(&anyValue);
        if (!pRawText) return;

        std::string srcText = *pRawText;

        // 1. ПРЕЦИЗИОННЫЙ АНАЛИЗ РАЗМЕТКИ ПОЛЬЗОВАТЕЛЯ
        bool isMultiLine = (srcText.find('\n') != std::string::npos || srcText.find('\r') != std::string::npos);
        bool hasExplicitX = (srcText.find(".x") != std::string::npos || srcText.find("x:") != std::string::npos);
        bool hasExplicitY = (srcText.find(".y") != std::string::npos || srcText.find("y:") != std::string::npos);

        // Автоопределение формата чисел (флоаты с точкой/суффиксом или целые инты)
        bool isFloat = (srcText.find('.') != std::string::npos || srcText.find('f') != std::string::npos || srcText.find('F') != std::string::npos);

        // 2. ДЕСЕРИАЛИЗАЦИЯ: Выдергиваем числа из любого многострочного хаоса
        size_t openIdx = srcText.find_first_of("{(");
        size_t closeIdx = srcText.find_last_of("})");
        std::string inner = (openIdx != std::string::npos && closeIdx != std::string::npos && closeIdx > openIdx)
            ? srcText.substr(openIdx + 1, closeIdx - openIdx - 1)
            : srcText;

        std::stringstream ss(inner); std::string token;
        float val1 = 0.0f, val2 = 0.0f; int idx = 0;

        while (std::getline(ss, token, ',')) {
            size_t eqPos = token.find('=');
            if (eqPos == std::string::npos) eqPos = token.find(':');
            std::string valPart = (eqPos != std::string::npos) ? token.substr(eqPos + 1) : token;

            valPart.erase(0, valPart.find_first_not_of(" \t\r\n.xyab="));
            valPart.erase(valPart.find_last_not_of(" \t\r\nFfuUlL") + 1);
            if (valPart.empty()) continue;

            float val = 0.0f;
            std::from_chars(valPart.data(), valPart.data() + valPart.size(), val);
            if (idx == 0) val1 = val; else if (idx == 1) val2 = val;
            idx++;
        }

        g_posCtx.currentPos.x = val1;
        g_posCtx.currentPos.y = val2;
        g_posCtx.vsUpdaterCallback = vsUpdater;

        // 3. НЕУБИВАЕМЫЙ ПОКАДРОВЫЙ ГЕНЕРАТОР: Защищает вид агрегата от разрушения!
        g_posCtx.textGenerator = [vsUpdater, isMultiLine, hasExplicitX, hasExplicitY, isFloat](float newX, float newY) {
            char buf[512]{};
            const char* numFmt = isFloat ? "%.2ff" : "%.0f";

            if (isMultiLine) {
                std::string xField = hasExplicitX ? ".x = " : "";
                std::string yField = hasExplicitY ? ".y = " : "";
                // Убрали внешние скобки, оставили только внутреннюю структуру и табы
                std::string masterTemplate = "\n\t\t\t\t" + xField + numFmt + ",\n\t\t\t\t" + yField + numFmt + "\n\t\t\t\t";
                sprintf_s(buf, masterTemplate.c_str(), newX, newY);
            }
            else {
                if (hasExplicitX || hasExplicitY) {
                    std::string masterTemplate = (hasExplicitX ? ".x = " : "x = ") + std::string(numFmt) + ", " + (hasExplicitY ? ".y = " : "y = ") + numFmt;
                    sprintf_s(buf, masterTemplate.c_str(), newX, newY);
                }
                else {
                    std::string masterTemplate = std::string(numFmt) + ", " + numFmt;
                    sprintf_s(buf, masterTemplate.c_str(), newX, newY);
                }
            }
            vsUpdater(buf);
            };


        // Твой оригинальный Win32-код окна трекпада...
        if (g_posCtx.hWindow && IsWindow(g_posCtx.hWindow)) {
            SetActiveWindow(g_posCtx.hWindow); return;
        }
        POINT mousePos; GetCursorPos(&mousePos); HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_Custom2DTrackpadWin";
        static bool registered = [hInst, className]() {
            WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) }; wc.lpfnWndProc = PosPickerWndProc;
            wc.hInstance = hInst; wc.lpszClassName = className;
            wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 32)); return RegisterClassExA(&wc) != 0;
            }();
        int calculatedWindowY = mousePos.y - (g_posCtx.height + 14);
        g_posCtx.hWindow = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, className, NULL, WS_POPUP | WS_VISIBLE,
            mousePos.x - (g_posCtx.width / 2), calculatedWindowY, g_posCtx.width, g_posCtx.height, NULL, NULL, hInst, NULL);
        if (!g_posCtx.hWindow) return;
        g_posCtx.hPadStatic = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, g_posCtx.width, g_posCtx.height, g_posCtx.hWindow, NULL, hInst, NULL);
        SetForegroundWindow(g_posCtx.hWindow); SetFocus(g_posCtx.hWindow);
    }
}
