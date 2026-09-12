<div align="center">

# 🚀 Live Programming Toolkit (LivePT)

A lightweight, zero-runtime-overhead interactive development tool for C++20 and Visual Studio.

<a href="#-version">🇷🇺 Читать на русском</a> | <a href="#-english-version">🇺🇸 Read in English</a>

</div>

---

## 🇷🇺 Version

### 🚀 Быстрый старт (Инструкция по установке)

Вы можете интегрировать библиотеку в свой проект за **5 простых шагов**:

1. **Скачайте папку `LivePT`** и положите её в директорию вашего проекта (рядом с исходным кодом).
2. **Взведите два дефайна** конфигурации в самом верху вашего главного файла:
```cpp
#define LivePT_EditMode true       // true — активирует режим live-редактирования
#define LivePT_WheelEditMode true  // true — включает управление мышью и контекстные меню
```
3. **Подключите инклуд** библиотеки:
```cpp
#include "LivePT/LivePT.h"
```
4. **Оберните число или параметр в макрос `eval()`** прямо внутри вашего цикла обновления сцены или рендера:
```cpp
primitive.Set(eval(-79), eval(-114), eval(Primitive::ptype::roundbox), eval(true));
```
5. **Добавьте вызов функции обновления** библиотеки в ваш главный игровой/рендер цикл (желательно с ограничением по FPS, например, каждые 16 мс):
```cpp
// Внутри вашего главного цикла приложения (например, WinMain / main loop):
LivePT::ProcessEdit();
```

### 🎮 Как это работает в реальности (Управление мышью)

После запуска программы вам больше не нужно возвращаться в редактор, переписывать цифры руками и заново нажимать "Компилировать". LivePT делает это за вас через автоматизацию Visual Studio:

* **Плавное изменение чисел (Drag):** Наведите курсор на число внутри `eval()` прямо в коде Visual Studio, **зажмите колесико мыши (MButton)** и тяните мышь вверх или вниз. Число в коде начнет интерактивно меняться, а ваше приложение мгновенно отобразит изменения в реальном времени.
* **Быстрые модификаторы:** Зажмите `Shift` во время перетаскивания, чтобы менять значения в 10 раз быстрее, или `Ctrl` — для ускорения в 100 раз.
* **Переключение `bool` по клику:** Просто **кликните колесиком мыши** по макросу `eval(true)` или `eval(false)` — текст в коде IDE сам поменяется на противоположный, а объект на экране скроется или появится.
* **Контекстное меню для `enum`:** Кликните колесиком по любому `eval(MyEnum::Value)`. Библиотека мгновенно считает все элементы перечисления и откроет удобное контекстное меню прямо у курсора. Выберите нужный пункт — и LivePT сам перепишет название элемента в исходном коде Visual Studio.

### 📦 Поддерживаемые типы данных и их расширение

Из коробки макрос `eval()` поддерживает типы: `int`, `float`, `bool`, `enum / enum class`.

#### Как добавить новый тип (например, `double`):
Благодаря использованию `std::variant` и шаблонов, вам **не нужно** писать свитчи, касты или условия парсинга. Достаточно добавить новый тип в одном месте в файле **`eval.h`**:

Найдите структуру `ref` и просто допишите ваш тип в список `using typeVariant`:
```cpp
struct ref {
    // Просто допишите сюда double через запятую:
    using typeVariant = std::variant<int, float, bool, double>; 
    typeVariant value;
    // ... остальной код структуры оставляем без изменений
};
```
Всё остальное скастится автоматически за счет шаблонного движка библиотеки.

---

## 🇺🇸 English Version

### 🚀 Quick Start (Installation Guide)

You can integrate the library into your project in just **5 simple steps**:

1. **Download the `LivePT` folder** and place it into your project directory (next to your source code).
2. **Define two configuration macros** at the very top of your main file:
```cpp
#define LivePT_EditMode true       // true — enables the live editing infrastructure
#define LivePT_WheelEditMode true  // true — enables mouse dragging and context menus
```
3. **Include the header** file:
```cpp
#include "LivePT/LivePT.h"
```
4. **Wrap any number or parameter with the `eval()` macro** inside your update or render loop:
```cpp
primitive.Set(eval(-79), eval(-114), eval(Primitive::ptype::roundbox), eval(true));
```
5. **Call the update function** inside your main application/render loop (ideally throttled, e.g., every 16ms):
```cpp
// Inside your main application update/message loop:
LivePT::ProcessEdit();
```

### 🎮 Live Mouse Tweak & Text Editing

Once your app is running, you no longer need to stop it, rewrite constants, and hit "Recompile". LivePT interacts with Visual Studio dynamically under the hood:

* **Smooth Number Dragging:** Hover your cursor over a number inside `eval()` right in your Visual Studio editor, **hold the middle mouse button (MButton)**, and drag your mouse up or down. The number in your source code will change fluidly, and the running app will update instantly.
* **Speed Modifiers:** Hold `Shift` while dragging to change values 10x faster, or `Ctrl` to speed up by 100x.
* **Click to Toggle `bool`:** Simply **click the middle mouse button** over `eval(true)` or `eval(false)`. The text in the IDE source code will automatically invert, instantly affecting your application logic.
* **Context Menus for `enums`:** Click MButton over any `eval(MyEnum::Value)`. The toolkit parses the enumeration at compile-time and opens a native context menu right under your cursor. Pick an item, and LivePT rewrites the token inside your Visual Studio document automatically.

### 📦 Supported Data Types & Extension

Out of the box, the `eval()` macro supports: `int`, `float`, `bool`, `enum / enum class`.

#### How to add a new type (e.g., `double`):
Thanks to `std::visit` and compile-time templates, you do not need to write custom switches, type-casts, or manual string parsers. You only need to add your type in a single place inside **`eval.h`**:

Locate the `ref` struct definition and append your type to the `using typeVariant` list:
```cpp
struct ref {
    // Just append double here:
    using typeVariant = std::variant<int, float, bool, double>; 
    typeVariant value;
    // ... leave the rest of the struct code as is
};
```
Everything else will be generated and automatically casted by the template engine under the hood.
