#if LivePT_EditMode

#pragma once
#include <windows.h>
#include <windowsx.h>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include <any>
#include <algorithm>
#include <sstream>
#include <charconv>
#include <type_traits>

#pragma comment(lib, "Msimg32.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Widgets {

    static std::vector<DWORD> g_wheelCache;
    static int g_cachedW = 0;
    static int g_cachedH = 0;
    static float g_lastCachedV = -1.0f; //

    struct ColorPickerContext {
        HWND hWindow = NULL;
        HWND hWheelStatic = NULL;
        HWND hValueStatic = NULL;

        ::color3 currentRGB = {255, 255, 255};
        std::function<void(std::string)> vsUpdaterCallback = nullptr;
        std::function<void(unsigned char, unsigned char, unsigned char)> textGenerator = nullptr;

        bool isTrackingWheel = false;
        bool isTrackingValue = false;

        float currentHue = 0.0f;
        float currentSat = 0.0f;
        float currentValue = 1.0f;

        const int BASE_windowWidth = 215;
        const int BASE_windowHeight = 130;

        const int BASE_centerX = 65;
        const int BASE_centerY = 65;
        const int BASE_radius = 58;

        const int BASE_valLeft = 135;
        const int BASE_valTop = 8;
        const int BASE_valWidth = 16;
        const int BASE_valHeight = 114;

        const int BASE_previewLeft = 162;
        const int BASE_previewTop = 8;
        const int BASE_previewRight = 207;
        const int BASE_previewBottom = 122;
    };

    static ColorPickerContext g_pickerCtx;

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

    inline ::color3 HsvToRgbStruct(float H, float S, float V) {
        DWORD gdiColor = HsvToGdiColor(H, S, V);
        return ::color3{
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

    inline void ProcessWheelClick(HWND hwnd, int mouseX, int mouseY) {
        float scale = ::LivePT::GetWidgetScale(hwnd);
        int currentCenterX = static_cast<int>(g_pickerCtx.BASE_centerX * scale);
        int currentCenterY = static_cast<int>(g_pickerCtx.BASE_centerY * scale);
        int currentRadius = static_cast<int>(g_pickerCtx.BASE_radius * scale);

        int dx = mouseX - currentCenterX;
        int dy = mouseY - currentCenterY;
        float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));

        if (distance > currentRadius) distance = static_cast<float>(currentRadius);

        float angle = std::atan2(static_cast<float>(-dy), static_cast<float>(dx));
        if (angle < 0) angle += static_cast<float>(2.0 * M_PI);

        g_pickerCtx.currentHue = angle * (180.0f / static_cast<float>(M_PI));
        g_pickerCtx.currentSat = distance / static_cast<float>(currentRadius);

        UpdateLiveCode();
    }

    inline void ProcessValueClick(HWND hwnd, int mouseY) {
        float scale = ::LivePT::GetWidgetScale(hwnd);
        int currentValTop = static_cast<int>(g_pickerCtx.BASE_valTop * scale);
        int currentValHeight = static_cast<int>(g_pickerCtx.BASE_valHeight * scale);

        int localY = mouseY - currentValTop;
        if (localY < 0) localY = 0;
        if (localY > currentValHeight) localY = currentValHeight;

        g_pickerCtx.currentValue = 1.0f - (static_cast<float>(localY) / static_cast<float>(currentValHeight));

        UpdateLiveCode();
    }

    inline LRESULT CALLBACK ColorPickerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SETCURSOR: {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            float scale = ::LivePT::GetWidgetScale(hwnd);
            int cx = static_cast<int>(g_pickerCtx.BASE_centerX * scale);
            int cy = static_cast<int>(g_pickerCtx.BASE_centerY * scale);
            int r = static_cast<int>(g_pickerCtx.BASE_radius * scale);

            int dx = pt.x - cx; int dy = pt.y - cy;
            float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            SetCursor(LoadCursor(NULL, distance <= r ? IDC_CROSS : IDC_ARROW));
            return TRUE;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                if (g_pickerCtx.isTrackingWheel || g_pickerCtx.isTrackingValue) ReleaseCapture();
                DestroyWindow(hwnd); g_pickerCtx.hWindow = NULL; return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                if (g_pickerCtx.isTrackingWheel || g_pickerCtx.isTrackingValue) ReleaseCapture();
                DestroyWindow(hwnd); g_pickerCtx.hWindow = NULL; return 0;
            }
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);

            RECT clientRc; GetClientRect(hwnd, &clientRc);
            int w = clientRc.right - clientRc.left;
            int h = clientRc.bottom - clientRc.top;
            float scale = ::LivePT::GetWidgetScale(hwnd);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

            static std::vector<DWORD> g_wheelTextureCache;
            static int g_lastWheelW = 0; static int g_lastWheelH = 0;
            static float g_lastV = -1.0f;

            int cx = static_cast<int>(g_pickerCtx.BASE_centerX * scale);
            int cy = static_cast<int>(g_pickerCtx.BASE_centerY * scale);
            int r = static_cast<int>(g_pickerCtx.BASE_radius * scale);

            int wheelBoxLeft = cx - r;
            int wheelBoxTop = cy - r;
            int wheelBoxWidth = r * 2;
            int wheelBoxHeight = r * 2;

            if (wheelBoxWidth != g_lastWheelW || wheelBoxHeight != g_lastWheelH || g_pickerCtx.currentValue != g_lastV) {
                g_lastWheelW = wheelBoxWidth; g_lastWheelH = wheelBoxHeight; g_lastV = g_pickerCtx.currentValue;
                g_wheelTextureCache.assign(wheelBoxWidth * wheelBoxHeight, 0x002D2D30);

                for (int y = 0; y < wheelBoxHeight; ++y) {
                    float dy = static_cast<float>(y - r);
                    for (int x = 0; x < wheelBoxWidth; ++x) {
                        float dx = static_cast<float>(x - r);
                        float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist <= r) {
                            float angle = std::atan2(-dy, dx);
                            if (angle < 0.0f) angle += static_cast<float>(2.0 * M_PI);
                            float hue = angle * (180.0f / static_cast<float>(M_PI));
                            float sat = dist / static_cast<float>(r);
                            g_wheelTextureCache[y * wheelBoxWidth + x] = HsvToGdiColor(hue, sat, g_pickerCtx.currentValue);
                        }
                    }
                }
            }

            HBRUSH hBg = CreateSolidBrush(RGB(45, 45, 48));
            FillRect(memDC, &clientRc, hBg); DeleteObject(hBg);

            BITMAPINFO wheelBmi = {};
            wheelBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            wheelBmi.bmiHeader.biWidth = wheelBoxWidth; wheelBmi.bmiHeader.biHeight = -wheelBoxHeight;
            wheelBmi.bmiHeader.biPlanes = 1; wheelBmi.bmiHeader.biBitCount = 32; wheelBmi.bmiHeader.biCompression = BI_RGB;

            StretchDIBits(memDC, wheelBoxLeft, wheelBoxTop, wheelBoxWidth, wheelBoxHeight,
                0, 0, wheelBoxWidth, wheelBoxHeight, g_wheelTextureCache.data(), &wheelBmi, DIB_RGB_COLORS, SRCCOPY);

            int vLeft = static_cast<int>(g_pickerCtx.BASE_valLeft * scale);
            int vTop = static_cast<int>(g_pickerCtx.BASE_valTop * scale);
            int vWidth = static_cast<int>(g_pickerCtx.BASE_valWidth * scale);
            int vHeight = static_cast<int>(g_pickerCtx.BASE_valHeight * scale);

            DWORD targetFullColor = HsvToGdiColor(g_pickerCtx.currentHue, g_pickerCtx.currentSat, 1.0f);

            TRIVERTEX vertices[2];

            vertices[0].x = vLeft;
            vertices[0].y = vTop;
            vertices[0].Red = ((targetFullColor >> 16) & 0xFF) << 8;
            vertices[0].Green = ((targetFullColor >> 8) & 0xFF) << 8;
            vertices[0].Blue = (targetFullColor & 0xFF) << 8;
            vertices[0].Alpha = 0;

            vertices[1].x = vLeft + vWidth;
            vertices[1].y = vTop + vHeight;
            vertices[1].Red = 0;
            vertices[1].Green = 0;
            vertices[1].Blue = 0;
            vertices[1].Alpha = 0;

            GRADIENT_RECT gRect;
            gRect.UpperLeft = 0;
            gRect.LowerRight = 1;

            GradientFill(memDC, vertices, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

            RECT previewRect = {
                static_cast<int>(g_pickerCtx.BASE_previewLeft * scale), static_cast<int>(g_pickerCtx.BASE_previewTop * scale),
                static_cast<int>(g_pickerCtx.BASE_previewRight * scale), static_cast<int>(g_pickerCtx.BASE_previewBottom * scale)
            };
            HBRUSH hPreviewBrush = CreateSolidBrush(RGB(g_pickerCtx.currentRGB.r, g_pickerCtx.currentRGB.g, g_pickerCtx.currentRGB.b));
            FillRect(memDC, &previewRect, hPreviewBrush); DeleteObject(hPreviewBrush);

            float rad = g_pickerCtx.currentHue * (static_cast<float>(M_PI) / 180.0f);
            int crossX = cx + static_cast<int>(std::cos(rad) * g_pickerCtx.currentSat * r);
            int crossY = cy - static_cast<int>(std::sin(rad) * g_pickerCtx.currentSat * r);

            HPEN hMarkerPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HPEN oldPen = (HPEN)SelectObject(memDC, hMarkerPen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));

            Ellipse(memDC, crossX - 4, crossY - 4, crossX + 5, crossY + 5);

            int markerY = vTop + static_cast<int>((1.0f - g_pickerCtx.currentValue) * (vHeight - 1));
            int thickness = static_cast<int>(2 * scale); if (thickness < 1) thickness = 1;
            RECT rcMarker = { vLeft - 1, markerY - thickness, vLeft + vWidth + 1, markerY + thickness + 1 };
            FrameRect(memDC, &rcMarker, (HBRUSH)GetStockObject(WHITE_BRUSH));

            RECT rcPreview = { previewRect.left, previewRect.top, previewRect.right, previewRect.bottom };
            FrameRect(memDC, &rcPreview, (HBRUSH)GetStockObject(WHITE_BRUSH));

            SelectObject(memDC, oldBrush); SelectObject(memDC, oldPen); DeleteObject(hMarkerPen);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
            EndPaint(hwnd, &ps); return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            float scale = ::LivePT::GetWidgetScale(hwnd);

            int cx = static_cast<int>(g_pickerCtx.BASE_centerX * scale);
            int cy = static_cast<int>(g_pickerCtx.BASE_centerY * scale);
            int r = static_cast<int>(g_pickerCtx.BASE_radius * scale);
            int dx = pt.x - cx; int dy = pt.y - cy;
            if (std::sqrt(static_cast<float>(dx * dx + dy * dy)) <= r) {
                g_pickerCtx.isTrackingWheel = true; SetCapture(hwnd);
                ProcessWheelClick(hwnd, pt.x, pt.y); return 0;
            }

            int vLeft = static_cast<int>(g_pickerCtx.BASE_valLeft * scale);
            int vTop = static_cast<int>(g_pickerCtx.BASE_valTop * scale);
            int vWidth = static_cast<int>(g_pickerCtx.BASE_valWidth * scale);
            int vHeight = static_cast<int>(g_pickerCtx.BASE_valHeight * scale);
            RECT rcValue = { vLeft, vTop, vLeft + vWidth, vTop + vHeight };
            if (PtInRect(&rcValue, pt)) {
                g_pickerCtx.isTrackingValue = true; SetCapture(hwnd);
                ProcessValueClick(hwnd, pt.y); return 0;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (g_pickerCtx.isTrackingWheel) {
                ProcessWheelClick(hwnd, pt.x, pt.y);
            }
            else if (g_pickerCtx.isTrackingValue) {
                ProcessValueClick(hwnd, pt.y);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_pickerCtx.isTrackingWheel || g_pickerCtx.isTrackingValue) {
                g_pickerCtx.isTrackingWheel = false; g_pickerCtx.isTrackingValue = false;
                ReleaseCapture();
            }
            return 0;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    inline void color3Callback(const std::any& anyValue, std::function<void(std::string)> vsUpdater) {
        const std::string* pRawText = std::any_cast<std::string>(&anyValue);
        if (!pRawText) return;

        std::string srcText = *pRawText;
        bool isMultiLine = (srcText.find('\n') != std::string::npos || srcText.find('\r') != std::string::npos);
        bool hasExplicitFields = (srcText.find(".r") != std::string::npos || srcText.find(".x") != std::string::npos);
        bool isFloatingPoint = false;

        std::stringstream ss(srcText); std::string token;
        float rawVals[3] = { 0.0f, 0.0f, 0.0f }; int idx = 0;

        while (std::getline(ss, token, ',')) {
            if (idx >= 3) break;
            size_t eqPos = token.find('='); if (eqPos == std::string::npos) eqPos = token.find(':');
            std::string valPart = (eqPos != std::string::npos) ? token.substr(eqPos + 1) : token;

            valPart.erase(0, valPart.find_first_not_of(" \t\r\n.rgbxyz="));
            valPart.erase(valPart.find_last_not_of(" \t\r\nFfuUlL") + 1);
            if (valPart.empty()) continue;

            if (valPart.find('.') != std::string::npos || valPart.find('f') != std::string::npos || valPart.find('F') != std::string::npos) {
                isFloatingPoint = true;
            }
            float val = 0.0f; std::from_chars(valPart.data(), valPart.data() + valPart.size(), val);
            rawVals[idx] = val; idx++;
        }

        float rNorm = 0.0f, gNorm = 0.0f, bNorm = 0.0f;
        if (isFloatingPoint) {
            rNorm = std::clamp(rawVals[0], 0.0f, 1.0f); gNorm = std::clamp(rawVals[1], 0.0f, 1.0f); bNorm = std::clamp(rawVals[2], 0.0f, 1.0f);
        }
        else {
            rNorm = std::clamp(rawVals[0] / 255.0f, 0.0f, 1.0f); gNorm = std::clamp(rawVals[1] / 255.0f, 0.0f, 1.0f); bNorm = std::clamp(rawVals[2] / 255.0f, 0.0f, 1.0f);
        }

        g_pickerCtx.currentRGB.r = static_cast<unsigned char>(rNorm * 255.0f);
        g_pickerCtx.currentRGB.g = static_cast<unsigned char>(gNorm * 255.0f);
        g_pickerCtx.currentRGB.b = static_cast<unsigned char>(bNorm * 255.0f);
        g_pickerCtx.vsUpdaterCallback = vsUpdater;

        float maxVal = (std::max)({ rNorm, gNorm, bNorm }); float minVal = (std::min)({ rNorm, gNorm, bNorm });
        float delta = maxVal - minVal; g_pickerCtx.currentValue = maxVal;
        g_pickerCtx.currentSat = (maxVal == 0.0f) ? 0.0f : (delta / maxVal);
        if (delta == 0.0f) g_pickerCtx.currentHue = 0.0f;
        else {
            if (maxVal == rNorm) g_pickerCtx.currentHue = 60.0f * (std::fmod(((gNorm - bNorm) / delta), 6.0f));
            else if (maxVal == gNorm) g_pickerCtx.currentHue = 60.0f * (((bNorm - rNorm) / delta) + 2.0f);
            else if (maxVal == bNorm) g_pickerCtx.currentHue = 60.0f * (((rNorm - gNorm) / delta) + 4.0f);
            if (g_pickerCtx.currentHue < 0.0f) g_pickerCtx.currentHue += 360.0f;
        }

        g_pickerCtx.textGenerator = [vsUpdater, isMultiLine, hasExplicitFields, isFloatingPoint](unsigned char r, unsigned char g, unsigned char b) {
            char buf[512]{};
            if (isFloatingPoint) {
                float outR = r / 255.0f; float outG = g / 255.0f; float outB = b / 255.0f; const char* fmt = "%.2ff";
                if (isMultiLine) {
                    std::string masterFmt = "\n\t\t\t\t" + std::string(hasExplicitFields ? ".r=" : "") + fmt + ", \n\t\t\t\t" + (hasExplicitFields ? ".g=" : "") + fmt + ",  \n\t\t\t\t" + (hasExplicitFields ? ".b=" : "") + fmt + "\n\t\t\t\t";
                    sprintf_s(buf, masterFmt.c_str(), outR, outG, outB);
                }
                else {
                    std::string masterFmt = hasExplicitFields ? (std::string(".r=") + fmt + ", .g=" + fmt + ", .b=" + fmt) : (std::string(fmt) + ", " + fmt + ", " + fmt);
                    sprintf_s(buf, masterFmt.c_str(), outR, outG, outB);
                }
            }
            else {
                int outR = static_cast<int>(r); int outG = static_cast<int>(g); int outB = static_cast<int>(b); const char* fmt = "%d";
                if (isMultiLine) {
                    std::string masterFmt = "\n\t\t\t\t" + std::string(hasExplicitFields ? ".r=" : "") + fmt + ", \n\t\t\t\t" + (hasExplicitFields ? ".g=" : "") + fmt + ",  \n\t\t\t\t" + (hasExplicitFields ? ".b=" : "") + fmt + "\n\t\t\t\t";
                    sprintf_s(buf, masterFmt.c_str(), outR, outG, outB);
                }
                else {
                    std::string masterFmt = hasExplicitFields ? (std::string(".r=") + fmt + ", .g=" + fmt + ", .b=" + fmt) : (std::string(fmt) + ", " + fmt + ", " + fmt);
                    sprintf_s(buf, masterFmt.c_str(), outR, outG, outB);
                }
            }
            vsUpdater(buf);
            };

        if (g_pickerCtx.hWindow && IsWindow(g_pickerCtx.hWindow)) {
            InvalidateRect(g_pickerCtx.hWindow, NULL, FALSE); SetActiveWindow(g_pickerCtx.hWindow); return;
        }

        POINT mousePos; GetCursorPos(&mousePos); HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_CustomColorWheelWin";

        static bool registered = [hInst, className]() {
            WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) }; wc.lpfnWndProc = ColorPickerWndProc;
            wc.hInstance = hInst; wc.lpszClassName = className;
            return RegisterClassExA(&wc) != 0;
            }();

        float scale = ::LivePT::GetWidgetScale();
        int scaledWidth = static_cast<int>(g_pickerCtx.BASE_windowWidth * scale);
        int scaledHeight = static_cast<int>(g_pickerCtx.BASE_windowHeight * scale);
        int calculatedWindowY = mousePos.y - scaledHeight - 20;

        g_pickerCtx.hWindow = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW, className, NULL, WS_POPUP | WS_VISIBLE,
            mousePos.x - 10, calculatedWindowY, scaledWidth, scaledHeight, NULL, NULL, hInst, NULL
        );

        SetForegroundWindow(g_pickerCtx.hWindow); SetFocus(g_pickerCtx.hWindow);
    }

}

#endif