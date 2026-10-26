#if LivePT_TriggerButton == VK_LBUTTON
#define LPT_WM_BUTTONUP   WM_LBUTTONUP
#elif LivePT_TriggerButton == VK_MBUTTON
#define LPT_WM_BUTTONUP   WM_MBUTTONUP
#endif

#pragma comment(lib, "User32.lib")

#include "enumMenu.h"
namespace Widgets {

    inline float GetWidgetScale(HWND hwnd = NULL) {
        if (hwnd && ::IsWindow(hwnd)) {
            UINT dpi = ::GetDpiForWindow(hwnd);
            if (dpi > 0) return static_cast<float>(dpi) / 96.0f;
        }
        HDC hdc = ::GetDC(NULL);
        int dpiY = ::GetDeviceCaps(hdc, LOGPIXELSY);
        ::ReleaseDC(NULL, hdc);
        return static_cast<float>(dpiY) / 96.0f;
    }
}
namespace LivePT {

    inline void SaveActiveDocument();
    inline bool ReplaceTextInActiveVS(long line, long visualStartCol, long visualEndCol, const std::string& newText, long newCursorPhysicalCol);
    

    inline void HandleMouseDrag(const POINT& pt, bool ctrl, bool shift);
    inline void HandleMouseUp();

    struct ExtractedArg {
        std::string text;
        long startColOffset;
        long endColOffset;
    };

    inline std::vector<ExtractedArg> TokenizeCallArguments(const std::wstring& lineText, size_t openBracketPos) {
        std::vector<ExtractedArg> args;
        if (openBracketPos == std::wstring::npos) return args;

        size_t i = openBracketPos + 1;
        size_t currentArgStart = i;
        int bracketCount = 0;
        int templateCount = 0;

        while (i < lineText.length()) {
            wchar_t ch = lineText[i];

            if (ch == L'(' || ch == L'{') bracketCount++;
            else if (ch == L')' || ch == L'}') {
                if (bracketCount == 0) {
                    if (i > currentArgStart) {
                        std::wstring argW = lineText.substr(currentArgStart, i - currentArgStart);
                        std::string argA = ConvertWStringToUtf8(argW);
                        args.push_back({ argA, static_cast<long>(currentArgStart + 1), static_cast<long>(i + 1) });
                    }
                    break;
                }
                bracketCount--;
            }
            else if (ch == L'<') templateCount++;
            else if (ch == L'>') templateCount--;
            else if (ch == L',' && bracketCount == 0 && templateCount == 0) {
                std::wstring argW = lineText.substr(currentArgStart, i - currentArgStart);
                std::string argA = ConvertWStringToUtf8(argW); // Было: std::string argA(argW.begin(), argW.end());
                args.push_back({ argA, static_cast<long>(currentArgStart + 1), static_cast<long>(i + 1) });
                currentArgStart = i + 1;
            }
            i++;
        }
        return args;
    }

    static IDispatch* pWidgetStartEditPoint = nullptr;
    static IDispatch* pWidgetEndEditPoint = nullptr;

    struct DragState {
        bool isDragging = false;
        int targetParamId = -1;
        int oldMouseX = 0;
        int oldMouseY = 0;
        int lastValue = 0;
        int oldValue = 0;
        int newValue = 0;

        bool pointBefore = false;
        long dragLine = 0;
        long dragEndLine = 0;
        long dragStartCol = 0;
        size_t currentTextLength = 0;

        long initialCursorAnchorOffset = 0;

        DWORD lastClickTime = 0;
        POINT lastClickPt = { 0, 0 };
        std::string startTextValue = "";
        std::string oldValueStr = "";
        std::string lastValueStr = "";

        bool isProportionalStructDrag = false;
        std::vector<float> originalStructValues;
        bool isCursorHidden = false;  
        POINT lockMousePos = { 0, 0 }; 
    };

    static DragState g_dragState;

    inline int GetMouseDragTargetParamId() {
        if (g_dragState.isDragging || g_dragState.isProportionalStructDrag) {
            return g_dragState.targetParamId;
        }
        return -1;
    }

    struct PendingVsWrite {
        bool hasPending = false;
        long line = 0;
        long startCol = 0;
        std::string codeText;
    };

    static PendingVsWrite g_pendingWrite;

    inline void PushPendingWrite(long line, long startCol, const std::string& text) {
        g_pendingWrite.line = line;
        g_pendingWrite.startCol = startCol;
        g_pendingWrite.codeText = text;
        g_pendingWrite.hasPending = true;
    }

    inline void FlushPendingWritesToVS() {
        if (g_pendingWrite.hasPending && !g_pendingWrite.codeText.empty()) {
            long newCursorPhysicalCol = g_pendingWrite.startCol + static_cast<long>(g_pendingWrite.codeText.length());

            StartUndoTransaction(L"LivePT Realtime Callback Change");

            ReplaceTextInActiveVS(
                g_pendingWrite.line,
                g_pendingWrite.startCol,
                g_pendingWrite.startCol + static_cast<long>(g_dragState.currentTextLength),
                g_pendingWrite.codeText,
                newCursorPhysicalCol
            );

            EndUndoTransaction();

            g_dragState.currentTextLength = g_pendingWrite.codeText.length();
            g_pendingWrite.hasPending = false;
        }
    }

    inline std::string NormalizeTextEndings(std::string text, const std::wstring& fileFullText) {

        if (text.find('\n') == std::string::npos) {
            return text;
        }

        bool isFileCRLF = (fileFullText.find(L"\r\n") != std::wstring::npos);

        if (isFileCRLF) {
            size_t pos = 0;
            while ((pos = text.find("\r\n", pos)) != std::string::npos) {
                text.replace(pos, 2, "\n");
            }
            pos = 0;
            while ((pos = text.find('\n', pos)) != std::string::npos) {
                text.replace(pos, 1, "\r\n");
                pos += 2;
            }
        }
        else {
            text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        }

        return text;
    }

    static HWND g_hShieldWnd = NULL;
    static DWORD g_vsThreadId = 0;

