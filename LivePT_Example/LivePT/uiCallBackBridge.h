#pragma once
#include <unordered_map>
#include <string>
#include <functional>
#include <any>
#include <typeinfo>

namespace LivePT {

    static inline bool isAnyWidgetTracking = false;

    // Callback принимает std::any, чтобы код ядра не зависел от конкретных типов данных
    using InternalDoubleClickCallback = std::function<void(const std::any&, std::function<void(std::string)>)>;

    inline std::unordered_map<std::string, InternalDoubleClickCallback>& GetTypeCallbackRegistry() {
        static std::unordered_map<std::string, InternalDoubleClickCallback> instance;
        return instance;
    }

    template <typename T>
    inline void RegisterTypeDoubleClickCallback(std::function<void(const std::any&, std::function<void(std::string)>)> userCallback) {
        std::string typeName = typeid(T).name();
        GetTypeCallbackRegistry()[typeName] = userCallback;
    }

}

// Абсолютно уникальный макрос: ноль глобальных имен, инициализация через анонимную структуру
#define LPT_REGISTER_TYPE(Type, Callback) \
    static inline struct { \
        bool initialized = []() { \
            LivePT::RegisterTypeDoubleClickCallback<Type>(Callback<Type>); \
            return true; \
        }(); \
    } _lpt_instance_##Type;


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
