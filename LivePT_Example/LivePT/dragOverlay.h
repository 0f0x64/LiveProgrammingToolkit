#include <windows.h>
#include <string>
#include <mutex>

namespace LivePT {

    inline HCURSOR      g_OverlayCursor = NULL;
    inline std::string  g_OverlayText = "";
    inline bool         g_OverlayShow = false;

    static HWND         s_hOverlayWnd = NULL;
    static std::mutex   s_OverlayMutex;

    // БАЗОВЫЕ размеры для 100% масштаба (DPI = 96)
    const int BASE_OVERLAY_WIDTH = 350;
    const int BASE_OVERLAY_HEIGHT = 105;
    const int BASE_FONT_SIZE = 18;

    // Вспомогательная функция для получения текущего DPI скейла окна
    inline float GetCurrentDPIScale() {
        if (s_hOverlayWnd) {
            // Современный и самый точный способ в Windows 10/11
            UINT dpi = ::GetDpiForWindow(s_hOverlayWnd);
            if (dpi > 0) return static_cast<float>(dpi) / 96.0f;
        }
        // Фолбэк на системный DPI, если окно еще не создано
        HDC hdc = GetDC(NULL);
        int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(NULL, hdc);
        return static_cast<float>(dpiY) / 96.0f;
    }

    inline void RedrawOverlayBuffer(int screenX, int screenY) {
        if (!s_hOverlayWnd) return;

        static HCURSOR     s_LastLoadedCursor = NULL;
        static std::string s_LastLoadedText = "";

        HCURSOR     localCursor;
        std::string localText;
        {
            std::lock_guard<std::mutex> lock(s_OverlayMutex);
            localCursor = g_OverlayCursor;
            localText = g_OverlayText;
        }

        // 1. ДИНАМИЧЕСКИЙ РАСЧЕТ СКЕЙЛА
        float scale = GetCurrentDPIScale();
        int currentWidth = static_cast<int>(BASE_OVERLAY_WIDTH * scale);
        int currentHeight = static_cast<int>(BASE_OVERLAY_HEIGHT * scale);
        int fontSize = static_cast<int>(BASE_FONT_SIZE * scale);

        int hotspotX = 0;
        int hotspotY = 0;
        int cursorWidth = static_cast<int>(32 * scale); // Базовый размер курсора 32x32

        if (localCursor) {
            ICONINFO iconInfo = { 0 };
            if (::GetIconInfo(localCursor, &iconInfo)) {
                hotspotX = (int)iconInfo.xHotspot;
                hotspotY = (int)iconInfo.yHotspot;

                // Извлекаем реальные размеры курсора, так как ОС его уже отмасштабировала
                if (iconInfo.hbmColor) {
                    BITMAP bmp;
                    if (GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmp)) {
                        cursorWidth = bmp.bmWidth;
                    }
                }
                if (iconInfo.hbmMask)  ::DeleteObject(iconInfo.hbmMask);
                if (iconInfo.hbmColor) ::DeleteObject(iconInfo.hbmColor);
            }
        }

        if (localCursor == s_LastLoadedCursor && localText == s_LastLoadedText) {
            int targetX = screenX - hotspotX;
            int targetY = screenY - hotspotY;
            ::SetWindowPos(s_hOverlayWnd, HWND_TOPMOST, targetX, targetY, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
            return;
        }

        s_LastLoadedCursor = localCursor;
        s_LastLoadedText = localText;

        HDC hdcScreen = GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        // Передаем динамические размеры с учетом масштаба
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = currentWidth;
        bmi.bmiHeader.biHeight = currentHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

        memset(pBits, 0, currentWidth * currentHeight * 4);

        if (localCursor) {
            DrawIcon(hdcMem, 0, 0, localCursor);
        }

        if (!localText.empty()) {
            // Применяем отмасштабированный fontSize
            HFONT hFont = CreateFontA(
                fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI"
            );
            HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);

            SIZE textSize;
            GetTextExtentPoint32A(hdcMem, localText.c_str(), (int)localText.length(), &textSize);

            // Текст выводится строго под нижней границей смасштабированного курсора
            int textYOffset = cursorWidth;

            // Скейлим отступы плашки (padding)
            int padX = static_cast<int>(6 * scale);
            int padY = static_cast<int>(3 * scale);

            int rectLeft = hotspotX - padX;
            int rectTop = textYOffset - padY;
            int rectRight = hotspotX + textSize.cx + padX;
            int rectBottom = textYOffset + textSize.cy + padY;

            if (rectLeft < 0) rectLeft = 0;
            if (rectRight > currentWidth) rectRight = currentWidth;

            HBRUSH hBgBrush = CreateSolidBrush(RGB(37, 37, 38));
            RECT rcTextBg = { rectLeft, rectTop, rectRight, rectBottom };
            FillRect(hdcMem, &rcTextBg, hBgBrush);
            DeleteObject(hBgBrush);

            HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(0, 122, 204));
            HGDIOBJ hOldPen = SelectObject(hdcMem, hBorderPen);
            HGDIOBJ hOldBrush = SelectObject(hdcMem, GetStockObject(NULL_BRUSH));

