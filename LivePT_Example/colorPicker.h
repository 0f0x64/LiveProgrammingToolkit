#include <commdlg.h>
#include <string>
#include <thread>
#include <functional>
#include <objbase.h>

static const UINT WM_COLOROK_MSG = RegisterWindowMessageA("commdlg_ColorOK");

// Контекст для проброса данных в фоновый поток и хук
struct RealtimeColorContext {
    std::function<void(std::string)> updateVsCallback;
    COLORREF customColors[16];
    POINT clickPt; // ДОБАВЛЕНО: Координаты клика мыши из игры
};

UINT_PTR CALLBACK RealtimeColorHook(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_INITDIALOG) {
        CHOOSECOLORA* cc = reinterpret_cast<CHOOSECOLORA*>(lParam);
        RealtimeColorContext* ctx = reinterpret_cast<RealtimeColorContext*>(cc->lpCustColors);
        SetWindowLongPtrA(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

        // ЦЕНТРИРОВАНИЕ ОКНА ПО КЛИКУ С ЗАЩИТОЙ ОТ ВЫЛЕТА ЗА ЭКРАН
        RECT rcDlg;
        GetWindowRect(hDlg, &rcDlg);
        int dlgWidth = rcDlg.right - rcDlg.left;
        int dlgHeight = rcDlg.bottom - rcDlg.top;

        // Рассчитываем идеальные координаты, чтобы центр окна был в точке клика
        int targetX = ctx->clickPt.x - (dlgWidth / 2);
        int targetY = ctx->clickPt.y - (dlgHeight / 2);

        // Получаем размеры текущего монитора (с учетом панели задач, rcWork)
        HMONITOR hMonitor = MonitorFromPoint(ctx->clickPt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (GetMonitorInfoA(hMonitor, &mi)) {
            // Корректируем по оси X (левая и правая границы)
            if (targetX < mi.rcWork.left) targetX = mi.rcWork.left;
            if (targetX + dlgWidth > mi.rcWork.right) targetX = mi.rcWork.right - dlgWidth;

            // Корректируем по оси Y (верхняя и нижняя границы)
            if (targetY < mi.rcWork.top) targetY = mi.rcWork.top;
            if (targetY + dlgHeight > mi.rcWork.bottom) targetY = mi.rcWork.bottom - dlgHeight;
        }

        // Перемещаем окно пикера в безопасную позицию
        SetWindowPos(hDlg, HWND_TOP, targetX, targetY, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        return 1;
    }

    if (uMsg == WM_COMMAND || uMsg == WM_LBUTTONUP || uMsg == WM_MOUSEMOVE || uMsg == WM_COLOROK_MSG) {
        LONG_PTR userData = GetWindowLongPtrA(hDlg, GWLP_USERDATA);
        if (userData) {
            RealtimeColorContext* ctx = reinterpret_cast<RealtimeColorContext*>(userData);

            int rVal = GetDlgItemInt(hDlg, 0x02C2, NULL, FALSE);
            int gVal = GetDlgItemInt(hDlg, 0x02C3, NULL, FALSE);
            int bVal = GetDlgItemInt(hDlg, 0x02C4, NULL, FALSE);

            if (rVal <= 255 && gVal <= 255 && bVal <= 255) {
                std::string code = "color3{ " + std::to_string(rVal) + ", " + std::to_string(gVal) + ", " + std::to_string(bVal) + " }";
                ctx->updateVsCallback(code);
            }
        }
    }
    return 0;
}

void AsyncColorPickerWorker(HWND hParentWnd, color3 currentColor, RealtimeColorContext* pSharedCtx) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    CHOOSECOLORA cc = { 0 };
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hParentWnd;
    cc.rgbResult = RGB(currentColor.r, currentColor.g, currentColor.b);
    cc.lpCustColors = reinterpret_cast<COLORREF*>(pSharedCtx); // Передаем контекст с точкой клика
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ENABLEHOOK;
    cc.lpfnHook = RealtimeColorHook;

    if (ChooseColorA(&cc) == FALSE) {
        std::string rollback = "color3{ " + std::to_string(currentColor.r) + ", " + std::to_string(currentColor.g) + ", " + std::to_string(currentColor.b) + " }";
        pSharedCtx->updateVsCallback(rollback);
    }

    delete pSharedCtx; // Удаляем контекст из кучи по завершении работы потока
    CoUninitialize();
}

void MyColorPickerCallback(const color3& currentColor, std::function<void(std::string)> updateVsCallback) {
    HWND hActiveGameWnd = GetActiveWindow();
    if (!hActiveGameWnd) hActiveGameWnd = GetForegroundWindow();

    // Захватываем текущие координаты курсора мыши на экране в момент вызова (клик в игре)
    POINT pt;
    GetCursorPos(&pt);

    // Выделяем контекст в куче, чтобы он гарантированно жил, пока работает асинхронный поток
    RealtimeColorContext* pCtx = new RealtimeColorContext();
    pCtx->updateVsCallback = updateVsCallback;
    pCtx->clickPt = pt;
    for (int i = 0; i < 16; ++i) pCtx->customColors[i] = RGB(255, 255, 255);

    std::thread t(AsyncColorPickerWorker, hActiveGameWnd, currentColor, pCtx);
    t.detach();
}
