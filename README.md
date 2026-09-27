<div align="center">

# 🚀 Live Programming Toolkit (LivePT) — v0.4

Легковесный интерактивный инструмент разработки для C++20 и Visual Studio с нулевыми затратами ресурсов в runtime.

</div>

---

## 📋 Требования к компилятору и IDE
Перед началом работы убедитесь, что ваш проект и среда Visual Studio настроены должным образом:

1. **Стандарт языка C++:** `Свойства проекта` ➔ `C/C++` ➔ `Язык C++` ➔ Установите **`ISO C++20 Standard (/std:c++20)`** (или выше).
2. **Новый препроцессор:** `Свойства проекта` ➔ `C/C++` ➔ `Препроцессор` ➔ `Использовать конформный препроцессор` ➔ Установите **`Да (/Zc:preprocessor)`**.
3. **Отладочные символы (PDB):** Убедитесь, что включена генерация PDB-файлов (требуется для автоматического анализа метаданных `enum` и структур через системные вызовы `DbgHelp`).
4. **Отключение автоскролла в VS:** Если вы используете среднюю кнопку мыши (`VK_MBUTTON`), в верхнем меню VS зайдите в `Сервис` ➔ `Параметры` ➔ `Текстовый редактор` ➔ `Общие` ➔ **Снимите галочку** с пункта **«Middle click to scroll»**.

---

## 🚀 Быстрый старт (Инструкция по установке)

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

## ⚙️ Конфигурация внутри библиотеки (`LivePT.h`)
Все остальные внутренние настройки системы задаются непосредственно в заголовочном файле `LivePT.h` в блоке пользователя:
* `#define LivePT_WheelEditMode true` — Включает отслеживание мыши, изменение значений и вызов меню.
* `#define LivePT_WindowManagement true` — Автоматически делит экран primary-монитора 50/50 между окном приложения и Visual Studio.
* `#define LivePT_AppToSecondaryDisplay false` — Автоматически разворачивает окно приложения на весь экран второго монитора.
* `#define LivePT_TriggerButton VK_LBUTTON` — Клавиша-триггер для взаимодействия с кодом (`VK_LBUTTON` для левой кнопки мыши или `VK_MBUTTON` для колесика).

---

## 🎮 Интерактивное управление (Мышь и Клавиатура)
После запуска приложения вы можете настраивать параметры прямо внутри исходного кода в Visual Studio двумя способами:

### 🖱 Управление мышью
* **Изменение чисел (Drag):** Наведите курсор на число внутри `eval()`, **зажмите кнопку-триггер** и тяните мышь вверх или вниз. Значение в коде и на экране начнет плавно меняться в реальном времени.
* **Модификаторы скорости:** Удерживайте `Shift` во время драга для изменения значений в 10 раз быстрее, или `Ctrl` — для ускорения в 100 раз.
* **Переключение `bool` по клику:** Кликните кнопкой-триггером по `eval(true)` или `eval(false)` для мгновенной инверсии флага.
* **Контекстное меню `enum`:** Сделайте двойной клик кнопкой-триггером по `eval(MyEnum::Value)`. Система вытащит элементы перечисления из PDB-файла и откроет нативное контекстное меню прямо под курсором.
* **Пропорциональное изменение структур (Struct Drag):** Зажмите кнопку-триггер на *имени типа или открывающей скобке* внутри `eval(MyStruct{...})`. Перемещение мыши будет пропорционально масштабировать (умножать) все числовые поля структуры одновременно. Если поле равно нулю, оно начнет увеличиваться линейно.

### ⌨️ Редактирование с клавиатуры
* **Прямой ввод:** Вы можете в любой момент сфокусироваться на коде в IDE и **вручную переписать число, изменить текст или имя элемента перечисления** прямо внутри скобок `eval(...)`.
* **Автоматическое чтение текста:** При перемещении курсора клавиатурой или вводе новых символов LivePT мгновенно перехватывает изменения строки через COM-интерфейсы `EnvDTE`, автоматически парсит новое строковое значение и сразу же применяет его к переменной в памяти запущенного приложения без перезапуска.

