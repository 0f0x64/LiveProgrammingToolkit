<div align="center">

# 🚀 Live Programming Toolkit (LivePT)

A lightweight, zero-runtime-overhead interactive development tool for C++20 and Visual Studio.

<a href="#-russian-version">🇷🇺 Читать на русском</a> | <a href="#-english-version">🇺🇸 Read in English</a>

</div>

---

## 🇷🇺 Russian Version

### 📋 Требования к компилятору и IDE
Перед началом работы убедитесь, что ваш проект и среда Visual Studio настроены должным образом:

1. **Стандарт языка C++:** `Свойства проекта` ➔ `C/C++` ➔ `Язык C++` ➔ Установите **`ISO C++20 Standard (/std:c++20)`** (или выше).
2. **Новый препроцессор:** `Свойства проекта` ➔ `C/C++` ➔ `Препроцессор` ➔ `Использовать конформный препроцессор` ➔ Установите **`Да (/Zc:preprocessor)`**.
3. **Отладочные символы (PDB):** Убедитесь, что включена генерация PDB-файлов (требуется для автоматического анализа метаданных `enum` и структур через `DbgHelp`).
4. **Отключение автоскролла в VS:** Если вы используете среднюю кнопку мыши (`VK_MBUTTON`), в верхнем меню VS зайдите в `Сервис` ➔ `Параметры` ➔ `Текстовый редактор` ➔ `Общие` ➔ **Снимите галочку** с пункта **«Middle click to scroll»**.

---

### 🚀 Быстрый старт (Инструкция по установке)

1. **Скачайте папку `LivePT`** и положите её в директорию вашего проекта.
2. **Вставьте главный дефайн активации** строго **перед** инклудом библиотеки в вашем основном файле:
```cpp
#define LivePT_EditMode true // Единственный дефайн для включения системы (false полностью вырежет LivePT из релиза)
#include "LivePT/LivePT.h"
```
3. **Добавьте вызов функции обновления** библиотеки в ваш главный игровой или рендер-цикл (рекомендуется вызывать с частотой ~60 FPS / каждые 16 мс):
```cpp
// Внутри вашего главного цикла приложения (например, WinMain / message loop):
LivePT::ProcessEdit();
```

---

### ⚙️ Конфигурация внутри библиотеки (`LivePT.h`)
Все остальные внутренние настройки системы задаются непосредственно в заголовочном файле `LivePT.h` в блоке пользователя:
* `#define LivePT_WheelEditMode true` — Включает отслеживание мыши, изменение значений и вызов меню.
* `#define LivePT_WindowManagement true` — Автоматически делит экран primary-монитора 50/50 между окном приложения и Visual Studio.
* `#define LivePT_AppToSecondaryDisplay false` — Автоматически разворачивает окно приложения на весь экран второго монитора.
* `#define LivePT_TriggerButton VK_LBUTTON` — Клавиша-триггер для взаимодействия с кодом (`VK_LBUTTON` для левой кнопки мыши или `VK_MBUTTON` для колесика).

---

### 🎮 Интерактивное управление (Мышь и Клавиатура)
После запуска приложения вы можете настраивать параметры прямо внутри исходного кода в Visual Studio двумя способами:

#### 🖱 Управление мышью
* **Изменение чисел (Drag):** Наведите курсор на число внутри `eval()`, **зажмите кнопку-триггер** и тяните мышь вверх или вниз. Значение в коде и на экране начнет плавно меняться в реальном времени.
* **Модификаторы скорости:** Удерживайте `Shift` во время драга для изменения значений в 10 раз быстрее, или `Ctrl` — для ускорения в 100 раз.
* **Переключение `bool` по клику:** Кликните кнопкой-триггером по `eval(true)` или `eval(false)` для мгновенной инверсии флага.
* **Контекстное меню `enum`:** Сделайте двойной клик кнопкой-триггером по `eval(MyEnum::Value)`. Система вытащит элементы перечисления из PDB-файла и откроет нативное контекстное меню прямо под курсором.

#### ⌨️ Редактирование с клавиатуры
* **Прямой ввод:** Вы можете в любой момент сфокусироваться на коде в IDE и **вручную переписать число, изменить текст или имя элемента перечисления** прямо внутри скобок `eval(...)`.
* **Автоматическое чтение текста:** При перемещении курсора клавиатурой или вводе новых символов LivePT мгновенно перехватывает изменения строки через COM-интерфейсы `EnvDTE`, автоматически парсит новое строковое значение и сразу же применяет его к переменной в памяти запущенного приложения без перезапуска.

---

### 📦 Поддерживаемый синтаксис и ограничения макроса `eval(...)`