    inline LRESULT CALLBACK ShieldWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == LPT_WM_BUTTONUP) {
            ReleaseCapture();
            DestroyWindow(hwnd);
            g_hShieldWnd = NULL;
            return 0;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    inline void CreateDragShield(const POINT& pt) {
        if (g_hShieldWnd) return;

        CComVariant vtMainWindow;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtMainWindow, pDTE, L"MainWindow", 0)) || !vtMainWindow.pdispVal) return;
        CComVariant vtHWnd;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtHWnd, vtMainWindow.pdispVal, L"HWnd", 0))) return;
        HWND hVSMainWnd = reinterpret_cast<HWND>(static_cast<LONG_PTR>(vtHWnd.lVal));
        if (!hVSMainWnd) return;

        HWND hEditorWnd = ::WindowFromPoint(pt);
        if (!hEditorWnd) hEditorWnd = hVSMainWnd;

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

        g_hShieldWnd = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className, "LPT_DragShield", WS_POPUP,
            pt.x, pt.y, 1, 1,
            NULL, NULL, hInst, NULL
        );

        if (g_hShieldWnd) {
            SetLayeredWindowAttributes(g_hShieldWnd, 0, 1, LWA_ALPHA);
            ShowWindow(g_hShieldWnd, SW_SHOW);

            ::SetCapture(g_hShieldWnd);
        }
    }

    inline bool isMouseDragging() { 
        return g_dragState.isDragging; 
    }

    inline std::wstring ConvertUtf8ToWString(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), wstr.data(), size_needed);
        return wstr;
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

            long physicalStartCol = 1;
            long physicalEndCol = 1;
            const long TAB_SIZE = GetVSTabSize();
            size_t currentVisualCol = 1;

            size_t i = 0;
            for (; i < lineText.length(); ++i) {
                if (currentVisualCol >= static_cast<size_t>(visualStartCol)) break;

                if (lineText[i] == L'\r') continue; 

                currentVisualCol += (lineText[i] == L'\t') ? (TAB_SIZE - ((currentVisualCol - 1) % TAB_SIZE)) : 1;
            }
            physicalStartCol = static_cast<long>(i) + 1;

            for (; i < lineText.length(); ++i) {
                if (currentVisualCol >= static_cast<size_t>(visualEndCol)) break;

                if (lineText[i] == L'\r') continue; 

                currentVisualCol += (lineText[i] == L'\t') ? (TAB_SIZE - ((currentVisualCol - 1) % TAB_SIZE)) : 1;
            }
            physicalEndCol = static_cast<long>(i) + 1;

            std::wstring wNewText = ConvertUtf8ToWString(NormalizeTextEndings(newText, fileText));
            std::wstring modifiedLineText = lineText;

            size_t localStartIdx = static_cast<size_t>(physicalStartCol - 1);
            size_t localEndIdx = static_cast<size_t>(physicalEndCol - 1);
            if (localStartIdx <= modifiedLineText.length() && localEndIdx <= modifiedLineText.length()) {
                modifiedLineText.replace(localStartIdx, localEndIdx - localStartIdx, wNewText);
            }

            long exactCharacterOffset = 1;
            size_t runningVisualCol = 1;
            for (size_t k = 0; k < modifiedLineText.length(); ++k) {
                if (runningVisualCol >= static_cast<size_t>(newCursorPhysicalCol)) {
                    exactCharacterOffset = static_cast<long>(k) + 1;
                    break;
                }
                if (modifiedLineText[k] == L'\r') continue; 

                runningVisualCol += (modifiedLineText[k] == L'\t') ? (TAB_SIZE - ((runningVisualCol - 1) % TAB_SIZE)) : 1;
            }
            if (runningVisualCol < static_cast<size_t>(newCursorPhysicalCol)) {
                exactCharacterOffset = static_cast<long>(modifiedLineText.length()) + 1;
            }

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

            CComBSTR bstrText(wNewText.c_str());
            CComVariant vText(bstrText); CComVariant vOption(1L);

            hr = AutoWrap(DISPATCH_METHOD, NULL, pEditStart.pdispVal, L"ReplaceText", 3, pEditEnd, vText, vOption);
            if (FAILED(hr)) return false;

            AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"MoveToLineAndOffset", 3, CComVariant(line), CComVariant(exactCharacterOffset), CComVariant(0L));
        }
        return true;
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
                    LPT_SHADER_COMPILE_FUNC;
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

    inline void InitMultiEnumSelection(int id) {

        g_dragState.isDragging = false;

        if (paramDesc[id].enumInfo.elements.empty()) {
            std::wstring wEnumName = L"";
            size_t lastCols = g_dragState.startTextValue.rfind("::");

            if (lastCols != std::string::npos) {
                std::string pureTypeName = g_dragState.startTextValue.substr(0, lastCols);
                int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, pureTypeName.c_str(), -1, NULL, 0);
                wEnumName.resize(sizeNeeded - 1);
                MultiByteToWideChar(CP_UTF8, 0, pureTypeName.c_str(), -1, wEnumName.data(), sizeNeeded);
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

        int currentVal = 0;
        if (paramDesc[id].value.type() == typeid(int)) {
            currentVal = std::any_cast<int>(paramDesc[id].value);
        }

        auto GetNewValueString = [&](const std::string& pureName) -> std::string {
            std::string newValueStr = pureName;
            size_t lastCols = g_dragState.startTextValue.rfind("::");
            if (lastCols != std::string::npos) {
                std::string prefix = g_dragState.startTextValue.substr(0, lastCols + 2);
                newValueStr = prefix + pureName;
            }
            return newValueStr;
            };

        if (totalElements == 2) {
            int currentIndex = 0;
            if (paramDesc[id].enumInfo.elements[0].value == currentVal) currentIndex = 0;
            else if (paramDesc[id].enumInfo.elements[1].value == currentVal) currentIndex = 1;

            int newIndex = 1 - currentIndex;
            int targetEnumValue = paramDesc[id].enumInfo.elements[newIndex].value;
            std::string pureName = paramDesc[id].enumInfo.elements[newIndex].name;
            std::string newValueStr = GetNewValueString(pureName);

            long newCursorRelPos = static_cast<long>(newValueStr.length()) - g_dragState.initialCursorAnchorOffset;
            if (newCursorRelPos < 0) newCursorRelPos = 0;
            long newCursorPhysicalCol = g_dragState.dragStartCol + newCursorRelPos;

            StartUndoTransaction(L"LiveWheel Change Enum");
            ReplaceTextInActiveVS(g_dragState.dragLine, g_dragState.dragStartCol, g_dragState.dragStartCol + static_cast<long>(g_dragState.currentTextLength), newValueStr, newCursorPhysicalCol);
            EndUndoTransaction();

            g_dragState.currentTextLength = newValueStr.length();
            g_dragState.oldValueStr = newValueStr;

            paramDesc[id].value = std::any(targetEnumValue);
            
            g_dragState.targetParamId = -1;

            return;
        }

        std::vector<std::string> enumMenu;
        for (int i = 0; i < totalElements; i++) {
            enumMenu.push_back(paramDesc[id].enumInfo.elements[i].name);
        }

        int newIndex = showEnum(enumMenu); 

        if (newIndex >= 0 && newIndex < totalElements) {
            int targetEnumValue = paramDesc[id].enumInfo.elements[newIndex].value;
            std::string pureName = enumMenu[newIndex];
            std::string newValueStr = GetNewValueString(pureName);

            long newCursorRelPos = static_cast<long>(newValueStr.length()) - g_dragState.initialCursorAnchorOffset;
            if (newCursorRelPos < 0) newCursorRelPos = 0;
            long newCursorPhysicalCol = g_dragState.dragStartCol + newCursorRelPos;

            StartUndoTransaction(L"LiveWheel Change Enum");
            ReplaceTextInActiveVS(g_dragState.dragLine, g_dragState.dragStartCol, g_dragState.dragStartCol + static_cast<long>(g_dragState.currentTextLength), newValueStr, newCursorPhysicalCol);
            EndUndoTransaction();

            g_dragState.currentTextLength = newValueStr.length();
            g_dragState.oldValueStr = newValueStr;

            paramDesc[id].value = std::any(targetEnumValue);

        }

        g_dragState.targetParamId = -1;

    }

    inline void InitNumericDragState(size_t cursorIdxInRaw, const std::string& cleanText) {

        g_dragState.isDragging = true;
        StartUndoTransaction(L"LiveWheel Live Edit");

        g_dragState.oldValueStr = cleanText;
        g_dragState.lastValueStr = cleanText;

        long relativeOffset = static_cast<long>(cursorIdxInRaw);

        size_t dotPos = cleanText.find('.');
        g_dragState.pointBefore = (dotPos != std::wstring::npos && relativeOffset > static_cast<long>(dotPos));
        g_dragState.initialCursorAnchorOffset = (dotPos != std::wstring::npos) ? std::abs(static_cast<long>(dotPos) - relativeOffset) : static_cast<long>(cleanText.length()) - relativeOffset;

        if (dotPos == std::wstring::npos) {
            g_dragState.oldValue = atoi(cleanText.c_str());
        }
        else {
            g_dragState.oldValue = atoi(g_dragState.pointBefore ? cleanText.substr(dotPos + 1).c_str() : cleanText.substr(0, dotPos).c_str());
        }

        g_dragState.lastValue = g_dragState.oldValue;
        g_dragState.newValue = g_dragState.oldValue;
    }


    inline void DragProportionalStructValue(const POINT& pt, bool ctrl, bool shift) {

        auto& registry = LivePT::GetCustomDragRegistry();

        auto it = registry.find(g_dragState.oldValueStr);
        if (it == registry.end()) return;

        CustomTypeDragCallback typeCallback = it->second;

        DragMathInput input;
        input.mouseFrameDeltaX = pt.x - g_dragState.lockMousePos.x;
        input.mouseFrameDeltaY = -(pt.y - g_dragState.lockMousePos.y);
        input.ctrl = ctrl;
        input.shift = shift;

        std::string updatedArgsText = typeCallback(g_dragState.startTextValue, input);

        if (updatedArgsText != g_dragState.startTextValue) {

            if (input.mouseFrameDeltaX != 0 || input.mouseFrameDeltaY != 0) {
                SetCursorPos(g_dragState.lockMousePos.x, g_dragState.lockMousePos.y);
            }

            if (pWidgetStartEditPoint && pWidgetEndEditPoint) {
                CComVariant pSafeStartPoint;
                HRESULT hr = AutoWrap(DISPATCH_METHOD, &pSafeStartPoint, pWidgetStartEditPoint, L"CreateEditPoint", 0);

                if (SUCCEEDED(hr) && pSafeStartPoint.vt == VT_DISPATCH && pSafeStartPoint.pdispVal) {

                    AutoWrap(DISPATCH_METHOD, NULL, pSafeStartPoint.pdispVal, L"CharRight", 1, CComVariant(1L));
                    updatedArgsText = LivePT::NormalizeTextEndings(updatedArgsText, g_currentFileFullText);
                    std::wstring wText(updatedArgsText.begin(), updatedArgsText.end());
                    CComBSTR bstrText(wText.c_str());

                    AutoWrap(DISPATCH_METHOD, NULL, pSafeStartPoint.pdispVal, L"ReplaceText", 3,
                        CComVariant(pWidgetEndEditPoint),
                        CComVariant(bstrText),
                        CComVariant(1L));
                }
            }

            g_dragState.currentTextLength = updatedArgsText.length();
            g_dragState.startTextValue = updatedArgsText;
        }
    }


    inline long AdjustCursorIndexForNumericContext(const std::wstring& lineText, long originalVisualColumn) {
        if (lineText.empty()) return 0;

        long tabSize = GetVSTabSize();
        size_t currentVisualCol = 1;
        size_t physicalIdx = lineText.length();

        for (size_t i = 0; i < lineText.length(); ++i) {
            if (currentVisualCol >= static_cast<size_t>(originalVisualColumn)) {
                physicalIdx = i;
                break;
            }
            currentVisualCol += (lineText[i] == L'\t') ? (tabSize - ((currentVisualCol - 1) % tabSize)) : 1;
        }

        if (physicalIdx >= lineText.length()) {
            physicalIdx = lineText.length() - 1;
        }

        long cursorIdx = static_cast<long>(physicalIdx);
        if (cursorIdx < 0) return 0;

        // Проверяем текущий символ под курсором
        if (cursorIdx < static_cast<long>(lineText.length())) {
            wchar_t ch = lineText[cursorIdx];
            if (iswdigit(ch) || ch == L'-' || ch == L'f' || ch == L'F') return cursorIdx;

            // Кейс без ведущего нуля: кликнули на '.', а справа цифра (например, .5)
            if (ch == L'.') {
                if (cursorIdx + 1 < static_cast<long>(lineText.length()) && iswdigit(lineText[cursorIdx + 1])) {
                    return cursorIdx;
                }
            }
        }

        // Если под курсором не число, проверяем символ слева
        if (cursorIdx > 0 && (cursorIdx - 1) < static_cast<long>(lineText.length())) {
            wchar_t leftCh = lineText[cursorIdx - 1];

            if (iswdigit(leftCh) || leftCh == L'.' || leftCh == L'-' || leftCh == L'f' || leftCh == L'F') {
                // Исключаем склеивание с именами переменных/функций (например, myFloat2)
                if (iswdigit(leftCh) && (cursorIdx - 2) >= 0) {
                    wchar_t preLeftCh = lineText[cursorIdx - 2];
                    if (iswalpha(preLeftCh)) return cursorIdx;
                }
                return cursorIdx - 1;
            }
        }

        return cursorIdx;
    }


    inline bool IsNumericTokenChar(wchar_t ch) {
        return iswdigit(ch) || ch == L'.' || ch == L'-' || ch == L'f' || ch == L'F';
    }

    inline void ScanNumericTokenBoundaries(const std::wstring& lineText, long startIdx, long& outStartCol, long& outEndCol) {
        outStartCol = startIdx;
        outEndCol = startIdx;

        // 1. Расширяем границу влево
        while (outStartCol > 0) {
            wchar_t prevCh = lineText[outStartCol - 1];

            if (iswdigit(prevCh) || prevCh == L'.' || prevCh == L'f' || prevCh == L'F') {
                outStartCol--;
            }
            // Минус берем только если он унарный
            else if (prevCh == L'-') {
                if (outStartCol - 1 == 0) {
                    outStartCol--;
                }
                else {
                    wchar_t prePrevCh = lineText[outStartCol - 2];
                    if (prePrevCh == L'(' || prePrevCh == L'[' || prePrevCh == L'{' ||
                        prePrevCh == L',' || prePrevCh == L'=' || prePrevCh == L'+' ||
                        prePrevCh == L'-' || prePrevCh == L'*' || prePrevCh == L'/' ||
                        iswspace(prePrevCh)) {
                        outStartCol--;
                    }
                    else {
                        break; // Бинарный минус (вычитание), стоп
                    }
                }
            }
            else {
                break;
            }
        }

        // 2. Расширяем границу вправо
        while (outEndCol < static_cast<long>(lineText.length())) {
            wchar_t nextCh = lineText[outEndCol];

            if (iswdigit(nextCh) || nextCh == L'.' || nextCh == L'f' || nextCh == L'F') {
                outEndCol++;
            }
            else {
                break; // Знаки операций, скобки и прочее справа полностью игнорируем
            }
        }
    }

        
    inline bool TryInitSingleNumericDrag(const std::wstring& lineText, long line, long originalColumn, long targetCharIdx, const POINT& pt) {
        long startCol = 0;
        long endCol = 0;

        ScanNumericTokenBoundaries(lineText, targetCharIdx, startCol, endCol);

        if (startCol < endCol) {

            if (startCol > 0) {
                wchar_t charBeforeToken = lineText[startCol - 1];
                if (iswalpha(charBeforeToken)) {

                    return false;
                }
            }

            std::string cleanText;
            cleanText.reserve(endCol - startCol);
            for (long k = startCol; k < endCol; ++k) {
                cleanText.push_back(static_cast<char>(lineText[k]));
            }

            g_dragState.dragLine = line;
            g_dragState.dragStartCol = GetVisualColumn(lineText, startCol);
            g_dragState.currentTextLength = cleanText.length();
            g_dragState.oldMouseX = pt.x;
            g_dragState.oldMouseY = pt.y;
            g_dragState.lockMousePos = pt;

            long physicalCursorIdx = targetCharIdx; 
            if (physicalCursorIdx < startCol) physicalCursorIdx = startCol;
            if (physicalCursorIdx > endCol)   physicalCursorIdx = endCol;

            size_t relativeCursorIdx = static_cast<size_t>(physicalCursorIdx - startCol);

            g_dragState.oldValueStr = cleanText;
            g_dragState.lastValueStr = cleanText;

            size_t dotPos = cleanText.find('.');
            g_dragState.pointBefore = (dotPos != std::wstring::npos && relativeCursorIdx > dotPos);

            g_dragState.initialCursorAnchorOffset = (dotPos != std::wstring::npos)
                ? std::abs(static_cast<long>(dotPos) - static_cast<long>(relativeCursorIdx))
                : static_cast<long>(cleanText.length()) - static_cast<long>(relativeCursorIdx);

            if (dotPos == std::wstring::npos) {
                g_dragState.oldValue = atoi(cleanText.c_str());
            }
            else {
                g_dragState.oldValue = atoi(g_dragState.pointBefore ? cleanText.substr(dotPos + 1).c_str() : cleanText.substr(0, dotPos).c_str());
            }

            g_dragState.lastValue = g_dragState.oldValue;
            g_dragState.newValue = g_dragState.oldValue;

            StartUndoTransaction(L"LivePT Realtime Numeric Edit");

            std::string normalizedPath = LivePT::NormalizePath(g_currentActiveFile.c_str());
            g_dragState.targetParamId = FindParamIdByStrictGeometry(normalizedPath, line, originalColumn);

            if (!g_dragState.isCursorHidden) {
                ShowCursor(FALSE);
                g_dragState.isCursorHidden = true;
            }

            return true;
        }

        return false;
    }

    inline bool TryToggleBooleanDirect(const std::wstring& lineText, long line, long targetCharIdx) {

        long startCol = targetCharIdx;
        long endCol = targetCharIdx;

        while (startCol > 0 && iswalpha(lineText[startCol - 1])) startCol--;
        while (endCol < static_cast<long>(lineText.length()) && iswalpha(lineText[endCol])) endCol++;

        if (startCol >= endCol) return false;

        std::string wordA;
        wordA.reserve(endCol - startCol);
        for (long k = startCol; k < endCol; ++k) {
            wordA.push_back(static_cast<char>(lineText[k]));
        }

        std::string newValueStr = "";

        if (wordA == "true" || wordA == "TRUE") {
            newValueStr = "false";
        }
        else if (wordA == "false" || wordA == "FALSE") {
            newValueStr = "true";
        }

        if (!newValueStr.empty()) {
            long visualStart = GetVisualColumn(lineText, startCol);
            long visualEnd = visualStart + static_cast<long>(wordA.length());
            long newCursorCol = visualStart + static_cast<long>(newValueStr.length());

            StartUndoTransaction(L"LiveWheel Toggle Bool");
            ReplaceTextInActiveVS(line, visualStart, visualEnd, newValueStr, newCursorCol);
            EndUndoTransaction();

            return true;
        }

        return false;
    }
    /*
    inline bool TryInitEnumSelectionDirect(const std::wstring& lineText, long line, long column, long targetCharIdx) {
        if (lineText.empty()) return false;

        long tokenStart = targetCharIdx;
        long tokenEnd = targetCharIdx;
        while (tokenStart > 0 && (iswalnum(lineText[tokenStart - 1]) || lineText[tokenStart - 1] == L'_')) tokenStart--;
        while (tokenEnd < static_cast<long>(lineText.length()) && (iswalnum(lineText[tokenEnd]) || lineText[tokenEnd] == L'_')) tokenEnd++;

        if (tokenStart >= tokenEnd) return false;

        std::wstring enumTypeNameW = L"";
        if (tokenStart >= 2 && lineText[tokenStart - 1] == L':' && lineText[tokenStart - 2] == L':') {
            long typeStart = tokenStart - 2;
            while (typeStart > 0 && (iswalnum(lineText[typeStart - 1]) || lineText[typeStart - 1] == L'_')) {
                typeStart--;
            }
            if (typeStart < tokenStart - 2) {
                enumTypeNameW = lineText.substr(typeStart, (tokenStart - 2) - typeStart);
            }
        }

        if (enumTypeNameW.empty()) return false;

        std::vector<std::string> parsedNames;
        std::vector<int> parsedValues;

        if (!LoadEnumMetadataDirect(enumTypeNameW.c_str(), parsedNames, parsedValues)) {
            return false; 
        }

        int newIndex = showEnum(parsedNames);

        if (newIndex >= 0 && newIndex < static_cast<int>(parsedNames.size())) {
            std::string pureName = parsedNames[newIndex];
            std::string typePrefixA;
            typePrefixA.reserve(enumTypeNameW.length());
            for (wchar_t wc : enumTypeNameW) {
                typePrefixA.push_back(static_cast<char>(wc));
            }

            std::string newValueStr = typePrefixA + "::" + pureName;

            long typeStartCol = tokenStart - 2;
            while (typeStartCol > 0 && (iswalnum(lineText[typeStartCol - 1]) || lineText[typeStartCol - 1] == L'_')) typeStartCol--;

            long visualStart = GetVisualColumn(lineText, typeStartCol);
            long visualEnd = GetVisualColumn(lineText, tokenEnd);
            long newCursorCol = visualStart + static_cast<long>(newValueStr.length());

            StartUndoTransaction(L"LiveWheel Change Enum");
            ReplaceTextInActiveVS(line, visualStart, visualEnd, newValueStr, newCursorCol);
            EndUndoTransaction();

            SaveActiveDocument();
            return true;
        }

        return false;
    }
    */
    inline bool TryInitEnumSelectionDirect(const std::wstring& lineText, long line, long column, long targetCharIdx) {
        if (lineText.empty()) return false;

        long tokenStart = targetCharIdx;
        long tokenEnd = targetCharIdx;

        while (tokenStart > 0 && (iswalnum(lineText[tokenStart - 1]) || lineText[tokenStart - 1] == L'_')) tokenStart--;
        while (tokenEnd < static_cast<long>(lineText.length()) && (iswalnum(lineText[tokenEnd]) || lineText[tokenEnd] == L'_')) tokenEnd++;

        if (tokenStart >= tokenEnd) return false;

        std::wstring enumTypeNameW = L"";
        long typeStartCol = tokenStart;

        // Идем влево и собираем всю цепочку: Идентификаторы и двоеточия (::)
        if (tokenStart >= 2 && lineText[tokenStart - 1] == L':' && lineText[tokenStart - 2] == L':') {
            long currentPos = tokenStart - 2;

            while (currentPos > 0) {
                // Пропускаем буквы, цифры и нижнее подчеркивание (часть имени неймспейса/типа)
                if (iswalnum(lineText[currentPos - 1]) || lineText[currentPos - 1] == L'_') {
                    currentPos--;
                }
                // Или если перед нами еще одно двоеточие '::', и перед ним тоже идет валидный символ
                else if (currentPos >= 2 && lineText[currentPos - 1] == L':' && lineText[currentPos - 2] == L':' &&
                    (iswalnum(lineText[currentPos - 3]) || lineText[currentPos - 3] == L'_')) {
                    currentPos -= 2;
                }
                else {
                    break;
                }
            }

            if (currentPos < tokenStart - 2) {
                enumTypeNameW = lineText.substr(currentPos, (tokenStart - 2) - currentPos);
                typeStartCol = currentPos; // Запоминаем точную позицию начала всей цепочки для замены текста
            }
        }

        if (enumTypeNameW.empty()) return false;

        std::vector<std::string> parsedNames;
        std::vector<int> parsedValues;

        if (!LoadEnumMetadataDirect(enumTypeNameW.c_str(), parsedNames, parsedValues)) {
            return false;
        }

        int newIndex = showEnum(parsedNames);
        if (newIndex >= 0 && newIndex < static_cast<int>(parsedNames.size())) {
            std::string pureName = parsedNames[newIndex];
            std::string typePrefixA;
            typePrefixA.reserve(enumTypeNameW.length());
            for (wchar_t wc : enumTypeNameW) {
                typePrefixA.push_back(static_cast<char>(wc));
            }

            std::string newValueStr = typePrefixA + "::" + pureName;

            // Используем вычисленный ранее typeStartCol вместо старого цикла
            long visualStart = GetVisualColumn(lineText, typeStartCol);
            long visualEnd = GetVisualColumn(lineText, tokenEnd);
            long newCursorCol = visualStart + static_cast<long>(newValueStr.length());

            StartUndoTransaction(L"LiveWheel Change Enum");
            ReplaceTextInActiveVS(line, visualStart, visualEnd, newValueStr, newCursorCol);
            EndUndoTransaction();

            return true;
        }
        return false;
    }


    struct WidgetPendingBuffer {
        bool hasChanges = false;
        long line = 0;
        long startCol = 0;
        std::string codeText;
    };

    static WidgetPendingBuffer g_widgetBuffer;
        
    inline void PushWidgetTextUpdate(long line, long startCol, const std::string& text) {
        g_widgetBuffer.line = line;
        g_widgetBuffer.startCol = startCol;
        g_widgetBuffer.codeText = text;
        g_widgetBuffer.hasChanges = true;
    }
    
    inline void WidgetBufferUpdate()
    {
        bool isTriggerButtonDown = (GetAsyncKeyState(LivePT_TriggerButton) & 0x8000) != 0;
        static bool s_WidgetTransactionActive = false;

        if (g_widgetBuffer.hasChanges && !s_WidgetTransactionActive) {
            StartUndoTransaction(L"LivePT Widget Realtime Update");
            s_WidgetTransactionActive = true;
        }
        
        if (g_widgetBuffer.hasChanges && !g_widgetBuffer.codeText.empty() && pWidgetEndEditPoint && pWidgetStartEditPoint) {

            VARIANT vtActiveDocLocal; VariantInit(&vtActiveDocLocal);
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDocLocal, pDTE, L"ActiveDocument", 0)) && vtActiveDocLocal.pdispVal) {

                VARIANT vtCloseLine, vtCloseOffset;
                VariantInit(&vtCloseLine); VariantInit(&vtCloseOffset);

                AutoWrap(DISPATCH_PROPERTYGET, &vtCloseLine, pWidgetEndEditPoint, L"Line", 0);
                AutoWrap(DISPATCH_PROPERTYGET, &vtCloseOffset, pWidgetEndEditPoint, L"DisplayColumn", 0);

                long actualCloseLine = (vtCloseLine.vt == VT_I4) ? vtCloseLine.lVal : g_widgetBuffer.line;
                long actualCloseOffset = (vtCloseOffset.vt == VT_I4) ? vtCloseOffset.lVal : g_dragState.dragStartCol;

                if (g_widgetBuffer.line == actualCloseLine) {

                    long newCursorPhysicalCol = g_dragState.dragStartCol + static_cast<long>(g_widgetBuffer.codeText.length());
                    ReplaceTextInActiveVS(g_widgetBuffer.line, g_dragState.dragStartCol, actualCloseOffset, g_widgetBuffer.codeText, newCursorPhysicalCol);
                }
                else {

                    CComVariant pSafeStartPoint;
                    HRESULT hr = AutoWrap(DISPATCH_METHOD, &pSafeStartPoint, pWidgetStartEditPoint, L"CreateEditPoint", 0);

                    if (SUCCEEDED(hr) && pSafeStartPoint.vt == VT_DISPATCH && pSafeStartPoint.pdispVal) {
                        VARIANT vtStartLineNum, vtStartCharOffset;
                        VariantInit(&vtStartLineNum); VariantInit(&vtStartCharOffset);

                        AutoWrap(DISPATCH_PROPERTYGET, &vtStartLineNum, pWidgetStartEditPoint, L"Line", 0);
                        AutoWrap(DISPATCH_PROPERTYGET, &vtStartCharOffset, pWidgetStartEditPoint, L"LineCharOffset", 0);

                        long startL = vtStartLineNum.lVal;
                        long startO = vtStartCharOffset.lVal + 1; 

                        AutoWrap(DISPATCH_METHOD, NULL, pSafeStartPoint.pdispVal, L"MoveToLineAndOffset", 2, CComVariant(startL), CComVariant(startO));

                        g_widgetBuffer.codeText = LivePT::NormalizeTextEndings(g_widgetBuffer.codeText, g_currentFileFullText);

                        std::wstring wText(g_widgetBuffer.codeText.begin(), g_widgetBuffer.codeText.end());
                        CComBSTR bstrText(wText.c_str());

                        AutoWrap(DISPATCH_METHOD, NULL, pSafeStartPoint.pdispVal, L"ReplaceText", 3,
                            CComVariant(pWidgetEndEditPoint),
                            CComVariant(bstrText),
                            CComVariant(1L));

                        VariantClear(&vtStartCharOffset); VariantClear(&vtStartLineNum);
                    }

                    VARIANT vtSelection; VariantInit(&vtSelection);
                    if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, vtActiveDocLocal.pdispVal, L"Selection", 0)) && vtSelection.pdispVal) {
                        VARIANT vtNewCursorLine, vtNewCursorOffset;
                        VariantInit(&vtNewCursorLine); VariantInit(&vtNewCursorOffset);

                        AutoWrap(DISPATCH_PROPERTYGET, &vtNewCursorLine, pWidgetEndEditPoint, L"Line", 0);
                        AutoWrap(DISPATCH_PROPERTYGET, &vtNewCursorOffset, pWidgetEndEditPoint, L"LineCharOffset", 0);

                        long finalLine = (vtNewCursorLine.vt == VT_I4) ? vtNewCursorLine.lVal : g_widgetBuffer.line;
                        long finalOffset = (vtNewCursorOffset.vt == VT_I4) ? vtNewCursorOffset.lVal : 1;

                        AutoWrap(DISPATCH_METHOD, NULL, vtSelection.pdispVal, L"MoveToLineAndOffset", 3,
                            CComVariant(finalLine),
                            CComVariant(finalOffset),
                            CComVariant(0L));

                        VariantClear(&vtNewCursorOffset); VariantClear(&vtNewCursorLine);
                        VariantClear(&vtSelection);
                    }
                }

                VariantClear(&vtCloseOffset); VariantClear(&vtCloseLine);
                VariantClear(&vtActiveDocLocal);
            }
            g_widgetBuffer.hasChanges = false;
        }

        if (!isTriggerButtonDown && s_WidgetTransactionActive) {
            EndUndoTransaction();
            s_WidgetTransactionActive = false;
        }
    }

    inline bool ExtractStructBoundaries(const std::wstring& fileText, const std::wstring& lineText, long line, long targetCharIdx, std::string& outInnerArgsA, long& outVisualStartCol, std::string& outTypeNameA) {
        if (fileText.empty() || lineText.empty()) return false;

        long tokenStart = targetCharIdx; long tokenEnd = targetCharIdx;
        while (tokenStart > 0 && iswalnum(lineText[tokenStart - 1])) tokenStart--;
        while (tokenEnd < static_cast<long>(lineText.length()) && iswalnum(lineText[tokenEnd])) tokenEnd++;
        if (tokenStart >= tokenEnd) return false;

        std::wstring typeNameW = lineText.substr(tokenStart, tokenEnd - tokenStart);

        outTypeNameA.clear(); // На всякий случай очищаем, если строка передана по ссылке
        outTypeNameA.reserve(typeNameW.length());
        for (wchar_t wc : typeNameW) {
            outTypeNameA.push_back(static_cast<char>(wc));
        }
        size_t lineStartOffset = 0; long currentLineIdx = 1;
        while (currentLineIdx < line && lineStartOffset < fileText.length()) {
            size_t nextNL = fileText.find(L'\n', lineStartOffset);
            if (nextNL != std::wstring::npos) { lineStartOffset = nextNL + 1; currentLineIdx++; }
            else break;
        }
        size_t absoluteTokenEndOffset = lineStartOffset + tokenEnd;

        size_t globalOpenIdx = std::wstring::npos;
        wchar_t openChar = L'\0'; wchar_t closeChar = L'\0';
        for (size_t k = absoluteTokenEndOffset; k < fileText.length(); ++k) {
            if (fileText[k] == L'{') { openChar = L'{'; closeChar = L'}'; globalOpenIdx = k; break; }
            if (fileText[k] == L'(') { openChar = L'('; closeChar = L')'; globalOpenIdx = k; break; }
            if (!iswspace(fileText[k]) && fileText[k] != L'.') break;
        }
        if (globalOpenIdx == std::wstring::npos) return false;

        size_t globalCloseIdx = std::wstring::npos;
        int bracketCount = 1;
        for (size_t k = globalOpenIdx + 1; k < fileText.length(); ++k) {
            if (fileText[k] == openChar) bracketCount++;
            if (fileText[k] == closeChar) {
                bracketCount--;
                if (bracketCount == 0) { globalCloseIdx = k; break; }
            }
        }
        if (globalCloseIdx == std::wstring::npos) return false;

        long startLine = line; size_t lastNL_start = lineStartOffset;
        size_t scanNL_start = fileText.find(L'\n', lineStartOffset);
        long startCrCount = 0;

        while (scanNL_start != std::wstring::npos && scanNL_start < globalOpenIdx) {
            startLine++; lastNL_start = scanNL_start + 1;
            scanNL_start = fileText.find(L'\n', lastNL_start);
        }
        for (size_t i = lastNL_start; i < globalOpenIdx; ++i) {
            if (fileText[i] == L'\r') startCrCount++;
        }
        long startOffset = static_cast<long>(globalOpenIdx - lastNL_start) - startCrCount + 1;

        long closeLine = line; size_t lastNL_end = lineStartOffset;
        size_t scanNL_end = fileText.find(L'\n', lineStartOffset);
        long closeCrCount = 0;

        while (scanNL_end != std::wstring::npos && scanNL_end < globalCloseIdx) {
            closeLine++; lastNL_end = scanNL_end + 1;
            scanNL_end = fileText.find(L'\n', lastNL_end);
        }
        for (size_t i = lastNL_end; i < globalCloseIdx; ++i) {
            if (fileText[i] == L'\r') closeCrCount++;
        }
        long closeOffset = static_cast<long>(globalCloseIdx - lastNL_end) - closeCrCount + 1;

        if (pWidgetStartEditPoint) { pWidgetStartEditPoint->Release(); pWidgetStartEditPoint = nullptr; }
        if (pWidgetEndEditPoint) { pWidgetEndEditPoint->Release(); pWidgetEndEditPoint = nullptr; }

        VARIANT vtActiveDoc; VariantInit(&vtActiveDoc);
        if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0)) && vtActiveDoc.pdispVal) {
            VARIANT vtSelection; VariantInit(&vtSelection);
            if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtSelection, vtActiveDoc.pdispVal, L"Selection", 0)) && vtSelection.pdispVal) {
                VARIANT vtActivePoint; VariantInit(&vtActivePoint);
                if (SUCCEEDED(AutoWrap(DISPATCH_PROPERTYGET, &vtActivePoint, vtSelection.pdispVal, L"ActivePoint", 0)) && vtActivePoint.pdispVal) {

                    HRESULT hr = AutoWrap(DISPATCH_METHOD, &vtActivePoint, vtActivePoint.pdispVal, L"CreateEditPoint", 0);
                    if (SUCCEEDED(hr) && vtActivePoint.vt == VT_DISPATCH && vtActivePoint.pdispVal) {
                        pWidgetStartEditPoint = vtActivePoint.pdispVal; pWidgetStartEditPoint->AddRef();
                        AutoWrap(DISPATCH_METHOD, NULL, pWidgetStartEditPoint, L"MoveToLineAndOffset", 2, CComVariant(startLine), CComVariant(startOffset));
                    }
                    hr = AutoWrap(DISPATCH_METHOD, &vtActivePoint, vtActivePoint.pdispVal, L"CreateEditPoint", 0);
                    if (SUCCEEDED(hr) && vtActivePoint.vt == VT_DISPATCH && vtActivePoint.pdispVal) {
                        pWidgetEndEditPoint = vtActivePoint.pdispVal; pWidgetEndEditPoint->AddRef();
                        AutoWrap(DISPATCH_METHOD, NULL, pWidgetEndEditPoint, L"MoveToLineAndOffset", 2, CComVariant(closeLine), CComVariant(closeOffset));
                    }
                }
                VariantClear(&vtActivePoint); VariantClear(&vtSelection);
            }
            VariantClear(&vtActiveDoc);
        }

        if (!pWidgetStartEditPoint || !pWidgetEndEditPoint) return false;

        size_t localOpenIdxInLine = globalOpenIdx - lineStartOffset;
        outVisualStartCol = GetVisualColumn(lineText, localOpenIdxInLine + 1);

        std::wstring innerArgsW = fileText.substr(globalOpenIdx + 1, globalCloseIdx - globalOpenIdx - 1);
        outInnerArgsA = ConvertWStringToUtf8(innerArgsW);
        return true;
    }


    inline bool TryInitStructDragDirect(const std::wstring& fileText, const std::wstring& lineText, long line, long column, long targetCharIdx, const POINT& pt) {
        std::string innerArgsA, typeNameA;
        long visualStartCol = 0;

        if (!ExtractStructBoundaries(fileText, lineText, line, targetCharIdx, innerArgsA, visualStartCol, typeNameA)) {
            return false;
        }

        
        bool hasAlphaVariables = false;
        for (size_t i = 0; i < innerArgsA.length(); ++i) {
            char c = innerArgsA[i];
            if (std::isalpha(static_cast<unsigned char>(c))) {
                if (c == 'f' || c == 'F') continue;
                bool isDesignatedInitializer = false;
                size_t left = i;
                while (left > 0) {
                    left--;
                    if (innerArgsA[left] == '.') { isDesignatedInitializer = true; break; }
                    if (!std::isspace(static_cast<unsigned char>(innerArgsA[left]))) break;
                }
                size_t right = i;
                while (right < innerArgsA.length() - 1) {
                    right++;
                    if (innerArgsA[right] == '=') { isDesignatedInitializer = true; break; }
                    if (!std::isspace(static_cast<unsigned char>(innerArgsA[right]))) break;
                }
                if (!isDesignatedInitializer) { hasAlphaVariables = true; break; }
            }
        }
        if (hasAlphaVariables) return false;
        
        auto& dragRegistry = GetCustomDragRegistry();
        if (dragRegistry.find(typeNameA) == dragRegistry.end()) return false;

        g_dragState.dragLine = line;
        g_dragState.dragStartCol = visualStartCol;
        g_dragState.currentTextLength = innerArgsA.length();
        g_dragState.oldValueStr = typeNameA;  
        g_dragState.startTextValue = innerArgsA;
        g_dragState.oldMouseX = pt.x;
        g_dragState.oldMouseY = pt.y;
        g_dragState.lockMousePos = pt;

        g_dragState.targetParamId = -1; 
        g_dragState.isDragging = true;  
        g_dragState.isProportionalStructDrag = true;

        StartUndoTransaction(L"LivePT Proportional Struct Drag");

        if (!g_dragState.isCursorHidden) {
            ShowCursor(FALSE);
            g_dragState.isCursorHidden = true;
        }

        return true;
    }

    inline bool TryInitStructWidgetDirect(const std::wstring& fileText, const std::wstring& lineText, long line, long column, long targetCharIdx) {
        std::string innerArgsA, typeNameA;
        long visualStartCol = 0;

        if (!ExtractStructBoundaries(fileText, lineText, line, targetCharIdx, innerArgsA, visualStartCol, typeNameA)) {
            return false;
        }

        
        bool hasAlphaVariables = false;

        for (size_t i = 0; i < innerArgsA.length(); ++i) {
            char c = innerArgsA[i];
            if (std::isalpha(static_cast<unsigned char>(c))) {

                if (c == 'f' || c == 'F') continue;

                bool isDesignatedInitializer = false;

                size_t left = i;
                while (left > 0) {
                    left--;
                    if (innerArgsA[left] == '.') { isDesignatedInitializer = true; break; }
                    if (!std::isspace(static_cast<unsigned char>(innerArgsA[left]))) break;
                }

                size_t right = i;
                while (right < innerArgsA.length() - 1) {
                    right++;
                    if (innerArgsA[right] == '=') { isDesignatedInitializer = true; break; }
                    if (!std::isspace(static_cast<unsigned char>(innerArgsA[right]))) break;
                }

                if (!isDesignatedInitializer) {
                    hasAlphaVariables = true;
                    break;
                }
            }
        }

        if (hasAlphaVariables) {
            return false; 
        }
        
        auto& registry = GetTypeCallbackRegistry();
        if (registry.find(typeNameA) == registry.end()) return false;

        g_dragState.dragLine = line;
        g_dragState.dragStartCol = visualStartCol;
        g_dragState.currentTextLength = innerArgsA.length();
        g_dragState.oldValueStr = typeNameA;

        std::function<void(std::string)> vsUpdater = [line](std::string newCodeText) {
            PushWidgetTextUpdate(line, g_dragState.dragStartCol, newCodeText);
            };

        registry[typeNameA](std::any(innerArgsA), vsUpdater);
        return true;
    }


    inline bool HandleMouseDown(const POINT& pt) {
        if (!initVsEditor()) {
            return false;
        }

        VARIANT vtActiveDoc;
        std::string currentFile;
        long line = 0, column = 0;
        std::wstring fileText;

        if (!GetActiveVSContext(vtActiveDoc, currentFile, line, column, fileText)) {
            return false;
        }

        std::wstring currentLineText = DownloadCurrentLineText(vtActiveDoc.pdispVal);
        std::string normalizedPath = LivePT::NormalizePath(currentFile.c_str());

        g_dragState.targetParamId = -1;
        g_dragState.isProportionalStructDrag = false;

        DWORD currentTime = GetTickCount();
        DWORD doubleClickTime = GetDoubleClickTime();
        bool isDoubleClick = (g_dragState.lastClickTime != 0) &&
            (currentTime - g_dragState.lastClickTime <= doubleClickTime) &&
            (std::abs(pt.x - g_dragState.lastClickPt.x) < 4) &&
            (std::abs(pt.y - g_dragState.lastClickPt.y) < 4);

        if (isDoubleClick) g_dragState.lastClickTime = 0;
        else g_dragState.lastClickTime = currentTime;
        g_dragState.lastClickPt = pt;

        long targetCharIdx = AdjustCursorIndexForNumericContext(currentLineText, column);

        if (targetCharIdx < 0 || targetCharIdx >= static_cast<long>(currentLineText.length())) {
            VariantClear(&vtActiveDoc);
            return false;
        }

        if (isDoubleClick) {
            if (TryToggleBooleanDirect(currentLineText, line, targetCharIdx)) { VariantClear(&vtActiveDoc); return false; }
            if (TryInitEnumSelectionDirect(currentLineText, line, column, targetCharIdx)) { VariantClear(&vtActiveDoc); return false; }
            if (TryInitStructWidgetDirect(fileText, currentLineText, line, column, targetCharIdx)) { VariantClear(&vtActiveDoc); return false; }
            VariantClear(&vtActiveDoc);
            return false;
        }

        wchar_t targetedChar = currentLineText[targetCharIdx];
        bool isNumeric = IsNumericTokenChar(targetedChar);

        if (isNumeric) {
            if (TryInitSingleNumericDrag(currentLineText, line, column, targetCharIdx, pt)) {
                VariantClear(&vtActiveDoc);
                return true;
            }
        }
        else if (iswalpha(targetedChar) || targetedChar == L'{' || targetedChar == L'(') {
            if (TryInitStructDragDirect(fileText,currentLineText, line, column, targetCharIdx, pt)) {
                VariantClear(&vtActiveDoc);
                return true;
            }
        }

        VariantClear(&vtActiveDoc);
        return false;
    }


    inline void DragNumericValue(const POINT& pt, bool ctrl, bool shift) {
        static HCURSOR hSizeNS = LoadCursor(NULL, IDC_SIZENS);

        ::LivePT::g_OverlayCursor = hSizeNS;
        ::LivePT::g_OverlayText = "shift=X10,ctrl=X100";
        ::LivePT::g_OverlayShow = true;

        int scale = 1;
        if (ctrl)  scale *= 100;
        if (shift) scale *= 10;

        int delta = -(pt.y - g_dragState.oldMouseY) * scale / 2;
        long long targetValue = static_cast<long long>(g_dragState.oldValue) + delta;
        g_dragState.newValue = static_cast<int>(targetValue);

        if (g_dragState.newValue != g_dragState.lastValue) {
            size_t dotPos = g_dragState.oldValueStr.find('.');
            std::string newValueStr;

            if (dotPos == std::string::npos) {
                // Целое число
                char modified[100];
                _itoa_s(g_dragState.newValue, modified, sizeof(modified), 10);
                newValueStr = modified;
            }
            else {
                // Число с плавающей точкой
                std::string intPartStr = g_dragState.oldValueStr.substr(0, dotPos);
                std::string fracPartStr = g_dragState.oldValueStr.substr(dotPos + 1);

                std::string suffix = "";
                if (!fracPartStr.empty() && (fracPartStr.back() == 'f' || fracPartStr.back() == 'F')) {
                    suffix = fracPartStr.back();
                    fracPartStr.pop_back();
                }

                size_t precision = fracPartStr.length();

                if (g_dragState.pointBefore) {
                    // Крутим дробную часть (.5)
                    long long fracLimit = 1;
                    for (size_t i = 0; i < precision; ++i) fracLimit *= 10;

                    // Безопасный парсинг целой части (если пустая или только минус, то это 0)
                    long long currentIntVal = 0;
                    bool isNegative = false;
                    if (!intPartStr.empty()) {
                        if (intPartStr == "-") isNegative = true;
                        else {
                            currentIntVal = std::stoll(intPartStr);
                            if (currentIntVal < 0 || intPartStr[0] == '-') isNegative = true;
                        }
                    }

                    long long currentFracVal = fracPartStr.empty() ? 0 : std::stoll(fracPartStr);
                    long long totalUnits = std::abs(currentIntVal) * fracLimit + currentFracVal;
                    if (isNegative) totalUnits = -totalUnits;

                    int currentDelta = g_dragState.newValue - g_dragState.lastValue;
                    totalUnits += currentDelta;

                    long long newIntVal = totalUnits / fracLimit;
                    long long newFracVal = std::abs(totalUnits % fracLimit);

                    std::string newIntStr = std::to_string(newIntVal);
                    // Сохраняем знак минус, если целая часть стала нулевой, но число отрицательное
                    if (totalUnits < 0 && newIntVal == 0) {
                        newIntStr = "-" + newIntStr;
                    }

                    std::string newFracStr = std::to_string(newFracVal);
                    if (newFracStr.length() < precision) {
                        newFracStr.insert(0, precision - newFracStr.length(), '0');
                    }

                    // Если изначально не было ведущего нуля (строка начиналась с '.' или '-.'), сохраняем этот стиль
                    if (intPartStr.empty() && newIntVal == 0 && totalUnits >= 0) {
                        newValueStr = "." + newFracStr + suffix;
                    }
                    else if (intPartStr == "-" && newIntVal == 0 && totalUnits < 0) {
                        newValueStr = "-." + newFracStr + suffix;
                    }
                    else {
                        newValueStr = newIntStr + "." + newFracStr + suffix;
                    }
                }
                else {
                    // Крутим целую часть (2.)
                    char modified[100];
                    _itoa_s(g_dragState.newValue, modified, sizeof(modified), 10);

                    // Если целая часть обнулилась, а ведущего нуля не было, сохраняем исходный формат
                    if (g_dragState.newValue == 0 && intPartStr.empty()) {
                        newValueStr = "." + fracPartStr + suffix;
                    }
                    else {
                        newValueStr = std::string(modified) + "." + fracPartStr + suffix;
                    }
                }
            }

            // Корректируем позицию курсора
            size_t newDotPos = newValueStr.find('.');
            long newCursorRelPos = 0;
            if (newDotPos != std::string::npos) {
                if (g_dragState.pointBefore) {
                    newCursorRelPos = static_cast<long>(newDotPos) + g_dragState.initialCursorAnchorOffset;
                }
                else {
                    // Защита: если целой части не было, позиция точки смещается на 0
                    long baseOffset = (newValueStr[0] == '.') ? 0 : static_cast<long>(newDotPos);
                    newCursorRelPos = baseOffset - g_dragState.initialCursorAnchorOffset;
                }
            }
            else {
                newCursorRelPos = static_cast<long>(newValueStr.length()) - g_dragState.initialCursorAnchorOffset;
            }

            long newCursorPhysicalCol = g_dragState.dragStartCol + newCursorRelPos;

            ReplaceTextInActiveVS(
                g_dragState.dragLine,
                g_dragState.dragStartCol,
                g_dragState.dragStartCol + static_cast<long>(g_dragState.currentTextLength),
                newValueStr,
                newCursorPhysicalCol
            );

            g_dragState.currentTextLength = newValueStr.length();
            g_dragState.oldValueStr = newValueStr;
            g_dragState.lastValue = g_dragState.newValue;
        }
    }


    inline void HandleMouseDrag(const POINT& pt, bool ctrl, bool shift) {
        if (!g_dragState.isDragging && !g_dragState.isProportionalStructDrag) return;

        if (!g_hShieldWnd) {
            CreateDragShield(pt);
        }

        if (g_dragState.isProportionalStructDrag) {
            DragProportionalStructValue(pt, ctrl, shift);
        }
        else {
            DragNumericValue(pt, ctrl, shift);
        }
    }

    inline void HandleMouseUp() {

        if (pWidgetStartEditPoint) {
            pWidgetStartEditPoint->Release();
            pWidgetStartEditPoint = nullptr;
        }
        if (pWidgetEndEditPoint) {
            pWidgetEndEditPoint->Release();
            pWidgetEndEditPoint = nullptr;
        }

        if (!g_dragState.isDragging && !g_dragState.isProportionalStructDrag) {
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

        if (g_dragState.isCursorHidden) {
            
            SetCursorPos(g_dragState.lockMousePos.x, g_dragState.lockMousePos.y);
            ShowCursor(TRUE);
            g_dragState.isCursorHidden = false;
        }

        EndUndoTransaction();

        g_dragState.isDragging = false;
        g_dragState.isProportionalStructDrag = false;
        g_dragState.targetParamId = -1;
        g_dragState.originalStructValues.clear();

        g_OverlayShow = false;
        g_OverlayText = "";

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

        bool buttonDown = (GetAsyncKeyState(LivePT_TriggerButton) & 0x8000) != 0;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        static bool s_PrevLButtonDown = false;

        if (buttonDown) {
            if (!s_PrevLButtonDown && !g_dragState.isDragging) {
                if (IsCursorOverActiveVSWindow()) {

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