---

## 🛠️ Расширение возможностями ядра: Кастомные графические контроллеры (User Space)

⚠️ **ВАЖНОЕ ЗАМЕЧАНИЕ:** Идущие в комплекте поставки графические контроллеры (такие как **ColorPicker**, **PosController/Trackpad** или **AngleRadar**) **НЕ ЯВЛЯЮТСЯ частью неизменяемого ядра библиотеки LivePT**. Это демонстрационные примеры из области **User Space**. Они созданы исключительно для демонстрации того, как легко связать внешние интерфейсы с движком LivePT.

Ядро LivePT предоставляет универсальный механизм обратного вызова для двойного клика (`Double-Click Callback`). Вы можете зарегистрировать абсолютно любое своё UI-окно (на базе ImGui, Win32 API, Qt), используя макрос авторегистрации типов:

```cpp
// Пример связывания вашей структуры с кастомным интерфейсом в User Space:
struct my_color { unsigned char r, g, b; };

// Функция-обработчик, которая откроет ваше окно при двойном клике в VS:
void MyCustomColorWindow(const my_color& initialValue, std::function<void(std::string)> vsUpdater) {
    // 1. Отображаете ваше UI окно
    // 2. При изменении ползунков в UI вызываете: vsUpdater("my_color{255, 0, 0}");
    // 3. Библиотека сама запишет этот текст прямо в активный файл Visual Studio!
}

// Регистрируем обработчик в системе:
LPT_REGISTER_TYPE(my_color, MyCustomColorWindow);
```

---

## 📦 Поддерживаемый синтаксис и ограничения макроса `eval(...)`

### ✅ Что МОЖНО оборачивать в `eval()`:
1. **Базовые типы констант:** Любые числовые литералы (`int`, `float`), включая значения с суффиксами (например, `eval(35.6f)`, `eval(-47.01)`).
2. **Перечисления:** Строго типизированные и классические перечисления (например, `eval(ptype::box)`).
3. **Именованные агрегаты (Явное указание имени типа):** Передача структуры с явным вызовом конструктора или типа. Система через `DbgHelp` найдет тип и сопоставит его внутреннюю анатомию:
   ```cpp
   primitive.Set(eval(Primitive{ -47.01f, -299, ptype::box, show_t::on, {120, 0, 0} }));
   ```
4. **Именованные агрегаты с C++20 Designated Initializers:** Инициализация структур с явным указанием полей. LivePT автоматически сопоставит смещения полей через PDB и обновит нужные байты в памяти с высокой производительностью за счет `std::from_chars`:
   ```cpp
   primitive.Set(Primitive{
       .x = eval(-47.01),
       .y = eval(-299),
       .type = eval(ptype::box),
       .color = eval(color3{ .r = 150, .g = 109, .b = 85 })
   });
   ```
5. **Смешивание литералов и runtime-переменных внутри агрегата:** При инициализации именованного агрегата вы можете свободно передавать внутрь `eval(...)` как фиксированные константы, так и динамические переменные (например, `x` и `y`). Парсер библиотеки автоматически определит текстовые токены переменных и пропустит их при интерактивном обновлении, изменяя исключительно литералы:
   ```cpp
   primitive.Set(Primitive{
       .x = eval(26.40),
       .y = eval(-276),
       .type = eval(ptype::roundbox),
       .show = eval(show_t::on),
       .color = eval(color3{
           .r = x,
           .g = 62, // Поле .g (литерал 62) можно интерактивно крутить мышкой/клавиатурой!
           .b = y
       })
   });
   ```