#### ✅ Что МОЖНО оборачивать в `eval()`:
1. **Базовые типы констант:** Любые числовые литералы (`int`, `float`), включая значения с суффиксами (например, `eval(35.6f)`, `eval(-47.01)`).
2. **Перечисления:** Строго типизированные и классические перечисления (например, `eval(Primitive::ptype::box)`).
3. **Агрегаты без указания имени типа (Безымянные):** Прямая инициализация структур списком значений:
   ```cpp
   // Поля заполняются по порядку: x, y, type, show, color
   primitive.Set(eval(-222), eval(-320), eval(Primitive::ptype::box), eval(Primitive::show_t::on), eval(Primitive::color3{ 47, 28, 45 }));
   ```
4. **Агрегаты с явным указанием имени типа (Именованные):** Передача структуры с явным вызовом конструктора или типа:
   ```cpp
   // Библиотека через DbgHelp найдет тип "Primitive" и сопоставит его внутреннюю анатомию
   primitive.Set(eval(Primitive{ -47.01f, -299, Primitive::ptype::box, Primitive::show_t::on, {120, 0, 0} }));
   ```
5. **Именованные агрегаты с C++20 Designated Initializers:** Инициализация структур с явным указанием полей. LivePT автоматически сопоставит смещения полей через `DbgHelp` и обновит нужные байты в памяти:
   ```cpp
   primitive.Set(Primitive{
       .x = eval(-47.01),
       .y = eval(-299),
       .type = eval(Primitive::ptype::box),
       .color = eval(Primitive::color3{ .r = 150, .g = 109, .b = 85 })
   });
   ```
6. **Частичные агрегаты и смешивание с переменными:** Вы можете оборачивать в `eval()` только конкретные константные поля структуры, оставляя остальные поля завязанными на динамические runtime-переменные:
   ```cpp
   // Поля .r и .b управляются динамически кодом (переменные x и y), а поле .g интерактивно меняется через eval()!
   primitive.Set(Primitive{
       .color = Primitive::color3{
           .r = x,
           .g = eval(109), 
           .b = y
       }
   });
   ```

#### ❌ Чего ДЕЛАТЬ НЕЛЬЗЯ:
1. **Динамические выражения:** Нельзя писать вычисления или вызовы функций, например `eval(a + b)` или `eval(GetX())`. Макрос ожидает фиксированный литерал или структуру, подлежащую текстовой перезаписи. Если нужно смешать константы и переменные внутри структуры — оборачивайте в `eval()` только константные поля по отдельности (как показано в пункте 6).
2. **Символы не-ASCII:** Текстовый парсер не поддерживает кодировки отличные от ASCII. Наличие кириллицы или спецсимволов внутри `eval()` вызовет ошибку парсинга.
3. **Сложные динамические типы внутри структур:** Поля структур, обернутых в `eval()`, должны состоять из примитивных типов (`char`, `int`, `float`, `double`, `bool`). Использование `std::string` или `std::vector` внутри таких структур не поддерживается.
---

## 🇺🇸 English Version

### 📋 Compiler & IDE Requirements
Before you begin, ensure your project and Visual Studio environment are properly configured:

1. **C++ Language Standard:** `Project Properties` ➔ `C/C++` ➔ `C++ Language Standard` ➔ Set to **`ISO C++20 Standard (/std:c++20)`** (or higher).
2. **Conforming Preprocessor:** `Project Properties` ➔ `C/C++` ➔ `Preprocessor` ➔ `Use Standard Conforming Preprocessor` ➔ Set to **`Yes (/Zc:preprocessor)`**.
3. **Debug Symbols (PDB):** Make sure PDB generation is enabled (required for runtime `enum` and `struct` metadata parsing via `DbgHelp`).
4. **Disable Auto-Scroll in VS:** If you plan to use the middle mouse button (`VK_MBUTTON`), navigate to `Tools` ➔ `Options` ➔ `Text Editor` ➔ `General` ➔ **Uncheck** the **"Middle click to scroll"** option.

---

### 🚀 Quick Start (Installation Guide)

1. **Download the `LivePT` folder** and place it into your project directory.
2. **Define the main activation macro** strictly **before** including the library header in your main source file:
```cpp
#define LivePT_EditMode true // The single macro to enable the system (false completely compiles LivePT out for release)
#include "LivePT/LivePT.h"
```
3. **Call the update function** inside your main application or render loop (ideally throttled, e.g., every 16ms to target ~60 FPS):
```cpp
// Inside your main application update/message loop:
LivePT::ProcessEdit();
```

---

