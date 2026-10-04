#if LivePT_TriggerButton == VK_LBUTTON
#define LPT_WM_BUTTONUP   WM_LBUTTONUP
#elif LivePT_TriggerButton == VK_MBUTTON
#define LPT_WM_BUTTONUP   WM_MBUTTONUP
#endif

#include "enumMenu.h"

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
        bool isCursorHidden = false;  // Флаг того, что системный курсор сейчас спрятан
        POINT lockMousePos = { 0, 0 }; // Точка старта драга, куда курсор принудительно возвращается
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

            long physicalStartCol = 1;
            long physicalEndCol = 1;
            const long TAB_SIZE = 4;
            size_t currentVisualCol = 1;

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

            // ТРАНСЛЯЦИЯ КУРСОРA: Переводим финальную visual-колонку в честный символьный сдвиг
            std::wstring wNewText(newText.begin(), newText.end());
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

            std::wstring wText(newText.begin(), newText.end()); CComBSTR bstrText(wText.c_str());
            CComVariant vText(bstrText); CComVariant vOption(1L);

            hr = AutoWrap(DISPATCH_METHOD, NULL, pEditStart.pdispVal, L"ReplaceText", 3, pEditEnd, vText, vOption);
            if (FAILED(hr)) return false;

            // Передаем точный физический офсет символа строки
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
        g_dragState.pointBefore = (dotPos != std::wstring::npos && relativeOffset > static_cast<long>(dotPos));
        g_dragState.initialCursorAnchorOffset = (dotPos != std::wstring::npos) ? std::abs(static_cast<long>(dotPos) - relativeOffset) : static_cast<long>(cleanText.length()) - relativeOffset;

        if (dotPos == std::wstring::npos) {
            g_dragState.oldValue = atoi(cleanText.c_str());
        }
        else {
            g_dragState.oldValue = atoi(g_dragState.pointBefore ? cleanText.substr(dotPos + 1).c_str() : cleanText.substr(0, dotPos).c_str());
        }

        // СТРОГАЯ СИНХРОНИЗАЦИЯ: Предотвращает фальстарт замене текста при клике
        g_dragState.lastValue = g_dragState.oldValue;
        g_dragState.newValue = g_dragState.oldValue;
    }


    inline void DragProportionalStructValue(const POINT& pt, bool ctrl, bool shift) {
        int id = g_dragState.targetParamId;
        if (id == -1 || id >= static_cast<int>(paramDesc.size()) || !paramDesc[id].structInfo.isStruct) return;

        // 1. [ТВОЙ КОД] Поиск зарегистрированного колбека по типу из std::any (RTTI)
        std::string typeName = paramDesc[id].value.type().name();
        if (typeName.find("struct ") == 0) typeName = typeName.substr(7);
        else if (typeName.find("class ") == 0) typeName = typeName.substr(6);

        auto& registry = GetCustomDragRegistry();
        auto it = registry.find(typeName);

        if (it == registry.end()) return;

        DragMathCallback mathCallback = it->second;

        // 2. [ТВОЙ КОД] Расчет покадровой дельты мыши
        DragMathInput input;
        input.mouseFrameDeltaX = pt.x - g_dragState.lockMousePos.x;
        input.mouseFrameDeltaY = -(pt.y - g_dragState.lockMousePos.y);
        input.ctrl = ctrl;
        input.shift = shift;

        if (input.mouseFrameDeltaX != 0 || input.mouseFrameDeltaY != 0) {
            mathCallback(g_dragState.originalStructValues, input);
            SetCursorPos(g_dragState.lockMousePos.x, g_dragState.lockMousePos.y);
        }

        // 3. Инфраструктура Visual Studio API
        CComVariant vtActiveDoc;
        if (FAILED(AutoWrap(DISPATCH_PROPERTYGET, &vtActiveDoc, pDTE, L"ActiveDocument", 0)) || !vtActiveDoc.pdispVal) return;

        std::wstring currentLineText = DownloadCurrentLineText(vtActiveDoc.pdispVal);

        // Находим физическое смещение начала макроса в строке, основываясь на p.column из базы
        long tabSize = GetVSTabSize();
        long currentVisualCol = 1;
        size_t macroStartIdx = currentLineText.length();
        for (size_t i = 0; i < currentLineText.length(); ++i) {
            if (currentVisualCol >= paramDesc[id].column) { macroStartIdx = i; break; }
            currentVisualCol += (currentLineText[i] == L'\t') ? (tabSize - ((currentVisualCol - 1) % tabSize)) : 1;
        }

        // Ищем скобку { строго от начала нашего макроса
        size_t openBrace = currentLineText.find(L'{', macroStartIdx);
        if (openBrace == std::wstring::npos) openBrace = currentLineText.find(L'{');
        if (openBrace == std::wstring::npos) { VariantClear(&vtActiveDoc); return; }

        auto args = TokenizeCallArguments(currentLineText, openBrace);
        if (args.empty()) { VariantClear(&vtActiveDoc); return; }

        // 4. [ТВОЙ КОД] Сборка результирующей строки аргументов структуры
        std::string fullResultString = "";
        for (size_t i = 0; i < args.size(); ++i) {
            std::string elementText = args[i].text;

            if (i < g_dragState.originalStructValues.size() && g_dragState.originalStructValues[i] != -999999.0f) {
                char buf[64]{};

                bool isIntegerMember = false;
                if (i < paramDesc[id].structInfo.members.size()) {
                    const auto& member = paramDesc[id].structInfo.members[i];
                    isIntegerMember = (member.typeName == "char" ||
                        member.typeName == "unsigned char" ||
                        member.typeName == "signed char" ||
                        member.typeName == "int" ||
                        member.typeName == "unsigned int" ||
                        member.typeName == "long");
                }

                if (isIntegerMember) {
                    int intVal = static_cast<int>(g_dragState.originalStructValues[i] + (g_dragState.originalStructValues[i] >= 0.0f ? 0.5f : -0.5f));
                    sprintf_s(buf, "%d", intVal);
                    elementText = buf;
                }
                else {
                    sprintf_s(buf, "%.4f", g_dragState.originalStructValues[i]);
                    elementText = buf;

                    while (elementText.length() > 2 && elementText.back() == '0' && elementText[elementText.length() - 2] != '.') {
                        elementText.pop_back();
                    }
                    elementText += "f";
                }
            }

            fullResultString += elementText;
            if (i < args.size() - 1) {
                fullResultString += ", ";
            }
        }

        // 5. ПЕРЕВОД КООРДИНАТ: Конвертируем относительные смещения токенов TokenizeCallArguments 
        // в абсолютные визуальные колонки Visual Studio
        size_t physReplaceStart = openBrace + 1 + (args.front().startColOffset - (openBrace + 2));
        size_t physReplaceEnd = openBrace + 1 + (args.back().endColOffset - (openBrace + 2));

        long visualReplaceStart = GetVisualColumn(currentLineText, physReplaceStart);
        long visualReplaceEnd = GetVisualColumn(currentLineText, physReplaceEnd);
        long newCursorPhysicalCol = visualReplaceStart + static_cast<long>(fullResultString.length());

        // Безопасная замена по честным визуальным колонкам
        ReplaceTextInActiveVS(
            g_dragState.dragLine,
            visualReplaceStart,
            visualReplaceEnd,
            fullResultString,
            newCursorPhysicalCol
        );

        // Обновляем метрику длины текста для поддержки непрерывного драга
        g_dragState.currentTextLength = fullResultString.length();

        VariantClear(&vtActiveDoc);
    }

    inline long AdjustCursorIndexForNumericContext(const std::wstring& lineText, long originalVisualColumn) {
        if (lineText.empty()) return 0;

        // 1. ЧЕСТНЫЙ ПЕРЕВОД: Конвертируем визуальную колонку VS в физический индекс строки wchar_t
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

        // Если кликнули в самый конец строки за пределами текста, берем последний символ
        if (physicalIdx >= lineText.length()) {
            physicalIdx = lineText.length() - 1;
        }

        long cursorIdx = static_cast<long>(physicalIdx);
        if (cursorIdx < 0) return 0;

        // 2. АДАПТИВНЫЙ СДВИГ: Если стоим на пробеле/запятой, но слева число — подхватываем его
        bool isCurrentNumeric = false;
        if (cursorIdx < static_cast<long>(lineText.length())) {
            wchar_t ch = lineText[cursorIdx];
            isCurrentNumeric = (iswdigit(ch) || ch == L'.' || ch == L'-' || ch == L'f' || ch == L'F');
        }

        if (!isCurrentNumeric && cursorIdx > 0 && (cursorIdx - 1) < static_cast<long>(lineText.length())) {
            wchar_t leftCh = lineText[cursorIdx - 1];
            if (iswdigit(leftCh) || leftCh == L'.' || leftCh == L'-' || leftCh == L'f' || leftCh == L'F') {
                return cursorIdx - 1;
            }
        }

        return cursorIdx;
    }



    // === ПОДФУНКЦИЯ 2: Проверка лексических границ числового символа ===
    inline bool IsNumericTokenChar(wchar_t ch) {
        return iswdigit(ch) || ch == L'.' || ch == L'-' || ch == L'f' || ch == L'F';
    }

    // === ПОДФУНКЦИЯ 3: Сканирование физических границ токена числа на строке ===
    inline void ScanNumericTokenBoundaries(const std::wstring& lineText, long startIdx, long& outStartCol, long& outEndCol) {
        outStartCol = startIdx;
        outEndCol = startIdx;

        while (outStartCol > 0 && IsNumericTokenChar(lineText[outStartCol - 1])) {
            outStartCol--;
        }
        while (outEndCol < static_cast<long>(lineText.length()) && IsNumericTokenChar(lineText[outEndCol])) {
            outEndCol++;
        }
    }

    // === ПОДФУНКЦИЯ 4: Инициализация и запуск сессии одиночного числового драга ===
    inline bool TryInitSingleNumericDrag(const std::wstring& lineText, long line, long originalColumn, long targetCharIdx, const POINT& pt) {
        long startCol = 0;
        long endCol = 0;

        ScanNumericTokenBoundaries(lineText, targetCharIdx, startCol, endCol);

        if (startCol < endCol) {
            std::wstring numW = lineText.substr(startCol, endCol - startCol);
            std::string cleanText(numW.begin(), numW.end());

            g_dragState.dragLine = line;
            g_dragState.dragStartCol = GetVisualColumn(lineText, startCol);
            g_dragState.currentTextLength = cleanText.length();
            g_dragState.oldMouseX = pt.x;
            g_dragState.oldMouseY = pt.y;

            // === ИСПРАВЛЕНИЕ: Считаем смещение строго в физических индексах строки ===
            long physicalCursorIdx = targetCharIdx; // Используем уже переведенный честный индекс!
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





    // === ПОДФУНКЦИЯ 5: Мгновенное переключение булевых значений по даблклику ===
    inline bool TryToggleBooleanDirect(const std::wstring& lineText, long line, long targetCharIdx) {
        // Выделяем границы слова вокруг курсора
        long startCol = targetCharIdx;
        long endCol = targetCharIdx;

        while (startCol > 0 && iswalpha(lineText[startCol - 1])) startCol--;
        while (endCol < static_cast<long>(lineText.length()) && iswalpha(lineText[endCol])) endCol++;

        if (startCol >= endCol) return false;

        std::wstring wordW = lineText.substr(startCol, endCol - startCol);
        std::string wordA(wordW.begin(), wordW.end());

        std::string newValueStr = "";
        // Реагируем СТРОГО на текстовые литералы true/false
        if (wordA == "true" || wordA == "TRUE") {
            newValueStr = "false";
        }
        else if (wordA == "false" || wordA == "FALSE") {
            newValueStr = "true";
        }

        // Если это булевое слово — мгновенно отправляем замену текста в Visual Studio
        if (!newValueStr.empty()) {
            long visualStart = GetVisualColumn(lineText, startCol);
            long visualEnd = visualStart + static_cast<long>(wordA.length());
            long newCursorCol = visualStart + static_cast<long>(newValueStr.length());

            StartUndoTransaction(L"LiveWheel Toggle Bool");
            ReplaceTextInActiveVS(line, visualStart, visualEnd, newValueStr, newCursorCol);
            EndUndoTransaction();

            SaveActiveDocument();
            return true;
        }

        return false;
    }

    // === ПОДФУНКЦИЯ 6: Полностью автономное открытие меню Энумов по даблклику (БЕЗ paramId) ===
    inline bool TryInitEnumSelectionDirect(const std::wstring& lineText, long line, long column, long targetCharIdx) {
        if (lineText.empty()) return false;

        // 1. Выделяем границы текущего выбранного токена (например, "box")
        long tokenStart = targetCharIdx;
        long tokenEnd = targetCharIdx;
        while (tokenStart > 0 && (iswalnum(lineText[tokenStart - 1]) || lineText[tokenStart - 1] == L'_')) tokenStart--;
        while (tokenEnd < static_cast<long>(lineText.length()) && (iswalnum(lineText[tokenEnd]) || lineText[tokenEnd] == L'_')) tokenEnd++;

        if (tokenStart >= tokenEnd) return false;

        // 2. СВЕРХТОЧНЫЙ ТЕКСТОВЫЙ ПОИСК ПРЕФИКСА ТИПА (Ищем "ptype::" слева от токена)
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

        // Если префикс типа "ptype::" в коде не написан, мы не сможем найти его в PDB — выходим
        if (enumTypeNameW.empty()) return false;

        // 3. ПРЯМОЙ ЗАПРОС К БАЗЕ PDB (Мгновенно вытаскиваем метаданные через DbgHelp)
        std::vector<std::string> parsedNames;
        std::vector<int> parsedValues;

        if (!LoadEnumMetadataDirect(enumTypeNameW.c_str(), parsedNames, parsedValues)) {
            return false; // Если в PDB такого энума нет (или это не энум) — тихо выходим
        }

        // 4. ВЫЗОВ КОНТЕКСТНОГО МЕНЮ WIN32
        int newIndex = showEnum(parsedNames);

        if (newIndex >= 0 && newIndex < static_cast<int>(parsedNames.size())) {
            std::string pureName = parsedNames[newIndex];
            std::string typePrefixA(enumTypeNameW.begin(), enumTypeNameW.end());

            // Собираем итоговую строку для вставки в Visual Studio, например "ptype::roundbox"
            std::string newValueStr = typePrefixA + "::" + pureName;

            // Находим точные визуальные координаты всего выражения "ptype::box" для полной замены
            long typeStartCol = tokenStart - 2;
            while (typeStartCol > 0 && (iswalnum(lineText[typeStartCol - 1]) || lineText[typeStartCol - 1] == L'_')) typeStartCol--;

            long visualStart = GetVisualColumn(lineText, typeStartCol);
            long visualEnd = GetVisualColumn(lineText, tokenEnd);
            long newCursorCol = visualStart + static_cast<long>(newValueStr.length());

            // Безопасно подменяем текст в буфере Visual Studio
            StartUndoTransaction(L"LiveWheel Change Enum");
            ReplaceTextInActiveVS(line, visualStart, visualEnd, newValueStr, newCursorCol);
            EndUndoTransaction();

            SaveActiveDocument();
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

    // Эту легкую функцию виджет будет вызывать вместо старого vsUpdater
    inline void PushWidgetTextUpdate(long line, long startCol, const std::string& text) {
        g_widgetBuffer.line = line;
        g_widgetBuffer.startCol = startCol;
        g_widgetBuffer.codeText = text;
        g_widgetBuffer.hasChanges = true;
    }
    

    inline bool TryInitStructWidgetDirect(const std::wstring& fileText, const std::wstring& lineText, long line, long column, long targetCharIdx) {
        if (fileText.empty() || lineText.empty()) return false;

        // 1. Вырезаем имя типа из текста текущей строки (например, "pos2")
        long tokenStart = targetCharIdx; long tokenEnd = targetCharIdx;
        while (tokenStart > 0 && iswalnum(lineText[tokenStart - 1])) tokenStart--;
        while (tokenEnd < static_cast<long>(lineText.length()) && iswalnum(lineText[tokenEnd])) tokenEnd++;
        if (tokenStart >= tokenEnd) return false;

        std::wstring typeNameW = lineText.substr(tokenStart, tokenEnd - tokenStart);
        std::string typeNameA(typeNameW.begin(), typeNameW.end());

        // Проверяем, зарегистрирован ли вообще такой текстовый виджет в системе
        auto& registry = GetTypeCallbackRegistry();
        if (registry.find(typeNameA) == registry.end()) return false;

        // 2. Находим абсолютное смещение начала макроса в ПОЛНОМ тексте файла
        size_t lineStartOffset = 0; long currentLineIdx = 1;
        while (currentLineIdx < line && lineStartOffset < fileText.length()) {
            size_t nextNL = fileText.find(L'\n', lineStartOffset);
            if (nextNL != std::wstring::npos) { lineStartOffset = nextNL + 1; currentLineIdx++; }
            else break;
        }
        size_t absoluteTokenEndOffset = lineStartOffset + tokenEnd;

        // 3. Сканируем ПОЛНЫЙ текст файла вправо от имени типа, чтобы определить характер скобок
        size_t globalOpenIdx = std::wstring::npos;
        wchar_t openChar = L'\0'; wchar_t closeChar = L'\0';

        for (size_t k = absoluteTokenEndOffset; k < fileText.length(); ++k) {
            if (fileText[k] == L'{') { openChar = L'{'; closeChar = L'}'; globalOpenIdx = k; break; }
            if (fileText[k] == L'(') { openChar = L'('; closeChar = L')'; globalOpenIdx = k; break; }
            if (!iswspace(fileText[k])) break;
        }

        if (globalOpenIdx == std::wstring::npos) return false;

        // Ищем парную закрывающую скобку по всему объему файла
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

        g_dragState.dragLine = line;

        // 4. Вычисляем физические координаты СТАРТА аргументов (после открывающей скобки)
        long startLine = line;
        size_t lastNL_start = lineStartOffset;
        size_t scanNL_start = fileText.find(L'\n', lineStartOffset);
        while (scanNL_start != std::wstring::npos && scanNL_start < globalOpenIdx) {
            startLine++;
            lastNL_start = scanNL_start + 1;
            scanNL_start = fileText.find(L'\n', lastNL_start);
        }
        long startOffset = static_cast<long>(globalOpenIdx - lastNL_start) + 1; // Ровно на скобку {

        // 5. Вычисляем физические координаты КОНЦА аргументов (СТРОГО на символ закрывающей скобки)
        long closeLine = line;
        size_t lastNL_end = lineStartOffset;
        size_t scanNL_end = fileText.find(L'\n', lineStartOffset);
        while (scanNL_end != std::wstring::npos && scanNL_end < globalCloseIdx) {
            closeLine++;
            lastNL_end = scanNL_end + 1;
            scanNL_end = fileText.find(L'\n', lastNL_end);
        }
        long closeOffset = static_cast<long>(globalCloseIdx - lastNL_end) + 1; // Ровно на скобку }

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
                        pWidgetStartEditPoint = vtActivePoint.pdispVal;
                        pWidgetStartEditPoint->AddRef();
                        AutoWrap(DISPATCH_METHOD, NULL, pWidgetStartEditPoint, L"MoveToLineAndOffset", 2, CComVariant(startLine), CComVariant(startOffset));
                    }

                    hr = AutoWrap(DISPATCH_METHOD, &vtActivePoint, vtActivePoint.pdispVal, L"CreateEditPoint", 0);
                    if (SUCCEEDED(hr) && vtActivePoint.vt == VT_DISPATCH && vtActivePoint.pdispVal) {
                        pWidgetEndEditPoint = vtActivePoint.pdispVal;
                        pWidgetEndEditPoint->AddRef();
                        AutoWrap(DISPATCH_METHOD, NULL, pWidgetEndEditPoint, L"MoveToLineAndOffset", 2, CComVariant(closeLine), CComVariant(closeOffset));
                    }
                }
                VariantClear(&vtActivePoint); VariantClear(&vtSelection);
            }
            VariantClear(&vtActiveDoc);
        }

        size_t localOpenIdxInLine = globalOpenIdx - lineStartOffset;
        g_dragState.dragStartCol = GetVisualColumn(lineText, localOpenIdxInLine + 1);

        std::wstring innerArgsW = fileText.substr(globalOpenIdx + 1, globalCloseIdx - globalOpenIdx - 1);
        std::string innerArgsA(innerArgsW.begin(), innerArgsW.end());

        g_dragState.currentTextLength = innerArgsW.length();
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

        // Гарантируем сброс старого покадрового состояния
        g_dragState.targetParamId = -1;
        g_dragState.isProportionalStructDrag = false;

        // Расчет факта Даблклика
        DWORD currentTime = GetTickCount();
        DWORD doubleClickTime = GetDoubleClickTime();
        bool isDoubleClick = (g_dragState.lastClickTime != 0) &&
            (currentTime - g_dragState.lastClickTime <= doubleClickTime) &&
            (std::abs(pt.x - g_dragState.lastClickPt.x) < 4) &&
            (std::abs(pt.y - g_dragState.lastClickPt.y) < 4);

        if (isDoubleClick) g_dragState.lastClickTime = 0;
        else g_dragState.lastClickTime = currentTime;
        g_dragState.lastClickPt = pt;

        // Применяем адаптивное смещение каретки (клик вплотную справа от числа)
        long targetCharIdx = AdjustCursorIndexForNumericContext(currentLineText, column);

        if (targetCharIdx < 0 || targetCharIdx >= static_cast<long>(currentLineText.length())) {
            VariantClear(&vtActiveDoc);
            return false;
        }

        // =========================================================================
        // [ФУНКЦИОНАЛ ДАБЛКЛИКА]: Открываем тяжелые GUI-окна, палитры и меню
        // =========================================================================
        if (isDoubleClick) {
            if (TryToggleBooleanDirect(currentLineText, line, targetCharIdx)) { VariantClear(&vtActiveDoc); return false; }
            if (TryInitEnumSelectionDirect(currentLineText, line, column, targetCharIdx)) { VariantClear(&vtActiveDoc); return false; }
            if (TryInitStructWidgetDirect(fileText, currentLineText, line, column, targetCharIdx)) { VariantClear(&vtActiveDoc); return false; }
            VariantClear(&vtActiveDoc);
            return false;
        }

        // =========================================================================
        // [ФУНКЦИОНАЛ ОДИНОЧНОГО КЛИКА]: Высокоскоростной драг конкретного числа!
        // =========================================================================
        wchar_t targetedChar = currentLineText[targetCharIdx];
        bool isNumeric = IsNumericTokenChar(targetedChar);

        if (isNumeric) {
            if (TryInitSingleNumericDrag(currentLineText, line, column, targetCharIdx, pt)) {
                VariantClear(&vtActiveDoc);
                return true;
            }
        }

        VariantClear(&vtActiveDoc);
        return false;
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

        // === ВОЗВРАЩАЕМ КУРСОРУ ВИДИМОСТЬ ===
        if (g_dragState.isCursorHidden) {
            ShowCursor(TRUE);
            g_dragState.isCursorHidden = false;
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
 // namespace LivePT



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
