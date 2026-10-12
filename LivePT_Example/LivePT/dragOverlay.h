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
    const int OVERLAY_WIDTH = 250;
    const int OVERLAY_HEIGHT = 50;

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
    inline void RedrawOverlayBuffer(int screenX, int screenY) {
        if (!s_hOverlayWnd) return; //

        // Внутренний кэш состояний для защиты от лишних перерисовок
        static HCURSOR     s_LastLoadedCursor = NULL; //
        static std::string s_LastLoadedText = ""; //

        HCURSOR     localCursor; //
        std::string localText; //
        {
            std::lock_guard<std::mutex> lock(s_OverlayMutex); //
            localCursor = g_OverlayCursor; //
            localText = g_OverlayText; //
        } //

        // Вычисляем Hotspot курсора (точку привязки)
        int hotspotX = 0;
        int hotspotY = 0;

        if (localCursor) {
            ICONINFO iconInfo = { 0 };
            if (::GetIconInfo(localCursor, &iconInfo)) {
                hotspotX = (int)iconInfo.xHotspot;
                hotspotY = (int)iconInfo.yHotspot;

                // Обязательно удаляем созданные GetIconInfo битмапы, чтобы избежать утечки памяти!
                if (iconInfo.hbmMask)  ::DeleteObject(iconInfo.hbmMask);
                if (iconInfo.hbmColor) ::DeleteObject(iconInfo.hbmColor);
            }
        }

        // ОФСЕТ: Если текст и курсор не изменились, просто мгновенно двигаем окно с учетом Hotspot
        if (localCursor == s_LastLoadedCursor && localText == s_LastLoadedText) {
            // Вычитаем hotspotX и hotspotY, чтобы точка клика совпала с центром/острием курсора
            int targetX = screenX - hotspotX;
            int targetY = screenY - hotspotY;

            ::SetWindowPos(s_hOverlayWnd, HWND_TOPMOST, targetX, targetY, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW); //
            return; //
        }

        s_LastLoadedCursor = localCursor; //
        s_LastLoadedText = localText; //

        HDC hdcScreen = GetDC(NULL); //
        HDC hdcMem = CreateCompatibleDC(hdcScreen); //

        BITMAPINFO bmi = { 0 }; //
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); //
        bmi.bmiHeader.biWidth = OVERLAY_WIDTH; //
        bmi.bmiHeader.biHeight = OVERLAY_HEIGHT; //
        bmi.bmiHeader.biPlanes = 1; //
        bmi.bmiHeader.biBitCount = 32; //
        bmi.bmiHeader.biCompression = BI_RGB; //

        void* pBits = nullptr; //
        HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0); //
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp); //

        memset(pBits, 0, OVERLAY_WIDTH * OVERLAY_HEIGHT * 4); //

        if (localCursor) {
            // Рисуем курсор в левый верхний угол (0, 0) нашего холста
            DrawIcon(hdcMem, 0, 0, localCursor); //
        }

        if (!localText.empty()) { //
            SetBkMode(hdcMem, TRANSPARENT); //
            SetTextColor(hdcMem, RGB(220, 220, 220)); //

            HFONT hFont = CreateFontA(
                13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, //
                VARIABLE_PITCH, "Consolas"
            ); //
            HGDIOBJ hOldFont = SelectObject(hdcMem, hFont); //

            // Рисуем текст. Смещение по X делаем равным hotspotX, чтобы текст 
            // начинался ровно по вертикальной оси курсора, а не улетал влево.
            // Смещение по Y = 32 пикселя (строго под иконку курсора)
            TextOutA(hdcMem, hotspotX, 32, localText.c_str(), (int)localText.length());

            DWORD* pixels = (DWORD*)pBits; //
            int totalPixels = OVERLAY_WIDTH * OVERLAY_HEIGHT; //
            for (int i = 0; i < totalPixels; ++i) { //
                if (pixels[i] != 0x00000000) { //
                    pixels[i] |= 0xFF000000; //
                } //
            } //

            SelectObject(hdcMem, hOldFont); //
            DeleteObject(hFont); //
        } //

        POINT ptZero = { 0, 0 }; //
        SIZE sizeWnd = { OVERLAY_WIDTH, OVERLAY_HEIGHT }; //
        BLENDFUNCTION blend = { 0 }; //
        blend.BlendOp = AC_SRC_OVER; //
        blend.SourceConstantAlpha = 255; //
        blend.AlphaFormat = AC_SRC_ALPHA; //

        // ПРИВЯЗКА ОКНА К ЭКРАНУ: Точное позиционирование окна оверлея с учетом Hotspot
        POINT ptWnd = { screenX - hotspotX, screenY - hotspotY };

        UpdateLayeredWindow(s_hOverlayWnd, hdcScreen, &ptWnd, &sizeWnd, hdcMem, &ptZero, 0, &blend, ULW_ALPHA); //

        SelectObject(hdcMem, hOldBmp); //
        DeleteObject(hBmp); //
        DeleteDC(hdcMem); //
        ReleaseDC(NULL, hdcScreen); //
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
