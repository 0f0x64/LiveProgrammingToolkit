# 🚀 Live Programming Toolkit (LivePT) — v0.5

* [Перейти к русской версии](#ru-live-programming-toolkit-livept--v05) | [Switch to English Version](#en-live-programming-toolkit-livept--v05)

---

## RU: Live Programming Toolkit (LivePT) — v0.5

Легковесный интерактивный инструмент разработки для **C++20 и Visual Studio** с нулевыми затратами ресурсов в релизной сборке. Позволяет изменять константы, перечисления и структуры в коде прямо во время работы приложения без перезапуска, а также управлять фокусом окон и поведением функций.

### 📋 Требования к среде и компилятору
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
* `#define LivePT_WindowManagement true` — Модуль `LptWindowManager`. Автоматически делит рабочую область экрана ровно **50/50** между окном вашего приложения и Visual Studio, а при закрытии — возвращает окно IDE в исходное состояние.
* `#define LivePT_AppToSecondaryDisplay false` — Автоматически переносит и разворачивает окно вашего приложения на весь экран второго дисплея.
* `#define LivePT_TriggerButton VK_LBUTTON` — Физическая клавиша мыши для захвата констант в коде (`VK_LBUTTON` — левая кнопка, `VK_MBUTTON` — колесико мыши).
* `#define LivePT_ShowEditPointsInLog true` — Активирует вывод в отладочный поток (`OutputDebugStringA`) полных путей к файлам и номеров строк для всех обнаруженных точек редактирования в момент первоначальной синхронизации диска. Полезно для верификации карты компиляции параметров.

### 🎮 Интерактивное управление (Мышь и Клавиатура)
* **Плавное изменение чисел (Numeric Drag):** Наведите курсор на числовой литерал внутри `eval()`, зажмите клавишу-триггер и перемещайте мышь вверх или вниз. Значение в коде Visual Studio и в памяти приложения начнет синхронно меняться.
* **Модификаторы скорости:** Удерживайте `Shift` во время драга для ускорения шага изменения в 10 раз, или `Ctrl` — для ускорения в 100 раз.
* **Мгновенная инверсия флагов по двойному клику:** Сделайте **двойной клик** кнопкой-триггером мыши по литералу `eval(true)` или `eval(false)`. Состояние логического флага мгновенно инвертируется в текстовом документе.
* **Интерактивное меню `enum`:** Сделайте двойной клик по элементу перечисления (например, `eval(ptype::box)`). Библиотека считает префикс типа, запросит у `DbgHelp` все доступные варианты этого `enum` и отобразит темное нативное контекстное меню Win32 для быстрого выбора.
* Кастомный драг структур (Struct Drag): Если вы зажмете мышь на имени типа или открывающей скобке выражения eval(pos2{...}), ядро передает текущую текстовую строку аргументов и дельту мыши в ваш зарегистрированный Drag-коллабэк. Логика полностью определяется пользователем: коллбэк может пропорционально масштабировать (умножать) все внутренние поля, изменять только определенные координаты или выполнять кастомную математику трансформации, возвращая готовую строку для записи в IDE.

### 📦 Поддерживаемый синтаксис и ограничения макроса `eval(...)`
* **Примитивные константные литералы:** Любые целые числа и числа с плавающей точкой, включая отрицательные значения и специфические суффиксы.
* **Элементы перечислений (как стандартных, так и `enum class`).**
* **Именованные агрегатные структуры (Явный тип):** При передаче структуры с указанием ее имени, ядро LivePT извлекает нативные смещения полей напрямую из Windows API (`TI_GET_OFFSET`) и точечно переносит измененные байты констант в рантайм-память поверх живого объекта.
* **Сложная вложенность структур:** Побайтовый движок `WriteStructBytes` рекурсивно раскладывает аргументы любой глубины вложенности, если их типы известны PDB.
* **Динамические векторы констант (`std::vector<T>`) с возможностью изменения размера:**
  LivePT v0.5 поддерживает глубокое отслеживание динамических массивов. Вы можете обернуть вектор структур целиком:
  ```cpp
  auto points = eval(std::vector<HeavyPoint>{
      { true, pos2{-52.0f, 24.0f}, 5, 94, color3{239, 61, 125} },
      { true, pos2{-24.0f, 31.0f}, 2, 95, color3{95, 35, 59} }
  });
  ```
    * **Добавление и удаление элементов:** Прокси-парсер автоматически сравнивает количество элементов в исходном коде Visual Studio с количеством элементов в runtime-памяти. Если вы вручную допишете новый элемент структуры через запятую в коде Visual Studio или удалите существующий, ядро LivePT автоматически выполнит операцию `resize()` для вектора в памяти запущенного приложения, полностью очистит старые смещения, соберет сквозную карту нативных PDB-смещений измененных байт и мгновенно перераспределит кучу без утечек памяти и падений.

### ⚡ Использование макроса `LPT_LIVE_FUNCTION` (Без `eval`)
Вместо оборачивания отдельных параметров через `eval(...)`, LivePT v0.5 позволяет переводить **функции целиком** в интерактивный режим. Это избавляет код от визуального шума макросов на уровне вызовов, позволяя оборачивать структуры и их аргументы одной декларацией.
```cpp
// Объявление «живой» функции с помощью макроса:
LPT_LIVE_FUNCTION(void, DrawTestRect, TestRect, in) {
    // Вся входящая структура 'in' (TestRect) становится полностью интерактивной.
    // Любые константные литералы, переданные на месте вызова функции, 
    // считываются и обновляются автоматически!
    int posX = (gc.w / 2) + in.pos.x;
    int posY = (gc.h / 2) + in.pos.y;
    // ... логика отрисовки ...
}

// Использование на месте вызова (чистый синтаксис C++ без eval):
DrawTestRect({
    .pos = pos2{ .x = 484.00f, .y = -62.13f },
    .size = size2{ 148.74f, 196.20f },
    .Angle = angle(47.0f),
    .color = color3{ 106, 19, 29 },
    .fill = true
});
```

### 🎯 Интеграция с IDE: Навигация и Анализ Вызовов
Ядро предоставляет встроенные методы для взаимодействия с редактором Visual Studio на основе информации о местоположении (`std::source_location`):

1. **`LivePT::OpenFileAndMoveCursorToLocation(location)`**
   Выполняет автоматическую фокусировку и перемещение каретки внутри IDE. При вызове (например, по интерактивному клику на объект в окне вашего приложения):
   * Находит HWND главного окна Visual Studio и перехватывает фокус ввода с помощью `SetForegroundWindow` и `AttachThreadInput`.
   * Через интерфейс `OpenFile` среды EnvDTE открывает нужный файл на экране.
   * Перемещает курсор на идеально сдвинутые с учетом рантайм-координат строку и колонку, где была объявлена или вызвана данная функция.
   
2. **`LivePT::IsCursorInsideFunctionCall(location)`**
   Позволяет функции узнать в рантайме, находится ли текстовый курсор пользователя в Visual Studio внутри её многострочного вызова.
   * Возвращает `true`, если текстовая каретка IDE установлена в диапазоне от имени функции до закрывающей точки с запятой `;` (или парных скобок).
   * **Применение:** Позволяет реализовать динамическую подсветку (Highlight) конкретного примитива на экране, который инженер редактирует в кодовой строке в данный момент.

### 🛠️ Создание кастомных контроллеров (User Space)
Ядро LivePT предоставляет две изолированные подсистемы расширения возможностей: **коллбэки на двойной клик** (для открытия тяжелых графических окон) и **коллбэки покадрового перетаскивания мыши (Drag Callbacks)**.

* **Регистрация контроллеров двойного клика (Type Callbacks):**
  ```cpp
  void MyCustomColorWindow(const std::any& anyValue, std::function<void(std::string)> vsUpdater);
  LPT_REGISTER_TYPE("color3", Widgets::color3Callback);
  ```
* **Регистрация покадровых обработчиков мыши (Drag Callbacks):**
  ```cpp
  std::string MyCustomDragLogic(const std::string& currentArgsStr, const LivePT::DragMathInput& input) {
      // Применяем смещение мыши из input к тексту аргументов
      return "15.0f, 25.5f"; 
  }
  LPT_REGISTER_DRAG("color3", DrugLogic::color3Drag);
  ```

---

## EN: Live Programming Toolkit (LivePT) — v0.5

A lightweight, zero-runtime-overhead interactive development tool for **C++20 and Visual Studio**. It allows modifying constants, enumerations, and structures directly within the source code at runtime without restarting, as well as managing window focus and function behavior.

### 📋 Environment & Compiler Requirements
1. **C++ Language Standard:** `Project Properties` ➔ `C/C++` ➔ `C++ Language Standard` ➔ **`ISO C++20 Standard (/std:c++20)`** (or higher).
2. **Conforming Preprocessor:** `Project Properties` ➔ `C/C++` ➔ `Preprocessor` ➔ `Use Standard Conforming Preprocessor` ➔ **`Yes (/Zc:preprocessor)`**.
3. **Debug Symbols:** Linker settings must have **PDB file generation** enabled (critical for the `DbgHelp` reflection subsystem).
4. **Visual Studio Mouse Settings:** If using the middle mouse button (`VK_MBUTTON`), go to `Tools` ➔ `Options` ➔ `Text Editor` ➔ `General` and **uncheck** **"Middle click to scroll"** to prevent auto-scroll conflicts.

### 🚀 Quick Start Installation Guide
1. Place the `LivePT` folder into your C++ project directory.
2. At the absolute top of your main source file (`main.cpp` / `WinMain`), include the library header strictly preceded by the activation flag:
   ```cpp
   #define LivePT_EditMode true // true — activates the toolkit. false — completely strips LivePT from release builds
   #include "LivePT/LivePT.h"
   ```
3. Inject the update dispatcher call into your main application/message loop:
   ```cpp
   while (true) {
       if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
           if (msg.message == WM_QUIT) break;
           TranslateMessage(&msg);
           DispatchMessage(&msg);
       } else {
           LivePT::ProcessEdit(); // Main trigger for real-time code analysis
       }
   }
   ```

### ⚙️ Global Configuration Settings (`LivePT.h`)
Adjust settings directly within the configuration block of the header file via `#define` statements:
* `#define LivePT_Mouse true` — Enables mouse value dragging, context menus for enums, and overlay windows.
* `#define LivePT_WindowManagement true` — Activates the `LptWindowManager` module. Automatically splits a single monitor layout **50/50** between the app window and Visual Studio, reverting to the original IDE state upon exit.
* `#define LivePT_AppToSecondaryDisplay false` — Automatically moves and maximizes the app window onto a secondary monitor if detected.
* `#define LivePT_TriggerButton VK_LBUTTON` — Mouse key mapping for code interactions (`VK_LBUTTON` for left click, `VK_MBUTTON` for wheel click).
* `#define LivePT_ShowEditPointsInLog true` — Logs full file paths and active line numbers to the debugger output (`OutputDebugStringA`) for all discovered edit points during initial disk synchronization. Extremely useful for validating the structural parameters compilation map.

### 🎮 Interactive Controls (Mouse & Keyboard)
* **Smooth Value Tweaking (Numeric Drag):** Hover over a literal inside `eval()`, hold the trigger key, and drag your mouse up/down. Visual Studio text and runtime memory sync instantly.
* **Speed Coefficients:** Hold `Shift` while dragging for a 10x multiplier, or `Ctrl` for a 100x speed increase.
* **Double-Click Boolean Inversion:** **Double-click** the trigger key over an `eval(true)` or `eval(false)` token to instantly flip its logical state directly inside the source text document.
* **Dynamic Enum Context Menu:** Double-click the trigger key over any enum variable (e.g., `eval(ptype::box)`). The engine queries `DbgHelp` for all matching enum constants and opens a native dark Win32 context menu right under your cursor.
* **Custom Struct Drag (Struct Drag): Holding the trigger button over a structure type identifier or its opening brace (e.g., eval(pos2{...})) dispatches the raw arguments string and mouse deltas directly to your custom registered Drag Callback. The transformation logic is completely user-defined: your callback can proportionally scale (multiply) all numerical members, modify specific coordinate axes, or apply custom math before returning the final formatted string back to the IDE automation layer.

### 📦 Supported Syntax & `eval(...)` Macro Constraints
* **Primitive Constant Literals:** Ints, floats, signed limits, and explicit type suffixes.
* **Enumeration Members (both standard and `enum class`).**
* **Explicitly Named Aggregates:** When a structure is passed with an explicit type, the LivePT core fetches native field offsets directly from the Windows API (`TI_GET_OFFSET`) and precisely applies the modified constant bytes over the live object in runtime memory.
* **Deep Nesting Support:** Structural byte shifting recursively unrolls nested aggregates of any depth, provided they are accessible via the generated PDB.
* **Resizable Dynamic Vectors (`std::vector<T>`):**
  LivePT v0.5 features specialized runtime parsing proxies for dynamic array allocations. You can safely wrap an entire structure vector block:
  ```cpp
  auto points = eval(std::vector<HeavyPoint>{
      { true, pos2{-52.0f, 24.0f}, 5, 94, color3{239, 61, 125} },
      { true, pos2{-24.0f, 31.0f}, 2, 95, color3{95, 35, 59} }
  });
  ```
    * **Element Insertion & Deletion:** The proxy parser monitors element counts within the source document against active memory arrays. Manually appending a new comma-separated struct item inside Visual Studio or deleting one causes LivePT to automatically trigger a safe `.resize()` on the live runtime vector, completely clear old entries, rebuild a precise end-to-end map of native PDB byte offsets, and reallocate the heap without memory leaks or crashes.

### ⚡ Exploiting the `LPT_LIVE_FUNCTION` Macro (Without `eval`)
Instead of wrapping individual parameters in repetitive `eval(...)` macro arguments, LivePT v0.5 introduces the ability to convert **entire functions** into interactive nodes. This approach eliminates visual clutter at call sites, declaring argument blocks interactive out-of-the-box.
```cpp
// Declaring a live function wrapper:
LPT_LIVE_FUNCTION(void, DrawTestRect, TestRect, in) {
    // The incoming 'in' structural data block (TestRect) becomes completely live.
    // Any constant literals specified at the call site are monitored 
    // and written back automatically!
    int posX = (gc.w / 2) + in.pos.x;
    int posY = (gc.h / 2) + in.pos.y;
    // ... drawing operations ...
}

// Clean syntax execution at the call site (no eval macro wrappers required):
DrawTestRect({
    .pos = pos2{ .x = 484.00f, .y = -62.13f },
    .size = size2{ 148.74f, 196.20f },
    .Angle = angle(47.0f),
    .color = color3{ 106, 19, 29 },
    .fill = true
});
```

### 🎯 IDE Automation: Focus & Real-Time Call Stack Highlighting
The core library exposes methods for manipulating Visual Studio's workspace state leveraging location footprints (`std::source_location`):

1. **`LivePT::OpenFileAndMoveCursorToLocation(location)`**
   Forces Visual Studio to focus a targeted file descriptor and scroll to a computed position. When called (e.g., clicking a primitive object inside your running application window):
   * Locates the main Visual Studio HWND and forces a focus context switch using `SetForegroundWindow` and `AttachThreadInput` APIs.
   * Dispatches the target document path onto the EnvDTE automation stack via `OpenFile`.
   * Automatically shifts the caret to the exact line and column offsets where that instance was compiled.
   
2. **`LivePT::IsCursorInsideFunctionCall(location)`**
   Evaluates whether a developer's active textual cursor in Visual Studio resides inside the multiline scope of a given function call block.
   * Returns `true` if the IDE text caret is positioned between the function's name identifier and its terminal semicolon `;` character.
   * **Use Case:** Enables dynamic context highlighting. You can conditionally draw visual helpers or bounding boxes around the exact element currently highlighted/edited by the programmer in the IDE.

### 🛠️ Extending Core Capabilities (User Space Drag Callbacks)
The LivePT backend exposes explicit layout hooks: **Double-Click Type Callbacks** and **Frame-by-Frame Drag Callbacks**.

* **Type-Based Double Click Subsystems:**
  ```cpp
  void MyCustomColorWindow(const std::any& anyValue, std::function<void(std::string)> vsUpdater);
  LPT_REGISTER_TYPE("color3", Widgets::color3Callback);
  ```
* **Real-Time Mouse Drag Callbacks:**
  ```cpp
  std::string MyCustomDragLogic(const std::string& currentArgsStr, const LivePT::DragMathInput& input) {
      // Offset values within currentArgsStr using raw input frame deltas
      return "15.0f, 25.5f"; 
  }
  LPT_REGISTER_DRAG("color3", DrugLogic::color3Drag);
  ```

---