### ❌ Чего ДЕЛАТЬ НЕЛЬЗЯ:
1. **Динамические выражения вне структуры:** Нельзя писать изолированные вычисления или вызовы функций, например `eval(a + b)` или `eval(GetX())`. Макрос ожидает константный литерал или агрегатную структуру.
2. **Агрегаты без указания имени типа (Безымянные):** Прямая инициализация структур списком значений вида `{ val1, val2 }` без явного имени типа **не поддерживается**, так как парсер не сможет извлечь анатомию структуры из PDB без текстового идентификатора типа.
3. **Символы не-ASCII:** Текстовый парсер не поддерживает кодировки отличные от ASCII. Наличие кириллицы или спецсимволов внутри `eval()` вызовет ошибку парсинга.
4. **Сложные динамические типы внутри структур:** Поля структур, обернутых в `eval()`, должны состоять из примитивных типов (`char`, `int`, `float`, `double`, `bool`). Использование `std::string` или `std::vector` внутри таких структур не поддерживается во избежание повреждения памяти.
<div align="center">

# 🚀 Live Programming Toolkit (LivePT) — v0.4

A lightweight, zero-runtime-overhead interactive development tool for C++20 and Visual Studio.

</div>

---

## 📋 Compiler & IDE Requirements
Before you begin, ensure your project and Visual Studio environment are properly configured:

1. **C++ Language Standard:** `Project Properties` ➔ `C/C++` ➔ `C++ Language Standard` ➔ Set to **`ISO C++20 Standard (/std:c++20)`** (or higher).
2. **Conforming Preprocessor:** `Project Properties` ➔ `C/C++` ➔ `Preprocessor` ➔ `Use Standard Conforming Preprocessor` ➔ Set to **`Yes (/Zc:preprocessor)`**.
3. **Debug Symbols (PDB):** Make sure PDB generation is enabled (required for runtime `enum` and `struct` metadata parsing via `DbgHelp` API).
4. **Disable Auto-Scroll in VS:** If you plan to use the middle mouse button (`VK_MBUTTON`), navigate to `Tools` ➔ `Options` ➔ `Text Editor` ➔ `General` ➔ **Uncheck** the **"Middle click to scroll"** option.

---

## 🚀 Quick Start (Installation Guide)

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

## ⚙️ Inner Configuration (`LivePT.h`)
All other internal settings are configured directly inside the `LivePT.h` header file within the user settings block:
* `#define LivePT_WheelEditMode true` — Enables mouse tracking, real-time value tweaking, and context menus.
* `#define LivePT_WindowManagement true` — Automatically splits your primary monitor screen 50/50 between your application window and Visual Studio.
* `#define LivePT_AppToSecondaryDisplay false` — Automatically moves your application window to the secondary monitor in fullscreen mode.
* `#define LivePT_TriggerButton VK_LBUTTON` — The physical mouse key used to interact with code (`VK_LBUTTON` for left click or `VK_MBUTTON` for middle click).

---

## 🎮 Interactive Controls (Mouse & Keyboard)
Once your application is running, you can adjust parameters directly within your source code in Visual Studio in two ways:

### 🖱 Mouse Controls
* **Smooth Value Dragging:** Hover your cursor over a number inside `eval()`, **hold your trigger button**, and drag your mouse up or down. The value in the IDE and your application will update simultaneously.
* **Speed Modifiers:** Hold `Shift` while dragging to change values 10x faster, or `Ctrl` to speed up by 100x.
* **Click to Toggle `bool`:** Click your trigger button over `eval(true)` or `eval(false)` to instantly invert the boolean flag.
* **Context Menus for `enums`:** Double-click your trigger button over any `eval(MyEnum::Value)`. The toolkit dynamically extracts enumeration variants from the PDB file and displays a native context menu right under your cursor.
* **Proportional Struct Dragging:** Hold your trigger button over the *type name or opening brace* inside `eval(MyStruct{...})`. Dragging the mouse will proportionally scale (multiply) all numerical fields of the structure at once. If a field is zero, it scales up linearly.

### ⌨️ Real-Time Keyboard Editing
* **Direct Text Modification:** You can focus on the code editor at any time and **manually type a new number, modify text, or overwrite an enum member name** directly within the `eval(...)` parenthesis.
* **Automatic Text Reader:** As you move the text cursor or type new characters, LivePT intercepts document changes on the fly via `EnvDTE` COM automation, automatically parses the new inner string value, and applies it to the active variable in your running application immediately without restarting.

---