### ⚙️ Inner Configuration (`LivePT.h`)
All other internal settings are configured directly inside the `LivePT.h` header file within the user settings block:
* `#define LivePT_WheelEditMode true` — Enables mouse tracking, real-time value tweaking, and context menus.
* `#define LivePT_WindowManagement true` — Automatically splits your primary monitor screen 50/50 between your application window and Visual Studio.
* `#define LivePT_AppToSecondaryDisplay false` — Automatically moves your application window to the secondary monitor in fullscreen mode.
* `#define LivePT_TriggerButton VK_LBUTTON` — The physical mouse key used to interact with code (`VK_LBUTTON` for left click or `VK_MBUTTON` for middle click).

---

### 🎮 Interactive Controls (Mouse & Keyboard)
Once your application is running, you can adjust parameters directly within your source code in Visual Studio in two ways:

#### 🖱 Mouse Controls
* **Smooth Value Dragging:** Hover your cursor over a number inside `eval()`, **hold your trigger button**, and drag your mouse up or down. The value in the IDE and your application will update simultaneously.
* **Speed Modifiers:** Hold `Shift` while dragging to change values 10x faster, or `Ctrl` to speed up by 100x.
* **Click to Toggle `bool`:** Click your trigger button over `eval(true)` or `eval(false)` to instantly invert the boolean flag.
* **Context Menus for `enums`:** Double-click your trigger button over any `eval(MyEnum::Value)`. The toolkit dynamically extracts enumeration variants from the PDB file and displays a native context menu right under your cursor.

#### ⌨️ Keyboard Editing
* **Direct Text Modification:** You can focus on the code editor at any time and **manually type a new number, modify text, or overwrite an enum member name** directly within the `eval(...)` parenthesis.
* **Automatic Text Reader:** As you move the text cursor or type new characters, LivePT intercepts document changes on the fly via `EnvDTE` COM automation, automatically parses the new inner string value, and applies it to the active variable in your running application immediately without restarting.

---

### 📦 Supported Syntax & `eval(...)` Macro Constraints

#### ✅ What CAN be wrapped inside `eval()`:
1. **Basic Constant Literals:** Any primitive numerical constants (`int`, `float`), including values with type suffixes (e.g., `eval(35.6f)`, `eval(-47.01)`).
2. **Enumerations:** Both scoped (`enum class`) and unscoped enums (e.g., `eval(Primitive::ptype::box)`).
3. **Unnamed Aggregates (List Initialization):** Direct structural initialization by passing an ordered list of values:
   ```cpp
   // Fields are mapped sequentially: x, y, type, show, color
   primitive.Set(eval(-222), eval(-320), eval(Primitive::ptype::box), eval(Primitive::show_t::on), eval(Primitive::color3{ 47, 28, 45 }));
   ```
4. **Named Aggregates (Explicit Type Names):** Passing a struct with an explicit constructor or type initialization:
   ```cpp
   // The toolkit looks up the "Primitive" type structure via DbgHelp and maps its internal anatomy layout
   primitive.Set(eval(Primitive{ -47.01f, -299, Primitive::ptype::box, Primitive::show_t::on, {120, 0, 0} }));
   ```
5. **Named Aggregates with C++20 Designated Initializers:** Struct initialization with explicit member naming. LivePT uses `DbgHelp` to map field offsets and updates raw bytes safely:
   ```cpp
   primitive.Set(Primitive{
       .x = eval(-47.01),
       .y = eval(-299),
       .type = eval(Primitive::ptype::box),
       .color = eval(Primitive::color3{ .r = 150, .g = 109, .b = 85 })
   });
   ```
6. **Partial Aggregates and Variable Mixing:** You can wrap only specific constant fields of a structure inside `eval()`, leaving other fields tied to dynamic runtime variables:
   ```cpp
   // The .r and .b fields are driven dynamically by runtime variables (x and y), while the .g field is interactively tweakable via eval()!
   primitive.Set(Primitive{
       .color = Primitive::color3{
           .r = x,
           .g = eval(109), 
           .b = y
       }
   });
   ```

#### ❌ What CANNOT be wrapped inside `eval()`:
1. **Dynamic Expressions:** You cannot pass runtime logic or function calls like `eval(a + b)` or `eval(GetX())`. The macro relies on fixed literals or structural blocks that can be structurally rewritten in the text file. If you need to mix constants and variables inside a struct, wrap only the constant fields individually (as shown in point 6).
2. **Non-ASCII Characters:** The internal text engine does not support non-ASCII encodings. Cyrillic characters or non-standard symbols inside `eval()` will cause parsing failures.
3. **Complex Non-Trivial Types in Structs:** Members of structs wrapped inside `eval()` must be primitive types (`char`, `int`, `float`, `double`, `bool`). Dynamic containers like `std::string` or `std::vector` are explicitly ignored to prevent memory corruption.

---
