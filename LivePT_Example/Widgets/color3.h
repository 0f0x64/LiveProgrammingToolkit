#pragma once
#include <windows.h>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include <any>
#include <algorithm>
#include <type_traits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Widgets {

    struct ColorPickerContext {
        HWND hWindow = NULL;
        HWND hWheelStatic = NULL;
        HWND hValueStatic = NULL;

        color3 currentRGB = { 255, 255, 255 };
        std::function<void(std::string)> vsUpdaterCallback = nullptr;
        std::function<void(unsigned char, unsigned char, unsigned char)> textGenerator = nullptr;

        bool isTrackingWheel = false;
        bool isTrackingValue = false;

        float currentHue = 0.0f;
        float currentSat = 0.0f;
        float currentValue = 1.0f;

        const int centerX = 65;
        const int centerY = 65;
        const int radius = 58;

        const int valLeft = 135;
        const int valTop = 8;
        const int valWidth = 16;
        const int valHeight = 114;
    };

    static ColorPickerContext g_pickerCtx;
}
namespace Widgets {

    inline DWORD HsvToGdiColor(float H, float S, float V) {
        float r = 0, g = 0, b = 0;
        if (S == 0) {
            r = g = b = V;
        }
        else {
            float h = H / 60.0f;
            int i = static_cast<int>(std::floor(h));
            float f = h - i;
            float p = V * (1.0f - S);
            float q = V * (1.0f - S * f);
            float t = V * (1.0f - S * (1.0f - f));
            switch (i % 6) {
            case 0: r = V; g = t; b = p; break;
            case 1: r = q; g = V; b = p; break;
            case 2: r = p; g = V; b = t; break;
            case 3: r = p; g = q; b = V; break;
            case 4: r = t; g = p; b = V; break;
            case 5: r = V; g = p; b = q; break;
            }
        }
        return (static_cast<DWORD>(r * 255) << 16) |
            (static_cast<DWORD>(g * 255) << 8) |
            static_cast<DWORD>(b * 255);
    }

    inline color3 HsvToRgbStruct(float H, float S, float V) {
        DWORD gdiColor = HsvToGdiColor(H, S, V);
        return color3{
            static_cast<unsigned char>((gdiColor >> 16) & 0xFF),
            static_cast<unsigned char>((gdiColor >> 8) & 0xFF),
            static_cast<unsigned char>(gdiColor & 0xFF)
        };
    }

    inline void UpdateLiveCode() {
        g_pickerCtx.currentRGB = HsvToRgbStruct(g_pickerCtx.currentHue, g_pickerCtx.currentSat, g_pickerCtx.currentValue);
        InvalidateRect(g_pickerCtx.hWindow, NULL, FALSE);

        if (g_pickerCtx.textGenerator) {
            g_pickerCtx.textGenerator(g_pickerCtx.currentRGB.r, g_pickerCtx.currentRGB.g, g_pickerCtx.currentRGB.b);
        }
    }