## 🛠️ Extending Core Capabilities: Custom Graphical Controllers (User Space)

⚠️ **CRITICAL NOTE:** The bundled visual controllers (such as **ColorPicker**, **PosController/Trackpad**, or **AngleRadar**) **ARE NOT a hardcoded part of the core LivePT library backend**. They represent **User Space** reference examples provided exclusively to demonstrate how cleanly you can bridge your own custom visual widgets with the underlying text manipulation engine.

The LivePT core engine exposes a flexible `Double-Click Callback` architecture. You can register any custom graphical interface (built with ImGui, Win32 API, Qt, etc.) using a straightforward type registration macro:

```cpp
// Example of binding your own custom type to a User Space interface:
struct my_color { unsigned char r, g, b; };

// The callback function that triggers your UI upon double-clicking inside VS:
void MyCustomColorWindow(const my_color& initialValue, std::function<void(std::string)> vsUpdater) {
    // 1. Draw your custom UI window here
    // 2. When UI sliders move, simply execute: vsUpdater("my_color{255, 0, 0}");
    // 3. The LivePT core will instantly commit that string to the active Visual Studio document!
}

// Register your type-to-UI bridge globally:
LPT_REGISTER_TYPE(my_color, MyCustomColorWindow);
```

---

## 📦 Supported Syntax & `eval(...)` Macro Constraints

### ✅ What CAN be wrapped inside `eval()`:
1. **Basic Constant Literals:** Any primitive numerical constants (`int`, `float`), including values with type suffixes (e.g., `eval(35.6f)`, `eval(-47.01)`).
2. **Enumerations:** Both scoped (`enum class`) and unscoped enums (e.g., `eval(ptype::box)`).
3. **Named Aggregates (Explicit Type Names):** Passing a struct with an explicit constructor or type initialization. The core lookups the type structure layout via `DbgHelp` and maps its internal layout:
   ```cpp
   blueprint.Set(eval(Primitive{ -47.01f, -299, ptype::box, show_t::on, {120, 0, 0} }));
   ```
4. **Named Aggregates with C++20 Designated Initializers:** Struct initialization with explicit member naming. LivePT uses `DbgHelp` to map field offsets and highly efficiently parses values using non-throwing `std::from_chars`:
   ```cpp
   primitive.Set(Primitive{
       .x = eval(-47.01),
       .y = eval(-299),
       .type = eval(ptype::box),
       .color = eval(color3{ .r = 150, .g = 109, .b = 85 })
   });
   ```
5. **Mixing Literals and Runtime Variables inside Aggregates:** When initializing a named aggregate, you can pass both fixed constants and dynamic variables (e.g., `x` and `y`) inside the `eval(...)` macro block. The toolkit's parser automatically identifies variable text tokens and skips them during update cycles, modifying only literal constants:
   ```cpp
   primitive.Set(Primitive{
       .x = eval(26.40),
       .y = eval(-276),
       .type = eval(ptype::roundbox),
       .show = eval(show_t::on),
       .color = eval(color3{
           .r = x,
           .g = 62, // The .g field (literal 62) remains interactively tweakable via mouse/keyboard!
           .b = y
       })
   });
   ```

### ❌ What CANNOT be wrapped inside `eval()`:
1. **Dynamic Expressions Outside Structs:** You cannot pass independent calculations or function calls like `eval(a + b)` or `eval(GetX())`. The macro expects a fixed literal or a structural aggregate block.
2. **Unnamed Aggregates (Raw List Initializations):** Initializing structures anonymously using `{ val1, val2 }` lists without an explicit type name **is not supported**, as the parser cannot fetch the structure layout from the PDB without a text type identifier.
3. **Non-ASCII Characters:** The internal text engine does not support non-ASCII encodings. Cyrillic characters or non-standard symbols inside `eval()` will cause parsing failures.
4. **Complex Non-Trivial Types in Structs:** Members of structs wrapped inside `eval()` must be primitive types (`char`, `int`, `float`, `double`, `bool`). Dynamic containers like `std::string` or `std::vector` are explicitly ignored to prevent memory corruption.
