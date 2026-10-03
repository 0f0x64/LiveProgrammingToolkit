#pragma once
#include <unordered_map>
#include <string>
#include <functional>
#include <any>
#include <typeinfo>

namespace LivePT {

    // Сигнатура коллбэка принимает std::any (строку аргументов) и vsUpdater
    using InternalDoubleClickCallback = std::function<void(const std::any&, std::function<void(std::string)>)>;

    // Глобальный реестр связывает ЧИСТОЕ ТЕКСТОВОЕ ИМЯ типа с функцией виджета
    inline std::unordered_map<std::string, InternalDoubleClickCallback>& GetTypeCallbackRegistry() {
        static std::unordered_map<std::string, InternalDoubleClickCallback> instance;
        return instance;
    }

    // Прямая регистрация коллбэка по текстовому ключу
    inline void RegisterTypeDoubleClickCallback(const std::string& typeName, InternalDoubleClickCallback userCallback) {
        GetTypeCallbackRegistry()[typeName] = userCallback;
    }

}

// Вспомогательные макросы для склейки имени с номером строки (защита от двоеточий ::)
#define LPT_CONCAT_INNER(a, b) a##b
#define LPT_CONCAT(a, b) LPT_CONCAT_INNER(a, b)

// ОБНОВЛЕННЫЙ ТЕКСТОВЫЙ МАКРОС: Исключает синтаксический сбой двоеточий.
// Принимает строковый ключ TypeNameString и саму CallbackFunction.
#define LPT_REGISTER_TYPE(TypeNameString, CallbackFunction) \
    static inline struct { \
        bool initialized = []() { \
            LivePT::RegisterTypeDoubleClickCallback(TypeNameString, CallbackFunction); \
            return true; \
        }(); \
    } LPT_CONCAT(_lpt_instance_, __LINE__);


//drag
namespace LivePT {

    struct DragMathInput {
        int mouseFrameDeltaX; // Смещение мыши по X за один кадр
        int mouseFrameDeltaY; // Смещение мыши по Y за один кадр (инвертированное)
        bool ctrl;            // Зажат ли Ctrl
        bool shift;           // Зажат ли Shift
    };

    // Сигнатура колбека: принимает ссылку на текущий массив float и структуру дельт
    using DragMathCallback = std::function<void(std::vector<float>& values, const DragMathInput& input)>;

    // Глобальный реестр для связи текстового имени типа с лямбдой математики
    inline std::unordered_map<std::string, DragMathCallback>& GetCustomDragRegistry() {
        static std::unordered_map<std::string, DragMathCallback> registry;
        return registry;
    }

    template <typename T>
    inline void RegisterTypeDragCallback(DragMathCallback userCallback) {
        std::string typeName = typeid(T).name();

        // Автоматически чистим имя типа от "struct " и "class " прямо при регистрации
        if (typeName.rfind("struct ", 0) == 0) typeName = typeName.substr(7);
        if (typeName.rfind("class ", 0) == 0)  typeName = typeName.substr(6);

        GetCustomDragRegistry()[typeName] = userCallback;
    }

#define LPT_REGISTER_DRAG(Type, Callback) \
    static inline struct { \
        bool initialized = []() { \
            LivePT::RegisterTypeDragCallback<Type>(Callback<Type>); \
            return true; \
        }(); \
    } _lpt_drag_instance_##Type;

}
