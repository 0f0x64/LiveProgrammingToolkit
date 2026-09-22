#if LPT_TRIGGER_BUTTON == VK_LBUTTON
#define LPT_WM_BUTTONUP   WM_LBUTTONUP
#elif LPT_TRIGGER_BUTTON == VK_MBUTTON
#define LPT_WM_BUTTONUP   WM_MBUTTONUP
#endif

#include "enumMenu.h"

namespace LivePT {

    inline void HandleMouseDrag(const POINT& pt, bool ctrl, bool shift);
    inline void HandleMouseUp();

    struct DragState {
        bool isDragging = false;
        int targetParamId = -1;
        int oldMouseY = 0;
        int lastValue = 0;
        int oldValue = 0;
        int newValue = 0;

        std::string startTextValue = "";
        bool pointBefore = false;
        long dragLine = 0;

        long dragStartCol = 0;
        size_t currentTextLength = 0;

        long initialCursorAnchorOffset = 0;

        // ВСПЛЫВАЮЩИЕ ПЕРЕМЕННЫЕ ДЛЯ ДАБЛКЛИКА
        DWORD lastClickTime = 0;
        POINT lastClickPt = { 0, 0 };
        std::string oldValueStr;
    };


    static DragState g_dragState;
    static HWND g_hShieldWnd = NULL;
    static DWORD g_vsThreadId = 0; // Запоминаем поток VS для отмены склейки

