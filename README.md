# 🚀 Live Programming Toolkit (LivePT) — v0.5

[🇷🇺 Читать на русском языке](#-live-programming-toolkit-livept--v05-ru) | [🇬🇧 Read in English](#-live-programming-toolkit-livept--v05-en)

---

## 🇷🇺 Live Programming Toolkit (LivePT) — v0.5 [RU]

Легковесный интерактивный инструмент разработки для **C++20 и Visual Studio** с нулевыми затратами ресурсов в релизной сборке. Позволяет изменять константы, перечисления и структуры в коде прямо во время работы приложения без перезапуска.

### 📋 Требования к среде и компилятору
Перед интеграцией библиотеки убедитесь, что ваш проект настроен согласно следующим требованиям:

1. **Стандарт языка C++:** `Свойства проекта` ➔ `C/C++` ➔ `Язык C++` ➔ **`ISO C++20 Standard (/std:c++20)`** (или выше).
2. **Конформный препроцессор:** `Свойства проекта` ➔ `C/C++` ➔ `Препроцессор` ➔ `Использовать конформный препроцессор` ➔ **`Да (/Zc:preprocessor)`**.
3. **Генерация отладочных символов:** В настройках компоновщика должна быть включена генерация **PDB-файлов** (критически важно для работы подсистемы `DbgHelp`).
4. **Настройка мыши в Visual Studio:** Если в качестве триггера используется средняя кнопка мыши (`VK_MBUTTON`), в настройках VS (`Сервис` ➔ `Параметры` ➔ `Текстовый редактор` ➔ `Общие`) **снимите галочку** с пункта **«Middle click to scroll»**, чтобы избежать конфликтов со считыванием координат.

### 🚀 Инструкция по быстрой установке

1. Перенесите папку `LivePT` в директорию вашего C++ проекта.
2. В самом начале вашего основного файла (`main.cpp` / `WinMain`) подключите заголовочный файл, строго предварив его флагом активации:
   ```cpp
   #define LivePT_EditMode true // true — активирует систему. false — полностью вырезает LivePT из бинарника на этапе компиляции
   #include "LivePT/LivePT.h"
   ```
3. Добавьте вызов диспетчера обновлений в ваш главный игровой или рендер-цикл приложения:
   ```cpp
   while (true) {
       if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
           if (msg.message == WM_QUIT) break;
           TranslateMessage(&msg);
           DispatchMessage(&msg);
       } else {
           LivePT::ProcessEdit(); // Главный триггер покадрового анализа кода
       }
   }
   ```

### ⚙️ Глобальная конфигурация (`LivePT.h`)
Основные пользовательские настройки задаются дефайнами в блоке конфигурации заголовочного файла:

* `#define LivePT_Mouse true` — Разрешает интерактивное взаимодействие с кодом (драг мышью, вызов контекстных меню энумов, отображение UI-плашек).
* `#define LivePT_WindowManagement true` — Модуль `LptWindowManager`. При запуске на одном мониторе автоматически делит рабочую область экрана ровно **50/50** между окном вашего приложения и Visual Studio, а при закрытии — возвращает окно IDE в исходное состояние.
* `#define LivePT_AppToSecondaryDisplay false` — Автоматически переносит и разворачивает окно вашего приложения на весь экран второго дисплея.
* `#define LivePT_TriggerButton VK_LBUTTON` — Физическая клавиша мыши для захвата констант в коде (`VK_LBUTTON` — левая кнопка, `VK_MBUTTON` — колесико мыши).

### 🎮 Интерактивное управление (Мышь и Клавиатура)

#### 🖱 Взаимодействие с помощью мыши
* **Плавное изменение чисел (Numeric Drag):** Наведите курсор на числовой литерал внутри `eval()`, зажмите клавишу-триггер и перемещайте мышь вверх или вниз. Значение в коде Visual Studio и в памяти приложения начнет синхронно меняться.
* **Модификаторы скорости:** Удерживайте `Shift` во время драга для ускорения шага изменения в 10 раз, или `Ctrl` — для ускорения в 100 раз.
* **Мгновенная инверсия флагов по двойному клику:** Сделайте **двойной клик** кнопкой-триггером мыши по литералу `eval(true)` или `eval(false)`. Состояние логического флага мгновенно инвертируется в текстовом документе.
* **Интерактивное меню `enum`:** Сделайте двойной клик по элементу перечисления (например, `eval(ptype::box)`). Библиотека считает префикс типа, запросит у `DbgHelp` все доступные варианты этого `enum` и отобразит темное нативное контекстное меню Win32 для быстрого выбора.
* **Пропорциональный драг структур (Struct Drag):** Если вы зажмете мышь на имени типа или открывающей скобке выражения `eval(pos2{...})`, перемещение мыши будет пропорционально масштабировать (умножать) все внутренние поля структуры одновременно.

### 📦 Поддерживаемый синтаксис и ограничения макроса `eval(...)`

#### ✅ Что МОЖНО оборачивать в `eval()`:

1. **Примитивные константные литералы:** Любые целые числа и числа с плавающей точкой, включая отрицательные значения и специфические суффиксы.
2. **Элементы перечислений (как стандартных, так и `enum class`).**
3. **Именованные агрегатные структуры (Явный тип):** При передаче структуры с указанием ее имени, ядро LivePT побайтово переносит измененные поля констант в рантайм.
4. **Сложная вложенность структур:** Побайтовый движок `WriteStructBytes` рекурсивно раскладывает аргументы любой глубины вложенности, если их типы известны PDB.
5. **Динамические векторы констант (`std::vector<T>`) с возможностью изменения размера:**
   LivePT v0.5 поддерживает глубокое отслеживание динамических массивов. Вы можете оборачивать векторы структур в `eval(...)` целиком:
   ```cpp
   auto points = eval(std::vector<HeavyPoint>{
       { true, pos2{-52.0f, 24.0f}, 5, 94, color3{239, 61, 125} },
       { true, pos2{-24.0f, 31.0f}, 2, 95, color3{95, 35, 59} }
   });
   ```
   * **Добавление и удаление элементов:** Прокси-парсер `MsvcVectorProxy` автоматически сравнивает количество элементов в исходном коде Visual Studio с количеством элементов в runtime-памяти. Если вы вручную допишете новый элемент структуры через запятую в коде Visual Studio или удалите существующий, ядро LivePT автоматически выполнит операцию `resize()` для вектора в памяти запущенного приложения, пересчитает сквозную битовую маску валидности полей `validFieldsMask` и мгновенно перераспределит кучу без утечек памяти и падений.
6. **Смешивание констант и переменных внутри структур:** Вы можете передавать динамические runtime-переменные внутрь инициализации структуры в `eval()`. Лексический анализатор ядра автоматически распознает имена переменных и пропустит их, позволяя интерактивно крутить только находящиеся рядом константные литералы.

#### ❌ Ограничения макроса (Чего делать НЕЛЬЗЯ):
1. **Динамические вычисления вне структур:** Нельзя писать формулы или вызовы функций в качестве изолированных аргументов, например: `eval(a + b)`.
2. **Безымянная инициализация (Raw Initializers):** Конструкции вида `eval({10.0f, 20.0f})` без указания имени структуры (`pos2{10.0f, 20.0f}`) не поддерживаются.
3. **Использование сложных динамических контейнеров внутри структур:** Поля структур, оборачиваемых в `eval`, должны состоять из тривиальных типов (`int`, `float`, `bool`, `char`, `double`) или вложенных агрегатов. Использование `std::string` или `std::vector` *внутри* редактируемой структуры запрещено.

### 🛠️ Расширение возможностями ядра: Графические виджеты и Обратные вызовы перетаскивания (Drag Callbacks)

⚠️ **ВАЖНОЕ ПОЯСНЕНИЕ:** Графические контроллеры (такие как **ColorPicker**, **PosPicker** или **AnglePicker**) **НЕ являются частью неизменяемого ядра LivePT**. Это демонстрационные примеры из области **User Space** (пользовательского кода).

Ядро LivePT предоставляет две изолированные подсистемы расширения возможностей: **коллбэки на двойной клик** (для открытия тяжелых графических окон) и **коллбэки покадрового перетаскивания мыши (Drag Callbacks)**.

#### 1. Регистрация контроллеров двойного клика (Type Callbacks)
Связывает текстовое имя типа со сторонним интерфейсом (ImGui, Win32, Qt) для вызова окна по двойному клику в редакторе кода:
```cpp
void MyCustomColorWindow(const std::any& anyValue, std::function<void(std::string)> vsUpdater);

// Регистрация в ядре:
LPT_REGISTER_TYPE("color3", Widgets::color3Callback);
```

#### 2. Регистрация покадровых обработчиков мыши (Drag Callbacks)
Позволяет перехватить удержание клавиши-триггера мыши на имени типа структуры или её открывающей скобке в Visual Studio. Каждая покадровая дельта перемещения физической мыши передается в ваш кастомный математический обработчик:

```cpp
// Сигнатура обработчика принимает текущую строку аргументов из VS и структуру смещения мыши
std::string MyCustomDragLogic(const std::string& currentArgsStr, const LivePT::DragMathInput& input) {
    // input.mouseFrameDeltaX — дельта смещения мыши по X за один кадр
    // input.mouseFrameDeltaY — инвертированная дельта смещения мыши по Y
    // input.ctrl, input.shift — флаги модификаторов скорости
    
    // Пример: парсим текущие аргументы, применяем дельту и возвращаем модифицированную строку кода:
    return "15.0f, 25.5f"; 
}

// Регистрация покадрового драга для конкретного типа:
LPT_REGISTER_DRAG("color3", DrugLogic::color3Drag);
LPT_REGISTER_DRAG("pos2", DrugLogic::pos2Drag);
```
При активации покадрового драга структуры ядро автоматически скрывает физический курсор ОС, фиксирует его в геометрическом центре окна манипуляции, разворачивает невидимый модальный щит Win32 для предотвращения потери фокуса ввода и выводит полупрозрачные текстовые подсказки (`g_OverlayText`) поверх экрана.

### 🛡️ Безопасность кода и транзакции изменений
* **Защитный барьер валидации (Shift Validation):** Перед смещением координат элементов в процессе редактирования, ядро производит предпроверку целевой строки. Если в силу ручной правки код деформировался и ожидаемый токен макроса не обнаружен по расчетным координаты, операция сдвига для этого элемента изолируется и мягко пропускается с выводом предупреждения в лог (`[LivePT Integrity Error]`), предотвращая порчу соседних участков кода.
* **Транзакционность (Undo/Redo):** Все изменения, вносимые мышиным драгом или графическими виджетами, оборачиваются в транзакции `UndoContext` среды Visual Studio. Каждая сессия изменения воспринимается IDE как единое атомарное действие — вы можете мгновенно отменить всю сессию правок стандартным сочетанием клавиш `Ctrl + Z`.

---

## 🇬🇧 Live Programming Toolkit (LivePT) — v0.5 [EN]

A lightweight, zero-runtime-overhead interactive development toolkit for **C++20 and Visual Studio**. It allows modifying constants, enumerations, and structural types directly in your source code at runtime without restarting the application.

### 📋 Compiler & IDE Requirements
Before integrating the library, ensure your project configurations meet the following prerequisites:

1. **C++ Language Standard:** `Project Properties` ➔ `C/C++` ➔ `C++ Language Standard` ➔ **`ISO C++20 Standard (/std:c++20)`** (or higher).
2. **Conforming Preprocessor:** `Project Properties` ➔ `C/C++` ➔ `Preprocessor` ➔ `Use Standard Conforming Preprocessor` ➔ **`Yes (/Zc:preprocessor)`**.
3. **Debug Symbols Generation:** Generation of **PDB files** must be explicitly enabled in your linker settings (crucial for the `DbgHelp` reflection subsystem).
4. **Mouse Automation Layout:** If using the middle mouse button (`VK_MBUTTON`) as your physical interface interaction button, open VS settings via `Tools` ➔ `Options` ➔ `Text Editor` ➔ `General` and **uncheck** the **"Middle click to scroll"** option to prevent document coordinate tracking collisions.

### 🚀 Quick Start Guide

1. Move the `LivePT` folder into your current C++ project directory.
2. At the absolute beginning of your primary source file (`main.cpp` / `WinMain`), include the toolkit's header prefixed with the compilation gate macro:
   ```cpp
   #define LivePT_EditMode true // true — activates the toolkit. false — completely strips out LivePT during compilation for release builds
   #include "LivePT/LivePT.h"
   ```
3. Insert the toolkit's background update pipeline dispatch method inside your main application or render loop:
   ```cpp
   while (true) {
       if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
           if (msg.message == WM_QUIT) break;
           TranslateMessage(&msg);
           DispatchMessage(&msg);
       } else {
           LivePT::ProcessEdit(); // Main trigger for on-the-fly text and frame evaluation
       }
   }
   ```

### ⚙️ Engine Inner Configuration (`LivePT.h`)
Primary user flags are configured via macro definitions directly inside the `LivePT.h` header file:

* `#define LivePT_Mouse true` — Enables interactive code editing mechanics (mouse dragging, enumeration context windows, floating help overlay drawing).
* `#define LivePT_WindowManagement true` — Toggles the `LptWindowManager` tracking. When running on a single monitor layout, it dynamically snaps your primary window and Visual Studio **50/50** across the screen workspace, restoring the IDE bounds flawlessly upon exit.
* `#define LivePT_AppToSecondaryDisplay false` — Automatically migrates and maximizes your execution window onto the secondary active monitor workspace.
* `#define LivePT_TriggerButton App VK_LBUTTON` — Specifies the virtual key bound to text interaction (`VK_LBUTTON` for standard left click, or `VK_MBUTTON` for middle click).

### 🎮 Interactive Infrastructure (Mouse & Keyboard)

#### 🖱 Mouse Controls
* **Smooth Value Dragging (Numeric Drag):** Hover the pointer over any numerical constant literal mapped inside an `eval()` macro, hold down your trigger button, and slide the mouse up or down. The text representation in the IDE and your runtime heap variable values scale instantly.
* **Tweak Speed Modifiers:** Hold down `Shift` while dragging to scale values 10x faster, or hold `Ctrl` to ramp values up or down 100x faster.
* **Instant Double-Click Boolean Toggle:** Perform a **double-click** action with your trigger button directly over a boolean literal like `eval(true)` or `eval(false)`. The token will instantly flip its state inside the active text document.
* **Dynamic Context Menus for Enums:** Double-click over any enumeration field token (e.g., `eval(ptype::box)`). The engine resolves the type namespace path, fetches all valid enum variants from the active PDB symbols database via `DbgHelp`, and displays a clean Win32 dark pop-up context menu at your cursor position.
* **Proportional Structural Transformation (Struct Drag):** Holding down your trigger button directly over a type name token or the opening brace of an expression like `eval(pos2{...})` locks the mouse into a proportional multiplier state. Dragging the mouse applies a matrix multiplier scaling all structural numeric properties simultaneously.

### 📦 Mapped Syntax Layout & `eval(...)` Constraints

#### ✅ Allowed Syntactic Formats inside `eval()`:

1. **Primitive Numerical Constant Literals:** Any integer or float representations, including standard negative integers and floating point precision suffixes.
2. **Enumeration Field Identifiers (both standard enums and scoped `enum class`).**
3. **Explicitly Named Structural Aggregates:** Passing structural definitions prefixed by their specific type name token pipes physical field offset states directly into runtime memory layouts.
4. **Deep Structural Nesting Multi-Tiers:** The `WriteStructBytes` subsystem recursively untangles complex aggregate layers automatically as long as the types are properly reflected in the symbols workspace.
5. **Dynamic Array Struct Containers (`std::vector<T>`) with Runtime Allocation Mapping:**
   LivePT v0.5 supports deep runtime analysis of dynamic list blocks. You can wrap standard vectors of structures into `eval(...)` arrays seamlessly:
   ```cpp
   auto points = eval(std::vector<HeavyPoint>{
       { true, pos2{-52.0f, 24.0f}, 5, 94, color3{239, 61, 125} },
       { true, pos2{-24.0f, 31.0f}, 2, 95, color3{95, 35, 59} }
   });
   ```
   * **On-the-fly In-Place Element Allocation/Removal:** The dedicated `MsvcVectorProxy` container engine monitors structural vector tokens inside the active document and maps them against runtime allocations. Typing a brand new array block inline or stripping a field out instantly prompts a `resize()` operation on the active application heap, adjusts the underlying `validFieldsMask` boundary arrays, and reallocates memory seamlessly without crashes or resource leaks.
6. **Hybrid Interlocking of Literals and Dynamic Runtime Variables:** You can mix raw local runtime tracking variables alongside fixed literals inside a structural `eval` declaration. The engine tokenizes and isolates dynamic layout signatures, bypassing them during layout changes while keeping your constants open to real-time mouse or keyboard tweaking.

#### ❌ Syntactic Violations & Constraints (What CANNOT be wrapped):
1. **Dynamic Equations Outside Structures:** Evaluating external formulas or complex run-time calculations inside the parent macro is invalid (e.g., `eval(a + b)`).
2. **Anonymous Unnamed List Aggregates:** Code structures initialized anonymously like `eval({10.0f, 20.0f})` missing an explicit type identifier token are **unsupported** (the layout parser cannot fetch offset constraints from your PDB without a valid text type mapping string).
3. **Complex Non-Trivial Containers Inside Struct Properties:** Fields inside structures evaluated by the macro block must consist of trivial types (`int`, `float`, `bool`, `char`, `double`). Packing dynamic standard containers like `std::string` or `std::vector` *inside* an `eval` structural layout block is strictly blocked to avoid выравнивание faults.
4. **Non-ASCII Text Formats:** The inner string parsing machine operates strictly inside the ASCII charset. Any Cyrillic character layout data or standard localization tags packed *inside* the active parsing boundaries of an `eval(...)` layout statement will cause a coordinate shift tracking failure.

### 🛠️ Extending Core Capabilities: Graphical Widgets & Frame Drag Callbacks

⚠️ **ARCHITECTURAL NOTE:** Embedded UI widgets (**ColorPicker**, **PosPicker**, and **AnglePicker**) **ARE NOT structural core dependencies hardcoded into LivePT**. They represent isolated **User Space** utility layers designed to demonstrate how cleanly your graphical layouts can interface with the core text pipeline.

The toolkit provides two distinct User Space callback pipelines: **Double-Click Type Callbacks** (for spawning separate overlay windows) and ** покадровые Drag Callbacks** (for deep mouse transformation hooks).

#### 1. Registering Double-Click Windows (Type Callbacks)
Maps an explicit text type identifier to external window tools (ImGui, Win32, Qt) when a double-click event is tracked inside your IDE workspace:
```cpp
void MyCustomColorWindow(const std::any& anyValue, std::function<void(std::string)> vsUpdater);

// Registering inside the engine's map:
LPT_REGISTER_TYPE("color3", Widgets::color3Callback);
```

#### 2. Mapped Per-Frame Mouse Drag Callbacks (Drag Callbacks)
Intercepts mouse tracking inputs whenever a trigger button session is held down over a structural type identifier token or its opening brace signature in Visual Studio. Every frame's physical displacement deltas are forwarded straight to your custom mathematical parser:

```cpp
// Callback receives the exact inner arguments string and a structural container holding mouse frame metrics
std::string MyCustomDragLogic(const std::string& currentArgsStr, const LivePT::DragMathInput& input) {
    // input.mouseFrameDeltaX — mouse X delta for the current frame loop
    // input.mouseFrameDeltaY — inverted mouse Y delta for the current frame loop
    // input.ctrl, input.shift — speed modifier toggles
    
    // Process input metrics and return a formatted, rewritten string block for your IDE:
    return "15.0f, 25.5f"; 
}

// Binding a custom frame drag handler to an explicit type key:
LPT_REGISTER_DRAG("color3", DrugLogic::color3Drag);
LPT_REGISTER_DRAG("pos2", DrugLogic::pos2Drag);
```
When a structural drag callback is fired, the engine smoothly suppresses the hardware OS cursor visibility, constraints mouse tracking coordinates to the center of your active layout space, deploys an invisible Win32 shield wrapper to block window defocus errors, and renders alpha-blended custom tooltip strings (`g_OverlayText`) directly on the screen space.

### 🛡️ Code Security & Transaction Isolation
* **Shift Validation Barrier:** Before shifting document tracking markers, the engine verifies the context data around the target row. If an accidental layout mutation deforms the text code blocks such that the target macro token shifts away from its expected location, the coordinate recalculation is cleanly isolated and skipped with an internal log warning (`[LivePT Integrity Error]`), protecting your codebase from layout corruptions.
* **Atomic Workspace Transactions (Undo/Redo):** Text modification streams pushed by mouse tracks or floating widgets are automatically grouped inside active Visual Studio `UndoContext` transaction boundaries. The entire layout stream session behaves as a single atomic operation—releasing the mouse slider lets you instantly revert all sequential file text updates using the standard IDE `Ctrl + Z` shortcut.