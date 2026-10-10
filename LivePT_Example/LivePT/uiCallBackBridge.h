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

    using CustomTypeDragCallback = std::string(*)(const std::string& currentArgsStr, const LivePT::DragMathInput& input);

    // Global registry matching the type string to the function pointer
    inline std::unordered_map<std::string, CustomTypeDragCallback>& GetCustomDragRegistry() {
        static std::unordered_map<std::string, CustomTypeDragCallback> instance;
        return instance;
    }

    // Explicit registration function
    inline bool RegisterTypeDragCallback(const std::string& typeName, CustomTypeDragCallback callback) {
        if (callback) {
            GetCustomDragRegistry()[typeName] = callback;
            return true;
        }
        return false;
    }

} // namespace LivePT

// =========================================================================
// IDENTICAL STRUCT-BASED MACRO MIRROR (YOUR EXACT ARCHITECTURE)
// =========================================================================

#define LPT_REGISTER_DRAG(TypeNameString, CallbackFunction) \
    static inline struct { \
        bool initialized = []() { \
            LivePT::RegisterTypeDragCallback(TypeNameString, CallbackFunction); \
            return true; \
        }(); \
    } LPT_CONCAT(_lpt_drag_instance_, __LINE__);