    inline void ProcessWheelClick(int mouseX, int mouseY) {
        int dx = mouseX - g_pickerCtx.centerX;
        int dy = mouseY - g_pickerCtx.centerY;
        float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));

        if (distance > g_pickerCtx.radius) return;

        float angle = std::atan2(static_cast<float>(-dy), static_cast<float>(dx));
        if (angle < 0) angle += static_cast<float>(2.0 * M_PI);

        g_pickerCtx.currentHue = angle * (180.0f / static_cast<float>(M_PI));
        g_pickerCtx.currentSat = distance / static_cast<float>(g_pickerCtx.radius);

        UpdateLiveCode();
    }

    inline void ProcessValueClick(int mouseY) {
        int localY = mouseY - g_pickerCtx.valTop;
        if (localY < 0) localY = 0;
        if (localY > g_pickerCtx.valHeight) localY = g_pickerCtx.valHeight;

        g_pickerCtx.currentValue = 1.0f - (static_cast<float>(localY) / static_cast<float>(g_pickerCtx.valHeight));

        UpdateLiveCode();
    }

    inline LRESULT CALLBACK ColorPickerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            int dx = pt.x - g_pickerCtx.centerX;
            int dy = pt.y - g_pickerCtx.centerY;
            float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));

            if (distance <= g_pickerCtx.radius) {
                SetCursor(LoadCursor(NULL, IDC_CROSS));
                return TRUE;
            }

            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                g_pickerCtx.hWindow = NULL;
                return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                DestroyWindow(hwnd);
                g_pickerCtx.hWindow = NULL;
                return 0;
            }
            break;
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT)lParam;
            HDC hdc = lpDrawItem->hDC;
            int w = lpDrawItem->rcItem.right - lpDrawItem->rcItem.left;
            int h = lpDrawItem->rcItem.bottom - lpDrawItem->rcItem.top;

            std::vector<DWORD> pixelBuffer(w * h, 0x0030302D);

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = w;
            bmi.bmiHeader.biHeight = -h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            if (lpDrawItem->hwndItem == g_pickerCtx.hWheelStatic) {
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        int dx = x - g_pickerCtx.centerX;
                        int dy = y - g_pickerCtx.centerY;
                        float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));

                        if (dist <= g_pickerCtx.radius) {
                            float angle = std::atan2(static_cast<float>(-dy), static_cast<float>(dx));
                            if (angle < 0) angle += static_cast<float>(2.0 * M_PI);
                            float hue = angle * (180.0f / static_cast<float>(M_PI));
                            float sat = dist / static_cast<float>(g_pickerCtx.radius);

                            pixelBuffer[y * w + x] = HsvToGdiColor(hue, sat, g_pickerCtx.currentValue);
                        }
                    }
                }
            }
            else if (lpDrawItem->hwndItem == g_pickerCtx.hValueStatic) {
                for (int y = 0; y < h; ++y) {
                    float ratio = 1.0f - (static_cast<float>(y) / static_cast<float>(h));
                    DWORD color = HsvToGdiColor(g_pickerCtx.currentHue, g_pickerCtx.currentSat, ratio);

                    for (int x = 0; x < w; ++x) {
                        pixelBuffer[y * w + x] = color;
                    }
                }

                int markerY = static_cast<int>((1.0f - g_pickerCtx.currentValue) * h);
                if (markerY >= 2 && markerY < h - 2) {
                    for (int my = markerY - 2; my <= markerY + 2; ++my) {
                        pixelBuffer[my * w + 0] = 0xFFFFFF;
                        pixelBuffer[my * w + (w - 1)] = 0xFFFFFF;
                        if (my == markerY - 2 || my == markerY + 2) {
                            for (int mx = 0; mx < w; ++mx) pixelBuffer[my * w + mx] = 0xFFFFFF;
                        }
                    }
                }
            }

            StretchDIBits(hdc, 0, 0, w, h, 0, 0, w, h, pixelBuffer.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            return TRUE;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT previewRect = { 162, 8, 207, 122 };

            HBRUSH hPreviewBrush = CreateSolidBrush(RGB(g_pickerCtx.currentRGB.r, g_pickerCtx.currentRGB.g, g_pickerCtx.currentRGB.b));
            FillRect(hdc, &previewRect, hPreviewBrush);
            DeleteObject(hPreviewBrush);

            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
            HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
            MoveToEx(hdc, previewRect.left, previewRect.top, NULL);
            LineTo(hdc, previewRect.right, previewRect.top);
            LineTo(hdc, previewRect.right, previewRect.bottom);
            LineTo(hdc, previewRect.left, previewRect.bottom);
            LineTo(hdc, previewRect.left, previewRect.top);
            SelectObject(hdc, oldPen);
            DeleteObject(hPen);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            RECT rcWheel, rcValue;

            GetWindowRect(g_pickerCtx.hWheelStatic, &rcWheel);
            MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcWheel, 2);
            GetWindowRect(g_pickerCtx.hValueStatic, &rcValue);
            MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcValue, 2);

            if (PtInRect(&rcWheel, pt)) {
                g_pickerCtx.isTrackingWheel = true;
                SetCapture(hwnd);
                ProcessWheelClick(pt.x - rcWheel.left, pt.y - rcWheel.top);
            }
            else if (PtInRect(&rcValue, pt)) {
                g_pickerCtx.isTrackingValue = true;
                SetCapture(hwnd);
                ProcessValueClick(pt.y);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            if (g_pickerCtx.isTrackingWheel) {
                RECT rcWheel;
                GetWindowRect(g_pickerCtx.hWheelStatic, &rcWheel);
                MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rcWheel, 2);
                ProcessWheelClick(pt.x - rcWheel.left, pt.y - rcWheel.top);
            }
            else if (g_pickerCtx.isTrackingValue) {
                ProcessValueClick(pt.y);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_pickerCtx.isTrackingWheel || g_pickerCtx.isTrackingValue) {
                g_pickerCtx.isTrackingWheel = false;
                g_pickerCtx.isTrackingValue = false;
                ReleaseCapture();
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            g_pickerCtx.hWindow = NULL;
            return 0;
        case WM_DESTROY:
            break;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
}
namespace Widgets {

