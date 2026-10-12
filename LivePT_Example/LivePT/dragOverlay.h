#include <windows.h>
#include <string>
#include <mutex>

namespace LivePT {

    // =========================================================================
    // ГЛОБАЛЬНЫЕ УПРАВЛЯЮЩИЕ ПЕРЕМЕННЫЕ (Доступны для изменения извне)
    // =========================================================================
    inline HCURSOR      g_OverlayCursor = NULL;       // Хэндл отображаемого курсора (например, LoadCursor(NULL, IDC_SIZEALL))
    inline std::string  g_OverlayText = "";         // Текст, выводимый под курсором
    inline bool         g_OverlayShow = false;      // Флаг управления видимостью (true - показать, false - скрыть)

    // Внутренние переменные модуля
    static HWND         s_hOverlayWnd = NULL;       // Хэндл пассивного окна
    static std::mutex   s_OverlayMutex;               // Защита данных при изменении из разных потоков

    // Размеры окна (расширены, чтобы гарантированно влезал текст)
    const int OVERLAY_WIDTH = 350;
    const int OVERLAY_HEIGHT = 100;

    // Внутренняя оконная процедура для пассивного окна
    inline LRESULT CALLBACK PassiveOverlayWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_DESTROY) {
            s_hOverlayWnd = NULL;
            return 0;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    // =========================================================================
    // ИНИЦИАЛИЗАЦИЯ (Вызывается один раз при старте плагина/модуля)
    // =========================================================================

    inline void InitPassiveOverlay() {
        if (s_hOverlayWnd) return;

        HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_PassiveOverlayWindow";

        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
        wc.lpfnWndProc = PassiveOverlayWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = className;
        wc.hCursor = NULL; // Реальный курсор скрывается внешней логикой ShowCursor
        RegisterClassExA(&wc);

        // Создаем окно-призрак. Оно изначально скрыто.
        // WS_EX_LAYERED     — попиксельный альфа-блендинг
        // WS_EX_TRANSPARENT — сквозной клик (мышь полностью игнорирует окно)
        // WS_EX_NOACTIVATE  — никогда не забирает фокус ввода у Visual Studio
        // WS_EX_TOOLWINDOW  — убирает из панели задач и Alt+Tab
        s_hOverlayWnd = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className, "LPT_Overlay", WS_POPUP,
            0, 0, OVERLAY_WIDTH, OVERLAY_HEIGHT,
            NULL, NULL, hInst, NULL
        );
    }

    // =========================================================================
    // СИСТЕМНЫЙ МЕТОД ОБНОВЛЕНИЯ (Перерисовывает текстуру на основе глобальных переменных)
    // =========================================================================
        // =========================================================================
    // СИСТЕМНЫЙ МЕТОД ОБНОВЛЕНИЯ (С добавлением контрастной подложки под текст)
    // =========================================================================
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

        int hotspotX = 0;
        int hotspotY = 0;
        if (localCursor) {
            ICONINFO iconInfo = { 0 };
            if (::GetIconInfo(localCursor, &iconInfo)) {
                hotspotX = (int)iconInfo.xHotspot;
                hotspotY = (int)iconInfo.yHotspot;
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

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = OVERLAY_WIDTH;
        bmi.bmiHeader.biHeight = OVERLAY_HEIGHT;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

        // Полная очистка холста в прозрачность (Alpha = 0)
        memset(pBits, 0, OVERLAY_WIDTH * OVERLAY_HEIGHT * 4);

        // 1. Отрисовка иконки курсора
        if (localCursor) {
            DrawIcon(hdcMem, 0, 0, localCursor);
        }

        // 2. Отрисовка подложки и текста
        if (!localText.empty()) {
            // Настраиваем шрифт Segoe UI
            HFONT hFont = CreateFontA(
                28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI"
            );
            HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);

            // Считаем размеры текста, чтобы прямоугольник идеально подгонялся под длину строки
            SIZE textSize;
            GetTextExtentPoint32A(hdcMem, localText.c_str(), (int)localText.length(), &textSize);

            // Координаты плашки под текстом (смещение по Y = 32 пикселя, строго под курсор)
            // Добавляем отступы (padding) по 6 пикселей слева/справа и 3 пикселя сверху/снизу
            int rectLeft = hotspotX - 6;
            int rectTop = 32 - 3;
            int rectRight = hotspotX + textSize.cx + 6;
            int rectBottom = 32 + textSize.cy + 3;

            // Защита от выхода за границы битмапа
            if (rectLeft < 0) rectLeft = 0;
            if (rectRight > OVERLAY_WIDTH) rectRight = OVERLAY_WIDTH;

            // Рисуем фон плашки (Темно-серый цвет из темы Visual Studio)
            HBRUSH hBgBrush = CreateSolidBrush(RGB(37, 37, 38));
            RECT rcTextBg = { rectLeft, rectTop, rectRight, rectBottom };
            FillRect(hdcMem, &rcTextBg, hBgBrush);
            DeleteObject(hBgBrush);

            // Рисуем тонкую рамку вокруг плашки (Акцентный синий VS)
            HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(0, 122, 204));
            HGDIOBJ hOldPen = SelectObject(hdcMem, hBorderPen);
            HGDIOBJ hOldBrush = SelectObject(hdcMem, GetStockObject(NULL_BRUSH));

            // Рисуем рамку
            RoundRect(hdcMem, rectLeft, rectTop, rectRight, rectBottom, 3, 3);

            SelectObject(hdcMem, hOldBrush);
            SelectObject(hdcMem, hOldPen);
            DeleteObject(hBorderPen);

            // Выводим сам текст поверх нарисованной плашки
            SetBkMode(hdcMem, TRANSPARENT);
            SetTextColor(hdcMem, RGB(241, 241, 241)); // Почти белый контрастный текст
            TextOutA(hdcMem, hotspotX, 32, localText.c_str(), (int)localText.length());

            // --- КРИТИЧЕСКИЙ ШАГ: Прошивка Альфа-канала для подложки и текста ---
            // Проходимся по пикселям в области нашей плашки и выставляем им непрозрачность.
            DWORD* pixels = (DWORD*)pBits;

            for (int y = rectTop; y < rectBottom; ++y) {
                for (int x = rectLeft; x < rectRight; ++x) {
                    // Инвертируем Y, так как CreateDIBSection создает буфер снизу вверх
                    int pixelIdx = (OVERLAY_HEIGHT - 1 - y) * OVERLAY_WIDTH + x;

                    if (pixelIdx >= 0 && pixelIdx < OVERLAY_WIDTH * OVERLAY_HEIGHT) {
                        // 0xFC000000 — задает ~98% непрозрачности (плашка слегка сглаживает фон)
                        // Если хотите железно матовую подложку, замените на 0xFF000000
                        pixels[pixelIdx] |= 0xFC000000;
                    }
                }
            }

            SelectObject(hdcMem, hOldFont);
            DeleteObject(hFont);
        }

        // 3. Аппаратный блендинг окна
        POINT ptZero = { 0, 0 };
        SIZE sizeWnd = { OVERLAY_WIDTH, OVERLAY_HEIGHT };
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



    // =========================================================================
    // ОСНОВНОЙ ВНЕШНИЙ МЕТОД: УПРАВЛЕНИЕ ВИДИМОСТЬЮ И ОТРИСОВКОЙ
    // =========================================================================
    // Принимает абсолютные координаты мыши (pt) из вашей Update/Drag функции.
    // Вызывается непрерывно внутри цикла обновлений.
    inline void UpdateOverlayPosition(int screenX, int screenY) {
        if (!s_hOverlayWnd) {
            InitPassiveOverlay();
        }

        // Читаем глобальный флаг видимости
        bool shouldShow = false;
        {
            std::lock_guard<std::mutex> lock(s_OverlayMutex);
            shouldShow = g_OverlayShow;
        }

        if (shouldShow) {
            // 1. Постоянно обновляем содержимое буфера и позицию
            RedrawOverlayBuffer(screenX, screenY);

            // 2. Если окно было скрыто — мягко включаем его без перехвата фокуса
            if (!::IsWindowVisible(s_hOverlayWnd)) {
                ::ShowWindow(s_hOverlayWnd, SW_SHOWNOACTIVATE);
            }
        }
        else {
            // Если флаг сброшен — мгновенно гасим окно с экрана
            if (::IsWindowVisible(s_hOverlayWnd)) {
                ::ShowWindow(s_hOverlayWnd, SW_HIDE);
            }
        }
    }
}