            int roundCorner = static_cast<int>(3 * scale);
            RoundRect(hdcMem, rectLeft, rectTop, rectRight, rectBottom, roundCorner, roundCorner);

            SelectObject(hdcMem, hOldBrush);
            SelectObject(hdcMem, hOldPen);
            DeleteObject(hBorderPen);

            SetBkMode(hdcMem, TRANSPARENT);
            SetTextColor(hdcMem, RGB(241, 241, 241));
            TextOutA(hdcMem, hotspotX, textYOffset, localText.c_str(), (int)localText.length());

            // Прошивка Альфа-канала с учетом динамической ширины и высоты буфера
            DWORD* pixels = (DWORD*)pBits;
            for (int y = rectTop; y < rectBottom; ++y) {
                for (int x = rectLeft; x < rectRight; ++x) {
                    // Инвертируем Y для DIB-секции
                    int pixelIdx = (currentHeight - 1 - y) * currentWidth + x;
                    if (pixelIdx >= 0 && pixelIdx < currentWidth * currentHeight) {
                        pixels[pixelIdx] |= 0xFC000000;
                    }
                }
            }

            SelectObject(hdcMem, hOldFont);
            DeleteObject(hFont);
        }

        POINT ptZero = { 0, 0 };
        SIZE sizeWnd = { currentWidth, currentHeight }; // Передаем новые размеры в систему
        BLENDFUNCTION blend = { 0 };
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        POINT ptWnd = { screenX - hotspotX, screenY - hotspotY };
        UpdateLayeredWindow(s_hOverlayWnd, hdcScreen, &ptWnd, &sizeWnd, hdcMem, &ptZero, 0, &blend, ULW_ALPHA);

        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
    }

    inline LRESULT CALLBACK PassiveOverlayWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_DESTROY) { s_hOverlayWnd = NULL; return 0; }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    inline void InitPassiveOverlay() {
        if (s_hOverlayWnd) return;
        HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_PassiveOverlayWindow";

        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
        wc.lpfnWndProc = PassiveOverlayWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = className;
        wc.hCursor = NULL;
        RegisterClassExA(&wc);

        // Изначально создаем окно с базовыми размерами, UpdateLayeredWindow сам растянет его внутренности
        s_hOverlayWnd = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className, "LPT_Overlay", WS_POPUP,
            0, 0, BASE_OVERLAY_WIDTH, BASE_OVERLAY_HEIGHT, NULL, NULL, hInst, NULL
        );
    }

    inline void UpdateOverlayPosition(int screenX, int screenY) {
        if (!s_hOverlayWnd) {
            InitPassiveOverlay();
        }

        bool shouldShow = false;
        {
            std::lock_guard<std::mutex> lock(s_OverlayMutex);
            shouldShow = g_OverlayShow;
        }

        if (shouldShow) {
            RedrawOverlayBuffer(screenX, screenY);
            if (!::IsWindowVisible(s_hOverlayWnd)) {
                ::ShowWindow(s_hOverlayWnd, SW_SHOWNOACTIVATE);
            }
        }
        else {
            if (::IsWindowVisible(s_hOverlayWnd)) {
                ::ShowWindow(s_hOverlayWnd, SW_HIDE);
            }
        }
    }
}