    inline void color3Callback(const std::any& anyValue, std::function<void(std::string)> vsUpdater) {
        const std::string* pRawText = std::any_cast<std::string>(&anyValue);
        if (!pRawText) return;

        std::string srcText = *pRawText;

        // 1. АНАЛИЗ СИНТАКСИСA И ФОРМАТА
        bool isMultiLine = (srcText.find('\n') != std::string::npos || srcText.find('\r') != std::string::npos);
        bool hasExplicitFields = (srcText.find(".r") != std::string::npos || srcText.find(".x") != std::string::npos);

        // Флаг флоата по умолчанию сброшен, мы взведем его только если реально найдем 'f' или '.' внутри ЧИСЛА
        bool isFloatingPoint = false;

        // 2. ДЕСЕРИАЛИЗАЦИЯ: Вырезаем три компонента из любого форматирования
        std::stringstream ss(srcText);
        std::string token;
        float rawVals[3] = { 0.0f, 0.0f, 0.0f };
        int idx = 0;

        while (std::getline(ss, token, ',')) {
            if (idx >= 3) break;
            size_t eqPos = token.find('=');
            if (eqPos == std::string::npos) eqPos = token.find(':');
            std::string valPart = (eqPos != std::string::npos) ? token.substr(eqPos + 1) : token;

            // Очищаем токен значения, убирая имена полей и их точки (.r, .g, .b)
            valPart.erase(0, valPart.find_first_not_of(" \t\r\n.rgbxyz="));
            valPart.erase(valPart.find_last_not_of(" \t\r\nFfuUlL") + 1);
            if (valPart.empty()) continue;

            // === ИСПРАВЛЕНИЕ: Проверяем на флоат только ЧИСТОЕ ЧИСЛО ===
            // Если в самом числе есть точка (например "1.0") или суффикс "f" — это честный float
            if (valPart.find('.') != std::string::npos ||
                valPart.find('f') != std::string::npos ||
                valPart.find('F') != std::string::npos) {
                isFloatingPoint = true;
            }

            float val = 0.0f;
            std::from_chars(valPart.data(), valPart.data() + valPart.size(), val);
            rawVals[idx] = val;
            idx++;
        }

        float rNorm = 0.0f, gNorm = 0.0f, bNorm = 0.0f;
        if (isFloatingPoint) {
            rNorm = std::clamp(rawVals[0], 0.0f, 1.0f);
            gNorm = std::clamp(rawVals[1], 0.0f, 1.0f);
            bNorm = std::clamp(rawVals[2], 0.0f, 1.0f);
        }
        else {
            rNorm = std::clamp(rawVals[0] / 255.0f, 0.0f, 1.0f);
            gNorm = std::clamp(rawVals[1] / 255.0f, 0.0f, 1.0f);
            bNorm = std::clamp(rawVals[2] / 255.0f, 0.0f, 1.0f);
        }

        g_pickerCtx.currentRGB.r = static_cast<unsigned char>(rNorm * 255.0f);
        g_pickerCtx.currentRGB.g = static_cast<unsigned char>(gNorm * 255.0f);
        g_pickerCtx.currentRGB.b = static_cast<unsigned char>(bNorm * 255.0f);
        g_pickerCtx.vsUpdaterCallback = vsUpdater;

        float maxVal = (std::max)({ rNorm, gNorm, bNorm });
        float minVal = (std::min)({ rNorm, gNorm, bNorm });
        float delta = maxVal - minVal;
        g_pickerCtx.currentValue = maxVal;
        g_pickerCtx.currentSat = (maxVal == 0.0f) ? 0.0f : (delta / maxVal);
        if (delta == 0.0f) g_pickerCtx.currentHue = 0.0f;
        else {
            if (maxVal == rNorm) g_pickerCtx.currentHue = 60.0f * (std::fmod(((gNorm - bNorm) / delta), 6.0f));
            else if (maxVal == gNorm) g_pickerCtx.currentHue = 60.0f * (((bNorm - rNorm) / delta) + 2.0f);
            else if (maxVal == bNorm) g_pickerCtx.currentHue = 60.0f * (((rNorm - gNorm) / delta) + 4.0f);
            if (g_pickerCtx.currentHue < 0.0f) g_pickerCtx.currentHue += 360.0f;
        }

        // 3. === АДАПТАЦИЯ СИММЕТРИЧНОГО ГЕНЕРАТОРА КОДА ПОД СХЕМУ С selection ===
        g_pickerCtx.textGenerator = [vsUpdater, isMultiLine, hasExplicitFields, isFloatingPoint](unsigned char r, unsigned char g, unsigned char b) {
            char buf[512]{};

            if (isFloatingPoint) {
                // ВЕТКА ФЛОАТОВ (0.0f - 1.0f)
                float outR = r / 255.0f;
                float outG = g / 255.0f;
                float outB = b / 255.0f;
                const char* fmt = "%.2ff";

                if (isMultiLine) {
                    std::string line1 = hasExplicitFields ? ".r=" : "";
                    std::string line2 = hasExplicitFields ? ".g=" : "";
                    std::string line3 = hasExplicitFields ? ".b=" : "";
                    std::string masterFmt = "\n\t\t\t\t" + line1 + fmt + ", \n\t\t\t\t" + line2 + fmt + ",  \n\t\t\t\t" + line3 + fmt + "\n\t\t\t\t";
                    sprintf_s(buf, masterFmt.c_str(), outR, outG, outB);
                }
                else {
                    std::string masterFmt = hasExplicitFields
                        ? (std::string(".r=") + fmt + ", .g=" + fmt + ", .b=" + fmt)
                        : (std::string(fmt) + ", " + fmt + ", " + fmt);
                    sprintf_s(buf, masterFmt.c_str(), outR, outG, outB);
                }
            }
            else {
                // ВЕТКА БАЙТОВ (0 - 255)
                // Принудительно кастим в int и используем жесткий целочисленный спецификатор %d
                int outR = static_cast<int>(r);
                int outG = static_cast<int>(g);
                int outB = static_cast<int>(b);
                const char* fmt = "%d";

                if (isMultiLine) {
                    std::string line1 = hasExplicitFields ? ".r=" : "";
                    std::string line2 = hasExplicitFields ? ".g=" : "";
                    std::string line3 = hasExplicitFields ? ".b=" : "";
                    std::string masterFmt = "\n\t\t\t\t" + line1 + fmt + ", \n\t\t\t\t" + line2 + fmt + ",  \n\t\t\t\t" + line3 + fmt + "\n\t\t\t\t";
                    sprintf_s(buf, masterFmt.c_str(), outR, outG, outB);
                }
                else {
                    std::string masterFmt = hasExplicitFields
                        ? (std::string(".r=") + fmt + ", .g=" + fmt + ", .b=" + fmt)
                        : (std::string(fmt) + ", " + fmt + ", " + fmt);
                    sprintf_s(buf, masterFmt.c_str(), outR, outG, outB);
                }
            }

            vsUpdater(buf);
            };

        // =========================================================================
        // [ТВОЙ ОРИГИНАЛЬНЫЙ WIN32 КОД ОКНА ПАЛИТРЫ]: Полностью сохранен
        // =========================================================================
        if (g_pickerCtx.hWindow && IsWindow(g_pickerCtx.hWindow)) {
            InvalidateRect(g_pickerCtx.hWindow, NULL, FALSE);
            SetActiveWindow(g_pickerCtx.hWindow);
            return;
        }

        POINT mousePos;
        GetCursorPos(&mousePos);
        HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_CustomColorWheelWin";

        static bool registered = [hInst, className]() {
            WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
            wc.lpfnWndProc = ColorPickerWndProc;
            wc.hInstance = hInst;
            wc.lpszClassName = className;
            wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(RGB(45, 45, 48));
            return RegisterClassExA(&wc) != 0;
            }();

        int calculatedWindowY = mousePos.y - 144;

        g_pickerCtx.hWindow = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className, NULL,
            WS_POPUP | WS_VISIBLE,
            mousePos.x - 10, calculatedWindowY, 215, 130,
            NULL, NULL, hInst, NULL
        );

        if (!g_pickerCtx.hWindow) return;

        g_pickerCtx.hWheelStatic = CreateWindowExA(
            0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 130, 130, g_pickerCtx.hWindow, NULL, hInst, NULL
        );

        g_pickerCtx.hValueStatic = CreateWindowExA(
            0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            g_pickerCtx.valLeft, g_pickerCtx.valTop, g_pickerCtx.valWidth, g_pickerCtx.valHeight,
            g_pickerCtx.hWindow, NULL, hInst, NULL
        );

        ShowWindow(g_pickerCtx.hWindow, SW_SHOW);
        UpdateWindow(g_pickerCtx.hWindow);
        SetForegroundWindow(g_pickerCtx.hWindow);
        SetFocus(g_pickerCtx.hWindow);
    }
}