    // Процедура обработки сообщений невидимого окна-щита
    inline LRESULT CALLBACK ShieldWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == LPT_WM_BUTTONUP) {
            ReleaseCapture();
            DestroyWindow(hwnd);
            g_hShieldWnd = NULL;
            return 0;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    // Функция создания легкого окна-перехватчика со склейкой ввода ОС
    inline void CreateDragShield(const POINT& pt) {
        if (g_hShieldWnd) return;

        // 1. Находим хэндл активного окна Visual Studio через DTE
        CComVariant vtMainWindow;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtMainWindow, pDTE, L"MainWindow", 0)) || !vtMainWindow.pdispVal) return;
        CComVariant vtHWnd;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtHWnd, vtMainWindow.pdispVal, L"HWnd", 0))) return;
        HWND hVSMainWnd = reinterpret_cast<HWND>(static_cast<LONG_PTR>(vtHWnd.lVal));
        if (!hVSMainWnd) return;

        // 2. Находим окно конкретного текстового редактора под курсором
        HWND hEditorWnd = ::WindowFromPoint(pt);
        if (!hEditorWnd) hEditorWnd = hVSMainWnd;

        // 3. СКЛЕИВАЕМ ОЧЕРЕДИ ВВОДА: Привязываем поток нашей игры к потоку Visual Studio
        DWORD currentThreadId = ::GetCurrentThreadId();
        g_vsThreadId = ::GetWindowThreadProcessId(hEditorWnd, NULL);
        if (g_vsThreadId != currentThreadId) {
            ::AttachThreadInput(currentThreadId, g_vsThreadId, TRUE);
        }

        HINSTANCE hInst = GetModuleHandleA(NULL);
        const char* className = "LPT_DragShieldWindow";

        static bool registered = [hInst, className]() {
            WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
            wc.lpfnWndProc = ShieldWndProc;
            wc.hInstance = hInst;
            wc.lpszClassName = className;
            return RegisterClassExA(&wc) != 0;
            }();

        // 4. Создаем окно 1x1 пиксель прямо под курсором мыши
        g_hShieldWnd = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TOPMOST,
            className, "LPT_DragShield", WS_POPUP,
            pt.x, pt.y, 1, 1,
            NULL, NULL, hInst, NULL
        );

        if (g_hShieldWnd) {
            SetLayeredWindowAttributes(g_hShieldWnd, 0, 1, LWA_ALPHA);
            ShowWindow(g_hShieldWnd, SW_SHOW);

            // Теперь, когда потоки склеены, SetCapture заберет ЛКМ из Visual Studio НАПРАМУЮ!
            ::SetCapture(g_hShieldWnd);
        }
    }


    inline bool isMouseDragging() { return g_dragState.isDragging; }

    inline std::string updateFractionalPart(const std::string& original, int delta) {
        if (original.empty()) return "";
        size_t length = original.length();
        if (original.back() == 'f' || original.back() == 'F') length--;
        std::string pureFrac = original.substr(0, length);
        long long limit = 1;
        for (size_t i = 0; i < pureFrac.length(); ++i) limit *= 10;
        long long max_val = limit - 1;
        long long value = std::stoll(pureFrac);
        value += delta;
        if (value > max_val) value = max_val;
        if (value < 0) value = 0;
        std::string result = std::to_string(value);
        if (result.length() < pureFrac.length()) {
            result.insert(0, pureFrac.length() - result.length(), '0');
        }
        return result + "f";
    }

    inline bool ReplaceTextInActiveVS(long line, long visualStartCol, long visualEndCol, const std::string& newText, long newCursorPhysicalCol) {
        if (!pDTE) return false;

        CComVariant vtActiveDoc;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0)) || !vtActiveDoc.pdispVal) return false;

        CComVariant vtSelection;
        if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, vtActiveDoc.pdispVal, L"Selection", 0)) && vtSelection.vt == VT_DISPATCH) {

            std::wstring fileText = DownloadDocumentText(vtActiveDoc.pdispVal);

            size_t lineStartOffset = 0; long currentLineIdx = 1;
            while (currentLineIdx < line && lineStartOffset < fileText.length()) {
                size_t nextNL = fileText.find(L'\n', lineStartOffset);
                if (nextNL != std::wstring::npos) { lineStartOffset = nextNL + 1; currentLineIdx++; }
                else break;
            }

            size_t lineEndOffset = fileText.find(L'\n', lineStartOffset);
            if (lineEndOffset == std::wstring::npos) lineEndOffset = fileText.length();
            std::wstring lineText = fileText.substr(lineStartOffset, lineEndOffset - lineStartOffset);

            long physicalStartCol = 1; long physicalEndCol = 1;
            const size_t TAB_SIZE = 4; size_t currentVisualCol = 1;

            size_t i = 0;
            for (; i < lineText.length(); ++i) {
                if (currentVisualCol >= static_cast<size_t>(visualStartCol)) break;
                currentVisualCol += (lineText[i] == L'\t') ? (TAB_SIZE - ((currentVisualCol - 1) % TAB_SIZE)) : 1;
            }
            physicalStartCol = static_cast<long>(i) + 1;

            for (; i < lineText.length(); ++i) {
                if (currentVisualCol >= static_cast<size_t>(visualEndCol)) break;
                currentVisualCol += (lineText[i] == L'\t') ? (TAB_SIZE - ((currentVisualCol - 1) % TAB_SIZE)) : 1;
            }
            physicalEndCol = static_cast<long>(i) + 1;

            CComVariant vtActivePoint;
            HRESULT hr = AutoWrap(DISPATCH_PROPERTYGET, &vtActivePoint, vtSelection.pdispVal, L"ActivePoint", 0);
            if (FAILED(hr) || vtActivePoint.vt != VT_DISPATCH) return false;

            CComVariant pEditStart; hr = AutoWrap(DISPATCH_METHOD, &pEditStart, vtActivePoint.pdispVal, L"CreateEditPoint", 0);
            if (FAILED(hr) || pEditStart.vt != VT_DISPATCH) return false;

            CComVariant pEditEnd; hr = AutoWrap(DISPATCH_METHOD, &pEditEnd, vtActivePoint.pdispVal, L"CreateEditPoint", 0);
            if (FAILED(hr) || pEditEnd.vt != VT_DISPATCH) return false;

            hr = AutoWrap(DISPATCH_METHOD, NULL, pEditStart.pdispVal, L"MoveToLineAndOffset", 2, CComVariant(line), CComVariant(physicalStartCol));
            if (FAILED(hr)) return false;

            hr = AutoWrap(DISPATCH_METHOD, NULL, pEditEnd.pdispVal, L"MoveToLineAndOffset", 2, CComVariant(line), CComVariant(physicalEndCol));
            if (FAILED(hr)) return false;

            std::wstring wText(newText.begin(), newText.end()); CComBSTR bstrText(wText.c_str());
            CComVariant vText(bstrText); CComVariant vOption(1L);

            hr = AutoWrap(DISPATCH_METHOD, NULL, pEditStart.pdispVal, L"ReplaceText", 3, pEditEnd, vText, vOption);
            if (FAILED(hr)) return false;

            AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"MoveToLineAndOffset", 3, CComVariant(line), CComVariant(newCursorPhysicalCol), CComVariant(0L));
        } return true;
    }

    inline void SaveActiveDocument() {
        if (!pDTE) return;

        VARIANT vtActiveDoc;
        VariantInit(&vtActiveDoc);

        if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0)) && vtActiveDoc.pdispVal) {

            std::string currentFile = GetActiveDocumentPath(vtActiveDoc.pdispVal);

            if (!currentFile.empty()) {
                std::string pathLower = currentFile;
                std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);

                bool isShader = (pathLower.find(".hlsl") != std::string::npos ||
                    pathLower.find(".glsl") != std::string::npos ||
                    pathLower.find(".shader") != std::string::npos ||
                    pathLower.find(".frag") != std::string::npos ||
                    pathLower.find(".vert") != std::string::npos);

                if (isShader) {
                    AutoWrap(DISPATCH_METHOD, NULL, vtActiveDoc.pdispVal, L"Save", 0);
                }
            }
        }
        VariantClear(&vtActiveDoc);
    }

    inline bool GetActiveVSContext(VARIANT& outActiveDoc, std::string& outFilePath, long& outLine, long& outColumn, std::wstring& outDocText) {
        VariantInit(&outActiveDoc);
        HRESULT hr = pDTE ? AutoWrap(DISPATCH_PROPERTYGET, &outActiveDoc, pDTE, L"ActiveDocument", 0) : E_FAIL;
        if (FAILED(hr) || !outActiveDoc.pdispVal) { VariantClear(&outActiveDoc); return false; }

        IDispatch* pActiveDoc = outActiveDoc.pdispVal;
        outFilePath = GetActiveDocumentPath(pActiveDoc);
        if (outFilePath.empty() || !GetCursorCoordinates(pActiveDoc, outLine, outColumn)) {
            VariantClear(&outActiveDoc);
            return false;
        }

        outDocText = DownloadDocumentText(pActiveDoc);
        return true;
    }

    inline bool ParseMacroValueBoundaries(const std::wstring& fileText, long line, size_t targetEvalAbsolutePos) {
        size_t openBracket = fileText.find(L'(', targetEvalAbsolutePos);
        size_t closeBracket = FindCloseBracket(fileText, openBracket);

        if (openBracket == std::wstring::npos || closeBracket == std::wstring::npos) return false;

        g_dragState.dragLine = line;

        size_t lineStartOffset = 0; long currentLineIdx = 1;
        while (currentLineIdx < line) { lineStartOffset = fileText.find(L'\n', lineStartOffset) + 1; currentLineIdx++; }

        size_t lineEndOffset = fileText.find(L'\n', lineStartOffset);
        if (lineEndOffset == std::wstring::npos) lineEndOffset = fileText.length();
        std::wstring wholeLineText = fileText.substr(lineStartOffset, lineEndOffset - lineStartOffset);

        g_dragState.dragStartCol = GetVisualColumn(wholeLineText, openBracket - lineStartOffset) + 1;
        g_dragState.currentTextLength = closeBracket - openBracket - 1;

        std::wstring innerW = fileText.substr(openBracket + 1, g_dragState.currentTextLength);
        std::string cleanText(innerW.begin(), innerW.end());
        cleanText.erase(0, cleanText.find_first_not_of(" \t\r\n"));
        cleanText.erase(cleanText.find_last_not_of(" \t\r\n") + 1);
        g_dragState.startTextValue = cleanText;

        return true;
    }

    inline void InitMultiEnumSelection(int id) {
        g_dragState.isDragging = false;

        // Он-деманд загрузка из PDB строго один раз при первом клике
        if (paramDesc[id].enumInfo.elements.empty()) {
            std::wstring wEnumName = L"";
            size_t lastCols = g_dragState.startTextValue.rfind("::");
            if (lastCols != std::string::npos) {
                std::string pureTypeName = g_dragState.startTextValue.substr(0, lastCols);
                wEnumName = std::wstring(pureTypeName.begin(), pureTypeName.end());
            }
            else {
                wEnumName = std::wstring(g_dragState.startTextValue.begin(), g_dragState.startTextValue.end());
            }

            if (!wEnumName.empty()) {
                std::vector<std::string> parsedNames;
                std::vector<int> parsedValues;
                if (LoadEnumMetadataDirect(wEnumName.c_str(), parsedNames, parsedValues)) {
                    for (size_t i = 0; i < parsedNames.size(); ++i) {
                        EnumElementDesc gameElem{ parsedValues[i], parsedNames[i] };
                        paramDesc[id].enumInfo.elements.push_back(gameElem);
                    }
                }
            }
        }

        int totalElements = static_cast<int>(paramDesc[id].enumInfo.elements.size());
        if (totalElements == 0) {
            g_dragState.targetParamId = -1;
            return;
        }

        // САМ РЕШАЕТ: Если элементов ровно 2 — берем противоположный и выходим!
        if (totalElements == 2) {
            int currentVal = std::any_cast<int>(paramDesc[id].value);

            // ПРАВИЛЬНЫЙ ИСПРАВЛЕННЫЙ ИНДЕКС: ищем текущее значение в векторе
            int currentIndex = (paramDesc[id].enumInfo.elements[0].value == currentVal) ? 0 : 1;
            int newIndex = 1 - currentIndex;

            int targetEnumValue = paramDesc[id].enumInfo.elements[newIndex].value;
            std::string pureName = paramDesc[id].enumInfo.elements[newIndex].name;
            std::string newValueStr = pureName;

            size_t lastCols = g_dragState.startTextValue.rfind("::");
            if (lastCols != std::string::npos) {
                std::string prefix = g_dragState.startTextValue.substr(0, lastCols + 2);
                newValueStr = prefix + pureName;
            }

            long newCursorRelPos = static_cast<long>(newValueStr.length()) - g_dragState.initialCursorAnchorOffset;
            if (newCursorRelPos < 0) newCursorRelPos = 0;
            long newCursorPhysicalCol = g_dragState.dragStartCol + newCursorRelPos;

            StartUndoTransaction(L"LiveWheel Change Enum");
            ReplaceTextInActiveVS(g_dragState.dragLine, g_dragState.dragStartCol, g_dragState.dragStartCol + static_cast<long>(g_dragState.currentTextLength), newValueStr, newCursorPhysicalCol);
            EndUndoTransaction();

            g_dragState.currentTextLength = newValueStr.length();
            paramDesc[id].value = targetEnumValue;
            UpdateParamValue(id, pureName);

            SaveActiveDocument();
            g_dragState.targetParamId = -1;
            return; // Мгновенный выход, меню не создается!
        }

        // Если элементов больше 2 — спокойно строим и выводим меню
        std::vector<std::string> enumMenu;
        for (int i = 0; i < totalElements; i++) {
            enumMenu.push_back(paramDesc[id].enumInfo.elements[i].name);
        }

        int newIndex = showEnum(enumMenu);

        if (newIndex >= 0 && newIndex < totalElements) {
            int targetEnumValue = paramDesc[id].enumInfo.elements[newIndex].value;
            std::string pureName = paramDesc[id].enumInfo.elements[newIndex].name;
            std::string newValueStr = pureName;

            size_t lastCols = g_dragState.startTextValue.rfind("::");
            if (lastCols != std::string::npos) {
                std::string prefix = g_dragState.startTextValue.substr(0, lastCols + 2);
                newValueStr = prefix + pureName;
            }

            long newCursorRelPos = static_cast<long>(newValueStr.length()) - g_dragState.initialCursorAnchorOffset;
            if (newCursorRelPos < 0) newCursorRelPos = 0;
            long newCursorPhysicalCol = g_dragState.dragStartCol + newCursorRelPos;

            StartUndoTransaction(L"LiveWheel Change Enum");
            ReplaceTextInActiveVS(g_dragState.dragLine, g_dragState.dragStartCol, g_dragState.dragStartCol + static_cast<long>(g_dragState.currentTextLength), newValueStr, newCursorPhysicalCol);
            EndUndoTransaction();

            g_dragState.currentTextLength = newValueStr.length();
            paramDesc[id].value = targetEnumValue;
            UpdateParamValue(id, pureName);

            SaveActiveDocument();
        }
        g_dragState.targetParamId = -1;
    }




    inline void InitNumericDragState(size_t cursorIdxInRaw, const std::string& cleanText) {
        g_dragState.isDragging = true;
        StartUndoTransaction(L"LiveWheel Live Edit");

        // СОХРАНЯЕМ ИСХОДНУЮ СТРОКУ ЧИСЛА ДЛЯ ФИКСА СТРУКТУР
        g_dragState.oldValueStr = cleanText;

        long relativeOffset = static_cast<long>(cursorIdxInRaw);
        // ... (весь остальной ваш код метода InitNumericDragState остается прежним)


        size_t dotPos = cleanText.find('.');
        g_dragState.pointBefore = (dotPos != std::string::npos && relativeOffset > static_cast<long>(dotPos));
        g_dragState.initialCursorAnchorOffset = (dotPos != std::string::npos) ? std::abs(static_cast<long>(dotPos) - relativeOffset) : static_cast<long>(cleanText.length()) - relativeOffset;

        if (dotPos == std::string::npos) g_dragState.oldValue = atoi(cleanText.c_str());
        else g_dragState.oldValue = atoi(g_dragState.pointBefore ? cleanText.substr(dotPos + 1).c_str() : cleanText.substr(0, dotPos).c_str());
        g_dragState.lastValue = g_dragState.oldValue;
    }

    inline bool HandleMouseDown(const POINT& pt) {
        if (!initVsEditor()) return false;

        VARIANT vtActiveDoc;
        std::string currentFile;
        long line = 0, column = 0;
        std::wstring fileText;

        bool clickedInsideNumber = false;

        if (GetActiveVSContext(vtActiveDoc, currentFile, line, column, fileText)) {
            // 1. Получаем текст только одной текущей строки, чтобы всё летало
            std::wstring lineText = DownloadCurrentLineText(vtActiveDoc.pdispVal);
            long cursorColIdx = column - 1; // Переводим в 0-индексируемую координату

            if (cursorColIdx >= 0 && cursorColIdx < static_cast<long>(lineText.length())) {
                // 2. Ищем физические границы числа под курсором (влево и вправо)
                long startCol = cursorColIdx;
                long endCol = cursorColIdx;

                // Двигаемся влево, пока видим цифры, точки, минусы или знаки 'f'
                while (startCol > 0 && (iswdigit(lineText[startCol - 1]) || lineText[startCol - 1] == L'.' || lineText[startCol - 1] == L'-' || lineText[startCol - 1] == L'f' || lineText[startCol - 1] == L'F')) {
                    startCol--;
                }
                // Двигаемся вправо
                while (endCol < static_cast<long>(lineText.length()) && (iswdigit(lineText[endCol]) || lineText[endCol] == L'.' || lineText[endCol] == L'-' || lineText[endCol] == L'f' || lineText[endCol] == L'F')) {
                    endCol++;
                }

                // 3. Проверяем, нашли ли мы валидное число
                if (startCol < endCol) {
                    std::wstring numW = lineText.substr(startCol, endCol - startCol);
                    std::string cleanText(numW.begin(), numW.end());

                    g_dragState.dragLine = line;
                    g_dragState.dragStartCol = startCol + 1;
                    g_dragState.currentTextLength = cleanText.length();
                    g_dragState.startTextValue = cleanText;
                    g_dragState.oldMouseY = pt.y;

                    // === ТОЧЕЧНЫЙ ФИКС: Возвращаем сохранение targetParamId ===
                    // Мы используем уже вычисленный в начале HandleMouseDown ID параметра
                    // Для этого временно вернем вызов CheckCursorInsideEval СТРОГО в момент клика (тут он не заблокирован!)
                    size_t evalIdxInLine = 0, targetEvalAbsolutePos = 0;
                    if (CheckCursorInsideEval(fileText, line, column, evalIdxInLine, targetEvalAbsolutePos)) {
                        int counterID = GetParamIndexByTextOrder(fileText, currentFile, line, evalIdxInLine);
                        if (counterID != -1) {
                            std::string vsLookupKey = currentFile + ":" + std::to_string(counterID);
                            g_dragState.targetParamId = getID(vsLookupKey);
                        }
                    }
                    // =========================================================

                    size_t relativeCursorIdx = cursorColIdx - startCol;
                    InitNumericDragState(relativeCursorIdx, cleanText);

                    clickedInsideNumber = true;
                }

            }
            VariantClear(&vtActiveDoc);
        }
        return clickedInsideNumber;
    }

    inline void DragNumericValue(int id, const POINT& pt, bool ctrl, bool shift) {
        int scale = 1;
        if (ctrl)  scale *= 100;
        if (shift) scale *= 10;

        int delta = -(pt.y - g_dragState.oldMouseY) * scale / 2;

        // Новое значение вычисляем свободно, без жестких лимитов paramDesc
        long long targetValue = static_cast<long long>(g_dragState.oldValue) + delta;

        g_dragState.newValue = static_cast<int>(targetValue);

        if (g_dragState.newValue != g_dragState.lastValue) {
            size_t dotPos = g_dragState.startTextValue.find('.');
            std::string newValueStr;

            if (dotPos == std::string::npos) {
                char modified[100];
                _itoa_s(g_dragState.newValue, modified, sizeof(modified), 10);
                newValueStr = modified;
            }
            else {
                // ... (Весь ваш ОРИГИНАЛЬНЫЙ и красивый математический код разбора целой/дробной частей с сохранением precision остается НЕИЗМЕННЫМ) ...
                std::string intPartStr = g_dragState.startTextValue.substr(0, dotPos);
                std::string fracPartStr = g_dragState.startTextValue.substr(dotPos + 1);
                std::string suffix = "";
                if (!fracPartStr.empty() && (fracPartStr.back() == 'f' || fracPartStr.back() == 'F')) {
                    suffix = fracPartStr.back();
                    fracPartStr.pop_back();
                }
                size_t precision = fracPartStr.length();
                if (g_dragState.pointBefore) {
                    long long fracLimit = 1;
                    for (size_t i = 0; i < precision; ++i) fracLimit *= 10;
                    long long currentIntVal = std::stoll(intPartStr);
                    long long currentFracVal = fracPartStr.empty() ? 0 : std::stoll(fracPartStr);
                    long long totalUnits = currentIntVal * fracLimit;
                    if (currentIntVal < 0 || intPartStr[0] == '-') totalUnits -= currentFracVal;
                    else totalUnits += currentFracVal;
                    totalUnits += delta;
                    long long newIntVal = totalUnits / fracLimit;
                    long long newFracVal = std::abs(totalUnits % fracLimit);
                    std::string newIntStr = std::to_string(newIntVal);
                    if (totalUnits < 0 && newIntVal == 0) newIntStr = "-" + newIntStr;
                    std::string newFracStr = std::to_string(newFracVal);
                    if (newFracStr.length() < precision) newFracStr.insert(0, precision - newFracStr.length(), '0');
                    newValueStr = newIntStr + "." + newFracStr + suffix;
                }
                else {
                    char modified[100];
                    _itoa_s(g_dragState.newValue, modified, sizeof(modified), 10);
                    newValueStr = std::string(modified) + "." + fracPartStr + suffix;
                }
            }

            size_t newDotPos = newValueStr.find('.');
            long newCursorRelPos = 0;
            if (newDotPos != std::string::npos) {
                if (g_dragState.pointBefore) newCursorRelPos = static_cast<long>(newDotPos) + g_dragState.initialCursorAnchorOffset;
                else newCursorRelPos = static_cast<long>(newDotPos) - g_dragState.initialCursorAnchorOffset;
            }
            else {
                newCursorRelPos = static_cast<long>(newValueStr.length()) - g_dragState.initialCursorAnchorOffset;
            }
            newCursorRelPos = std::clamp(newCursorRelPos, 0L, static_cast<long>(newValueStr.length()));
            long newCursorPhysicalCol = g_dragState.dragStartCol + newCursorRelPos;

            // 1. Выполняем физическую замену текста в Visual Studio (уже есть в коде)
            ReplaceTextInActiveVS(g_dragState.dragLine, g_dragState.dragStartCol, g_dragState.dragStartCol + static_cast<long>(g_dragState.currentTextLength), newValueStr, newCursorPhysicalCol);

            g_dragState.currentTextLength = newValueStr.length();

            // ============================================================================
            // ИСПРАВЛЕННЫЙ ТОЧЕЧНЫЙ ФИКС: ПОКАДРОВЫЙ ДРАГ СТРУКТУР И ЧИСЕЛ ИЗ СТРОКИ КОДА
            // ============================================================================
            int targetId = g_dragState.targetParamId;
            if (targetId != -1) {
                CComVariant vtActiveDoc;
                if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0)) && vtActiveDoc.pdispVal) {

                    // Выкачиваем текст ОДНОЙ текущей строки (этот метод работает покадрово и не блокируется!)
                    std::wstring currentLineText = DownloadCurrentLineText(vtActiveDoc.pdispVal);
                    std::string fullLineA(currentLineText.begin(), currentLineText.end());

                    // Находим макрос eval на этой строке. Чтобы не ошибиться, ищем кусок строки, начиная от нашего числа
                    size_t evalPos = fullLineA.find("eval");
                    if (evalPos != std::string::npos) {
                        std::string cleanLinePart = fullLineA.substr(evalPos);

                        size_t openBracket = cleanLinePart.find('(');
                        // Ищем закрывающую скобку макроса eval
                        size_t closeBracket = cleanLinePart.rfind(')');

                        if (openBracket != std::string::npos && closeBracket != std::string::npos && closeBracket > openBracket) {
                            // Вырезаем абсолютно всё, что находится внутри eval(...)
                            // Для структуры это будет актуальный "Primitive::color3{0, 0, 245}"
                            // Для обычного числа это будет просто актуальный "43" или "44"
                            std::string innerMacroText = cleanLinePart.substr(openBracket + 1, closeBracket - openBracket - 1);

                            // Очищаем от пробелов по краям
                            innerMacroText.erase(0, innerMacroText.find_first_not_of(" \t\r\n"));
                            innerMacroText.erase(innerMacroText.find_last_not_of(" \t\r\n") + 1);

                            // Прямой покадровый укол в память игры! Универсально работает и для чисел, и для структур
                            UpdateParamValue(targetId, innerMacroText);
                        }
                    }
                    VariantClear(&vtActiveDoc);
                }
            }
            // ============================================================================

            g_dragState.lastValue = g_dragState.newValue;
        }
    }



    inline void HandleMouseDrag(const POINT& pt, bool ctrl, bool shift) {
        // ИСПРАВЛЕНО: Проверяем только флаг драга, так как мышь теперь текстовая
        if (!g_dragState.isDragging) return;

        // Создаем шилд со склейкой потоков строго в момент НАЧАЛА движения
        if (!g_hShieldWnd) {
            CreateDragShield(pt);
        }

        // Передаем 0 вместо ID, так как внутри функции ID больше не используется
        DragNumericValue(0, pt, ctrl, shift);
    }


    inline void HandleMouseUp() {
        // ИСПРАВЛЕНО: Проверяем факт драга, а не ID параметра
        if (!g_dragState.isDragging) {
            if (g_hShieldWnd) {
                ReleaseCapture();
                DestroyWindow(g_hShieldWnd);
                g_hShieldWnd = NULL;
            }
            if (g_vsThreadId != 0) {
                ::AttachThreadInput(::GetCurrentThreadId(), g_vsThreadId, FALSE);
                g_vsThreadId = 0;
            }
            return;
        }

        EndUndoTransaction();
        SaveActiveDocument();

        g_dragState.isDragging = false;
        g_dragState.targetParamId = -1;

        if (g_hShieldWnd) {
            ReleaseCapture();
            DestroyWindow(g_hShieldWnd);
            g_hShieldWnd = NULL;
        }

        if (g_vsThreadId != 0) {
            ::AttachThreadInput(::GetCurrentThreadId(), g_vsThreadId, FALSE);
            g_vsThreadId = 0;
        }
    }

    


    inline bool IsCursorOverActiveVSWindow() {
        if (!pDTE) return false;

        HWND hForeground = ::GetForegroundWindow();
        if (!hForeground) return false;

        CComVariant vtMainWindow;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtMainWindow, pDTE, L"MainWindow", 0)) || !vtMainWindow.pdispVal) {
            return false;
        }

        CComVariant vtHWnd;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtHWnd, vtMainWindow.pdispVal, L"HWnd", 0))) {
            return false;
        }

        HWND hVSMainWnd = reinterpret_cast<HWND>(static_cast<LONG_PTR>(vtHWnd.lVal));
        if (!hVSMainWnd) return false;

        if (hForeground != hVSMainWnd && !::IsChild(hVSMainWnd, hForeground)) {
            return false;
        }

        POINT pt;
        if (::GetCursorPos(&pt)) {
            HWND hWindowUnderCursor = ::WindowFromPoint(pt);
            if (hWindowUnderCursor != hVSMainWnd && !::IsChild(hVSMainWnd, hWindowUnderCursor)) {
                return false;
            }
        }

        return true;
    }

    inline void Update() {
        POINT pt;
        GetCursorPos(&pt);

        bool buttonDown = (GetAsyncKeyState(LPT_TRIGGER_BUTTON) & 0x8000) != 0;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        static bool s_PrevLButtonDown = false;

        if (buttonDown) {
            if (!s_PrevLButtonDown && !g_dragState.isDragging) {
                if (IsCursorOverActiveVSWindow()) {
                    // Если под курсором нашлось число — HandleMouseDown вернет true
                    if (HandleMouseDown(pt)) {
                        g_dragState.isDragging = true;
                    }
                }
            }
            if (g_dragState.isDragging) {
                HandleMouseDrag(pt, ctrl, shift);
            }
        }

        else {
            if (s_PrevLButtonDown && g_dragState.isDragging) {
                HandleMouseUp();
            }
        }

        // Прокачиваем очередь сообщений шилда для отлова WM_LBUTTONUP
        if (g_hShieldWnd) {
            MSG msg;
            while (PeekMessageA(&msg, g_hShieldWnd, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }

        s_PrevLButtonDown = buttonDown;
    }



} 
