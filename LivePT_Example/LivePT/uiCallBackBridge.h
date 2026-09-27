namespace LivePT {

    using InternalDoubleClickCallback = std::function<void(const std::any&, std::function<void(std::string)>)>;

    inline std::unordered_map<std::string, InternalDoubleClickCallback>& GetTypeCallbackRegistry() {
        static std::unordered_map<std::string, InternalDoubleClickCallback> instance;
        return instance;
    }

    template <typename T>
    inline void RegisterTypeDoubleClickCallback(std::function<void(const T&, std::function<void(std::string)>)> userCallback) {
        std::string typeName = typeid(T).name();

        GetTypeCallbackRegistry()[typeName] = [userCallback](const std::any& anyValue, std::function<void(std::string)> updateVs) {
            if (const T* pInstance = std::any_cast<T>(&anyValue)) {
                userCallback(*pInstance, updateVs);
            }
            };
    }

#define LPT_REGISTER_TYPE(Type, Callback) \
    static inline bool _lpt_init_##Type = []() { \
        LivePT::RegisterTypeDoubleClickCallback<Type>(Callback); \
        return true; \
    }();
}