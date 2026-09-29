#pragma once
#include <unordered_map>
#include <string>
#include <functional>
#include <any>
#include <typeinfo>

namespace LivePT {

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
