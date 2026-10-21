#pragma once
#include <unordered_map>
#include <string>
#include <functional>
#include <any>
#include <typeinfo>

namespace LivePT {

    using InternalDoubleClickCallback = std::function<void(const std::any&, std::function<void(std::string)>)>;

    inline std::unordered_map<std::string, InternalDoubleClickCallback>& GetTypeCallbackRegistry() {
        static std::unordered_map<std::string, InternalDoubleClickCallback> instance;
        return instance;
    }

    inline void RegisterTypeDoubleClickCallback(const std::string& typeName, InternalDoubleClickCallback userCallback) {
        GetTypeCallbackRegistry()[typeName] = userCallback;
    }

}

#define LPT_CONCAT_INNER(a, b) a##b
#define LPT_CONCAT(a, b) LPT_CONCAT_INNER(a, b)

#define LPT_REGISTER_TYPE(TypeNameString, CallbackFunction) \
    static inline struct { \
        bool initialized = []() { \
            LivePT::RegisterTypeDoubleClickCallback(TypeNameString, CallbackFunction); \
            return true; \
        }(); \
    } LPT_CONCAT(_lpt_instance_, __LINE__);


namespace LivePT {

    struct DragMathInput {
        int mouseFrameDeltaX; 
        int mouseFrameDeltaY; 
        bool ctrl;            
        bool shift;           
    };

    using CustomTypeDragCallback = std::string(*)(const std::string& currentArgsStr, const LivePT::DragMathInput& input);

    inline std::unordered_map<std::string, CustomTypeDragCallback>& GetCustomDragRegistry() {
        static std::unordered_map<std::string, CustomTypeDragCallback> instance;
        return instance;
    }

    inline bool RegisterTypeDragCallback(const std::string& typeName, CustomTypeDragCallback callback) {
        if (callback) {
            GetCustomDragRegistry()[typeName] = callback;
            return true;
        }
        return false;
    }

} 

#define LPT_REGISTER_DRAG(TypeNameString, CallbackFunction) \
    static inline struct { \
        bool initialized = []() { \
            LivePT::RegisterTypeDragCallback(TypeNameString, CallbackFunction); \
            return true; \
        }(); \
    } LPT_CONCAT(_lpt_drag_instance_, __LINE__);

