#if LivePT_TriggerButton == VK_LBUTTON
#define LPT_WM_BUTTONUP   WM_LBUTTONUP
#elif LivePT_TRIGGER_BUTTON == VK_MBUTTON
#define LPT_WM_BUTTONUP   WM_MBUTTONUP
#endif

#include "enumMenu.h"

namespace LivePT {

    inline void SaveActiveDocument();
    inline bool ReplaceTextInActiveVS(long line, long visualStartCol, long visualEndCol, const std::string& newText, long newCursorPhysicalCol);
    

    inline void HandleMouseDrag(const POINT& pt, bool ctrl, bool shift);
    inline void HandleMouseUp();

    // РАСШИРЕНИЕ ДЛЯ ПРОПОРЦИОНАЛЬНОГО ДРАГА СТРУКТУР
    struct ExtractedArg {
        std::string text;
        long startColOffset;
        long endColOffset;
    };

    

    // Токенизатор аргументов внутри фигурных скобок с учетом вложенности
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
                        std::string argA(argW.begin(), argW.end());
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
                std::string argA(argW.begin(), argW.end());
                args.push_back({ argA, static_cast<long>(currentArgStart + 1), static_cast<long>(i + 1) });
                currentArgStart = i + 1;
            }
            i++;
        }
        return args;
    }

    struct DragState {
        bool isDragging = false;
        int targetParamId = -1;
        int oldMouseY = 0;
        int lastValue = 0;
        int oldValue = 0;
        int newValue = 0;

        bool pointBefore = false;
        long dragLine = 0;

        long dragStartCol = 0;
        size_t currentTextLength = 0;

        long initialCursorAnchorOffset = 0;

        DWORD lastClickTime = 0;
        POINT lastClickPt = { 0, 0 };
        std::string startTextValue = "";
        std::string oldValueStr = "";
        std::string lastValueStr = "";

        // ДОБАВЛЕНО: Состояние пропорционального драга типа структуры
        bool isProportionalStructDrag = false;
        std::vector<float> originalStructValues;
    };


    static DragState g_dragState;

    struct PendingVsWrite {
        bool hasPending = false;
        long line = 0;
        long startCol = 0;
        std::string codeText;
    };

    static PendingVsWrite g_pendingWrite;

    // Сохраняем оригинальную сигнатуру с тремя параметрами, чтобы компилятор не ругался
    inline void PushPendingWrite(long line, long startCol, const std::string& text) {
        g_pendingWrite.line = line;
        g_pendingWrite.startCol = startCol;
        g_pendingWrite.codeText = text;
        g_pendingWrite.hasPending = true;
    }

    // Восстанавливаем оригинальную функцию FlushPendingWritesToVS без аргументов (принимает 0 параметров).
    // Теперь она вызывается каждый кадр из ProcessEdit() в главном потоке и мгновенно пишет в VS!
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
            SaveActiveDocument();

            g_dragState.currentTextLength = g_pendingWrite.codeText.length();
            g_pendingWrite.hasPending = false;
        }
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
            UpdateParamValue(id, pureName);

            SaveActiveDocument();
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
            UpdateParamValue(id, pureName); 

            SaveActiveDocument();
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
        g_dragState.pointBefore = (dotPos != std::string::npos && relativeOffset > static_cast<long>(dotPos));
        g_dragState.initialCursorAnchorOffset = (dotPos != std::string::npos) ? std::abs(static_cast<long>(dotPos) - relativeOffset) : static_cast<long>(cleanText.length()) - relativeOffset;

        if (dotPos == std::string::npos) g_dragState.oldValue = atoi(cleanText.c_str());
        else g_dragState.oldValue = atoi(g_dragState.pointBefore ? cleanText.substr(dotPos + 1).c_str() : cleanText.substr(0, dotPos).c_str());
        g_dragState.lastValue = g_dragState.oldValue;
    }

    inline void DragProportionalStructValue(const POINT& pt, bool ctrl, bool shift) {
        int id = g_dragState.targetParamId;
        if (id == -1 || id >= static_cast<int>(paramDesc.size()) || !paramDesc[id].structInfo.isStruct) return;

        // Вычисляем дельту мыши от точки клика
        int deltaY = -(pt.y - g_dragState.oldMouseY);

        CComVariant vtActiveDoc;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0)) || !vtActiveDoc.pdispVal) return;

        std::wstring currentLineText = DownloadCurrentLineText(vtActiveDoc.pdispVal);

        // ================== ТОЧЕЧНЫЙ ФИКС ДЛЯ ДВУХ МАКРОСОВ НА СТРОКЕ ==================
        // Нам нужно найти '{' именно ТЕКУЩЕГО макроса, а не самый первый на строке.
        // Используем g_dragState.dragStartCol как опорную точку, так как она хранит
        // позицию открывающей КРУГЛОЙ скобки '(' текущего макроса eval(...)
        size_t openBrace = std::wstring::npos;
        if (g_dragState.dragStartCol > 0 && static_cast<size_t>(g_dragState.dragStartCol) <= currentLineText.length()) {
            openBrace = currentLineText.find(L'{', g_dragState.dragStartCol - 1);
        }
        if (openBrace == std::wstring::npos) openBrace = currentLineText.find(L'{');
        if (openBrace == std::wstring::npos) { VariantClear(&vtActiveDoc); return; }
        // ==============================================================================

        auto args = TokenizeCallArguments(currentLineText, openBrace);

        // РЕВЕРСИВНЫЙ ЦИКЛ: С КОНЦА В НАЧАЛО
        for (size_t reverseIdx = args.size(); reverseIdx > 0; --reverseIdx) {
            size_t i = reverseIdx - 1;

            if (i >= g_dragState.originalStructValues.size() || i >= paramDesc[id].structInfo.members.size()) continue;

            if (g_dragState.originalStructValues[i] != -999999.0f) {
                std::string newText = "";
                char buf[64]{};

                const auto& member = paramDesc[id].structInfo.members[i];
                float startVal = g_dragState.originalStructValues[i];

                if (member.typeName == "char" || member.typeName == "unsigned char" || member.typeName == "signed char" ||
                    member.typeName == "int" || member.typeName == "unsigned int" || member.typeName == "long")
                {
                    float multiplier = 1.0f + (deltaY * 0.005f);
                    if (multiplier < 0.0f) multiplier = 0.0f;

                    float computedVal = startVal * multiplier;
                    if (startVal == 0.0f) {
                        float speedScale = ctrl ? 5.0f : (shift ? 0.2f : 1.0f);
                        computedVal += static_cast<float>(deltaY) * speedScale;
                    }

                    int intVal = static_cast<int>(computedVal + 0.5f);

                    if (member.typeName == "unsigned char") {
                        if (intVal > 255) intVal = 255;
                        if (intVal < 0) intVal = 0;
                    }

                    sprintf_s(buf, "%d", intVal);
                    newText = buf;
                }
                else
                {
                    float multiplier = 1.0f + (deltaY * 0.005f);
                    if (multiplier < 0.0f) multiplier = 0.0f;

                    float floatVal = startVal * multiplier;
                    if (startVal == 0.0f) {
                        float speedScale = ctrl ? 0.1f : (shift ? 0.001f : 0.01f);
                        floatVal += static_cast<float>(deltaY) * speedScale;
                    }

                    sprintf_s(buf, "%.4f", floatVal);
                    newText = buf;

                    while (newText.length() > 2 && newText.back() == '0' && newText[newText.length() - 2] != '.') {
                        newText.pop_back();
                    }
                    newText += "f";
                }

                long startCol = args[i].startColOffset;
                long endCol = args[i].endColOffset;

                ReplaceTextInActiveVS(g_dragState.dragLine, startCol, endCol, newText, startCol + static_cast<long>(newText.length()));
                UpdateParamValue(id, newText);
            }
        }

        VariantClear(&vtActiveDoc);
    }



    inline bool HandleMouseDown(const POINT& pt) {
        if (!initVsEditor()) return false;

        VARIANT vtActiveDoc;
        std::string currentFile;
        long line = 0, column = 0;
        std::wstring fileText;
        bool clickedInsideNumber = false;

        if (GetActiveVSContext(vtActiveDoc, currentFile, line, column, fileText)) {

            DWORD currentTime = GetTickCount();
            DWORD doubleClickTime = GetDoubleClickTime();
            bool isDoubleClick = (g_dragState.lastClickTime != 0) &&
                (currentTime - g_dragState.lastClickTime <= doubleClickTime) &&
                (std::abs(pt.x - g_dragState.lastClickPt.x) < 4) &&
                (std::abs(pt.y - g_dragState.lastClickPt.y) < 4);

            if (isDoubleClick) g_dragState.lastClickTime = 0;
            else g_dragState.lastClickTime = currentTime;
            g_dragState.lastClickPt = pt;

            size_t targetEvalAbsolutePos = std::wstring::npos;
            g_dragState.targetParamId = -1;

            std::wstring currentLineText = DownloadCurrentLineText(vtActiveDoc.pdispVal);

            EvalContext evalCtx = GetCurrentRawIdUnderCursor(fileText, line, column, currentLineText);

            if (evalCtx.rawId != -1 && evalCtx.absolutePos != std::wstring::npos) {
                targetEvalAbsolutePos = evalCtx.absolutePos;
                std::string normalizedPath = LivePT::NormalizePath(currentFile.c_str());

                auto it = g_filesMapsCache.find(normalizedPath);
                if (it != g_filesMapsCache.end()) {
                    const auto& currentFileMap = it->second;

                    if (evalCtx.rawId >= 0 && evalCtx.rawId < static_cast<int>(currentFileMap.size())) {
                        int validRuntimeId = currentFileMap[evalCtx.rawId];

                        if (validRuntimeId != -1) {
                            std::string vsLookupKey = normalizedPath + ":" + std::to_string(validRuntimeId);
                            g_dragState.targetParamId = getID(vsLookupKey);
                        }
                    }
                }
            }

            int id = g_dragState.targetParamId;
            long cursorColIdx = column - 1;

            // 1. ОБРАБОТКА ЕНАМОВ ПРИ ДАБЛКЛИКЕ
            if (id != -1 && paramDesc[id].enumInfo.isEnum) {
                if (isDoubleClick) {
                    if (ParseMacroValueBoundaries(fileText, line, targetEvalAbsolutePos)) {
                        InitMultiEnumSelection(id);
                    }
                }
                g_dragState.targetParamId = -1;
                VariantClear(&vtActiveDoc);
                return false;
            }

            // 2. ОБРАБОТКА КАСТОМНЫХ СТРУКТУР ПРИ ДАБЛКЛИКЕ / ПРОПОРЦИОНАЛЬНЫЙ ДРАГ ТИПА
            if (id != -1 && !paramDesc[id].enumInfo.isEnum) {

                // Вычисляем точное начало текущей строки в байтах
                size_t lineStartOffset = 0; long currentLineIdx = 1;
                while (currentLineIdx < line) {
                    lineStartOffset = fileText.find(L'\n', lineStartOffset) + 1;
                    currentLineIdx++;
                }

                // Локальная позиция текущего слова eval на этой строке
                size_t localEvalPosInLine = std::wstring::npos;
                if (targetEvalAbsolutePos != std::wstring::npos && targetEvalAbsolutePos >= lineStartOffset) {
                    localEvalPosInLine = targetEvalAbsolutePos - lineStartOffset;
                }

                // Ищем открывающую фигурую скобку СТРОГО после нашего eval, а не с начала строки
                size_t openBracePos = (localEvalPosInLine != std::wstring::npos)
                    ? currentLineText.find(L'{', localEvalPosInLine)
                    : currentLineText.find(L'{');

                // Ищем закрывающую круглую скобку ) нашего макроса eval(...)
                std::wstring macroName = GetConfiguredMacroName();
                size_t openBracketPos = (localEvalPosInLine != std::wstring::npos)
                    ? currentLineText.find(L'(', localEvalPosInLine + macroName.length())
                    : std::wstring::npos;

                size_t closeBracketPos = std::wstring::npos;
                if (openBracketPos != std::wstring::npos) {
                    size_t globalCloseBracket = FindCloseBracket(fileText, lineStartOffset + openBracketPos);
                    if (globalCloseBracket != std::wstring::npos) {
                        closeBracketPos = globalCloseBracket - lineStartOffset;
                    }
                }

                // Проверяем, что клик пришелся СТРОГО внутрь зоны инициализации типа
                if (openBracePos != std::wstring::npos && cursorColIdx < static_cast<long>(openBracePos)) {

                    long startValidZone = (localEvalPosInLine != std::wstring::npos) ? static_cast<long>(localEvalPosInLine) : 0;

                    if (cursorColIdx >= startValidZone && paramDesc[id].structInfo.isStruct) {

                        g_dragState.isProportionalStructDrag = true;
                        g_dragState.dragLine = line;
                        g_dragState.oldMouseY = pt.y;
                        g_dragState.originalStructValues.clear();

                        g_dragState.dragStartCol = (openBracketPos != std::wstring::npos) ? static_cast<long>(openBracketPos) + 1 : static_cast<long>(openBracePos);

                        StartUndoTransaction(L"LiveWheel Live Edit");

                        // Вырезаем аргументы строго внутри фигурных скобок ИМЕННО ЭТОЙ структуры
                        size_t closeBracePos = currentLineText.find(L'}', openBracePos);
                        std::wstring innerArgsW = L"";
                        if (closeBracePos != std::wstring::npos && closeBracePos > openBracePos) {
                            if (closeBracketPos != std::wstring::npos && closeBracePos > closeBracketPos) {
                                closeBracePos = currentLineText.rfind(L'}', closeBracketPos);
                            }
                            if (closeBracePos != std::wstring::npos && closeBracePos > openBracePos) {
                                innerArgsW = currentLineText.substr(openBracePos + 1, closeBracePos - openBracePos - 1);
                            }
                        }

                        if (innerArgsW.empty()) {
                            innerArgsW = currentLineText.substr(openBracePos + 1, (closeBracketPos != std::wstring::npos ? closeBracketPos : currentLineText.length()) - openBracePos - 1);
                        }

                        std::string innerArgs(innerArgsW.begin(), innerArgsW.end());
                        std::stringstream ss(innerArgs);
                        std::string token;

                        while (std::getline(ss, token, ',')) {
                            size_t eqPos = token.find('=');
                            if (eqPos == std::string::npos) eqPos = token.find(':');
                            std::string valPart = (eqPos != std::string::npos) ? token.substr(eqPos + 1) : token;

                            valPart.erase(0, valPart.find_first_not_of(" \t\r\n"));
                            valPart.erase(valPart.find_last_not_of(" \t\r\n") + 1);
                            while (!valPart.empty() && (valPart.back() == 'f' || valPart.back() == 'F' ||
                                valPart.back() == 'u' || valPart.back() == 'U' ||
                                valPart.back() == 'l' || valPart.back() == 'L')) {
                                valPart.pop_back();
                            }

                            float startVal = 0.0f;
                            auto [ptr, ec] = std::from_chars(valPart.data(), valPart.data() + valPart.size(), startVal);
                            if (ec == std::errc() && ptr == (valPart.data() + valPart.size())) {
                                g_dragState.originalStructValues.push_back(startVal);
                            }
                            else {
                                g_dragState.originalStructValues.push_back(-999999.0f);
                            }
                        }

                        VariantClear(&vtActiveDoc);
                        return true;
                    }
                }

                // ОБРАБОТКА ДАБЛКЛИКА ДЛЯ КАСТОМНЫХ ОКРУЖЕНИЙ (ЦВЕТОВЫЕ ПАЛИТРЫ И Т.Д.)
                if (isDoubleClick) {
                    g_dragState.isProportionalStructDrag = false;
                    if (ParseMacroValueBoundaries(fileText, line, targetEvalAbsolutePos)) {
                        std::string typeNameStr = paramDesc[id].value.type().name();
                        auto& registry = GetTypeCallbackRegistry();

                        if (registry.find(typeNameStr) != registry.end()) {
                            long targetLine = g_dragState.dragLine;
                            long targetStartCol = g_dragState.dragStartCol;

                            std::function<void(std::string)> vsUpdater = [targetLine, targetStartCol](std::string newCodeText) {
                                PushPendingWrite(targetLine, targetStartCol, newCodeText);
                                };
                            registry[typeNameStr](paramDesc[id].value, vsUpdater);
                        }
                    }
                    g_dragState.targetParamId = -1;
                    VariantClear(&vtActiveDoc);
                    return false;
                }
            }


            // 3. ПАРСИНГ ОБЫЧНЫХ ЧИСЕЛ ДЛЯ ДРАГА
            if (!g_dragState.isProportionalStructDrag && cursorColIdx >= 0 && cursorColIdx < static_cast<long>(currentLineText.length())) {
                long startCol = cursorColIdx;
                long endCol = cursorColIdx;

                while (startCol > 0 && (iswdigit(currentLineText[startCol - 1]) ||
                    currentLineText[startCol - 1] == L'.' ||
                    currentLineText[startCol - 1] == L'-' ||
                    currentLineText[startCol - 1] == L'f' ||
                    currentLineText[startCol - 1] == L'F')) {
                    startCol--;
                }

                while (endCol < static_cast<long>(currentLineText.length()) && (iswdigit(currentLineText[endCol]) ||
                    currentLineText[endCol] == L'.' ||
                    currentLineText[endCol] == L'-' ||
                    currentLineText[endCol] == L'f' ||
                    currentLineText[endCol] == L'F')) {
                    endCol++;
                }

                if (startCol < endCol) {
                    std::wstring numW = currentLineText.substr(startCol, endCol - startCol);
                    std::string cleanText(numW.begin(), numW.end());

                    g_dragState.dragLine = line;
                    g_dragState.dragStartCol = startCol + 1;
                    g_dragState.currentTextLength = cleanText.length();
                    g_dragState.oldMouseY = pt.y;

                    size_t relativeCursorIdx = cursorColIdx - startCol;
                    InitNumericDragState(relativeCursorIdx, cleanText);

                    clickedInsideNumber = true;
                }
            }

            VariantClear(&vtActiveDoc);
        }

        return (clickedInsideNumber || g_dragState.isProportionalStructDrag);
    }


    inline void DragNumericValue(const POINT& pt, bool ctrl, bool shift) {

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
                char modified[100];
                _itoa_s(g_dragState.newValue, modified, sizeof(modified), 10);
                newValueStr = modified;
            }
            else {
                std::string intPartStr = g_dragState.oldValueStr.substr(0, dotPos);
                std::string fracPartStr = g_dragState.oldValueStr.substr(dotPos + 1);
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

                    int currentDelta = g_dragState.newValue - g_dragState.lastValue;
                    totalUnits += currentDelta;

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

            ReplaceTextInActiveVS(g_dragState.dragLine, g_dragState.dragStartCol, g_dragState.dragStartCol + static_cast<long>(g_dragState.currentTextLength), newValueStr, newCursorPhysicalCol);

            g_dragState.currentTextLength = newValueStr.length();
            g_dragState.oldValueStr = newValueStr;

            int targetId = g_dragState.targetParamId;
            if (targetId != -1) {
                UpdateParamValue(targetId, newValueStr);
                g_dragState.lastValueStr = newValueStr;
            }
            g_dragState.lastValue = g_dragState.newValue;
        }
    }


    // ОБНОВЛЕННЫЙ МЕТОД HANDLE_MOUSE_DRAG
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

    // ОБНОВЛЕННЫЙ МЕТОД HANDLE_MOUSE_UP
    inline void HandleMouseUp() {
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

        EndUndoTransaction();
        SaveActiveDocument();

        g_dragState.isDragging = false;
        g_dragState.isProportionalStructDrag = false;
        g_dragState.targetParamId = -1;
        g_dragState.originalStructValues.clear();

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
