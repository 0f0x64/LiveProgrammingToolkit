#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <sstream>
#include <algorithm>
#include <type_traits>
#include <cctype>

namespace LivePT {

    constexpr int MAX_SCAN_RANGE = 1024;

    template <typename E, E V>
    inline std::string_view GetEnumNameRaw() {
        return __FUNCSIG__;
    }

    struct EnumElementDesc {
        int value;
        std::string name;
    };

    struct EnumTypeDesc {
        bool isEnum = false;
        std::vector<EnumElementDesc> elements;
    };

    inline void ParseSignature(std::string_view rawSigView, int V, EnumTypeDesc& desc) {
        std::string rawSig(rawSigView);
        std::string anchor = "GetEnumNameRaw<";
        size_t anchorPos = rawSig.find(anchor);
        if (anchorPos == std::string::npos) return;

        size_t startTemplate = anchorPos + anchor.length();
        size_t endTemplate = rawSig.find('>', startTemplate);
        if (endTemplate == std::string::npos) return;

        std::string paramsStr = rawSig.substr(startTemplate, endTemplate - startTemplate);
        size_t commaPos = paramsStr.rfind(',');
        if (commaPos == std::string::npos) return;

        std::string valueStr = paramsStr.substr(commaPos + 1);
        if (valueStr.find('(') != std::string::npos) return;

        valueStr.erase(0, valueStr.find_first_not_of(" \t"));
        valueStr.erase(valueStr.find_last_not_of(" \t") + 1);

        size_t lastCols = valueStr.rfind("::");
        if (lastCols != std::string::npos) {
            valueStr = valueStr.substr(lastCols + 2);
        }

        if (!valueStr.empty()) {
            desc.elements.push_back({ V, valueStr });
        }
    }

    template <typename E, int I>
    inline void ProcessSingleIndex(EnumTypeDesc& desc) {
        ParseSignature(GetEnumNameRaw<E, static_cast<E>(I)>(), I, desc);
    }

    template <typename E, int... Is>
    inline void ExpandEnumIndices(EnumTypeDesc& desc, std::integer_sequence<int, Is...>) {
        (ProcessSingleIndex<E, Is>(desc), ...);
    }

    template <typename E>
    inline EnumTypeDesc ReflectedEnumInfo() {
        if constexpr (!std::is_enum_v<E>) {
            return EnumTypeDesc{ false };
        }
        else {
            EnumTypeDesc desc;
            desc.isEnum = true;
            ExpandEnumIndices<E>(desc, std::make_integer_sequence<int, MAX_SCAN_RANGE>{});
            return desc;
        }
    }

    // Хранилище ядра теперь ОДНОРОДНОЕ — только чистый std::string
    struct ref {
        std::string value;
        bool loaded = false;
        std::string fileName;
        unsigned int counterID;
        EnumTypeDesc enumInfo;
    };

    inline std::vector<ref>& getParamDesc() {
        static std::vector<ref> instance;
        return instance;
    }

    inline std::unordered_map<std::string, int>& getRegistry() {
        static std::unordered_map<std::string, int> instance;
        return instance;
    }

    inline std::vector<ref>& paramDesc = getParamDesc();
    inline std::unordered_map<std::string, int>& registry = getRegistry();

    inline int getID(const std::string& key) {
        auto it = registry.find(key);
        if (it != registry.end()) return it->second;
        return -1;
    }

    // Обновление значения — это просто прямая запись пришедшего текста из VS
    inline void UpdateParamValue(int id, const std::string& newValue) {
        if (newValue.empty() || id < 0 || id >= static_cast<int>(paramDesc.size())) return;

        for (char c : newValue) {
            if (static_cast<unsigned char>(c) > 127) return;
        }

        paramDesc[id].value = newValue;
    }

    inline std::string NormalizePath(const char* fullPath) {
        std::string path(fullPath);
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    struct StaticOrderKey {
        int line;
        int column;
        bool operator<(const StaticOrderKey& other) const {
            if (line != other.line) return line < other.line;
            return column < other.column;
        }
    };
    inline int RegisterEvalPreMain(const char* file, const std::string& value, int line, int column, EnumTypeDesc enumDesc) {
        std::string absolutePath = NormalizePath(file);
        static std::unordered_map<std::string, std::map<StaticOrderKey, int>> fileCompileTree;

        StaticOrderKey key{ line, column };
        auto& fileMap = fileCompileTree[absolutePath];

        if (fileMap.find(key) != fileMap.end()) {
            return fileMap[key];
        }

        int paramID = static_cast<int>(paramDesc.size());
        paramDesc.push_back({
            .value = value,
            .loaded = false,
            .fileName = absolutePath,
            .counterID = 0,
            .enumInfo = enumDesc
            });

        fileMap[key] = paramID;

        unsigned int cleanCounterID = 0;
        for (const auto& [staticKey, assignedId] : fileMap) {
            std::string vsLookupKey = absolutePath + ":" + std::to_string(cleanCounterID);
            registry[vsLookupKey] = assignedId;
            paramDesc[assignedId].counterID = cleanCounterID;
            cleanCounterID++;
        }

        return paramID;
    }

    template <size_t N>
    struct FixedString {
        char buf[N]{};
        constexpr FixedString(const char* str) {
            for (size_t i = 0; i < N - 1 && str[i] != '\0'; ++i) {
                buf[i] = str[i];
            }
        }
        constexpr const char* c_str() const { return buf; }
    };
    template <size_t N> FixedString(const char(&str)[N]) -> FixedString<N>;

    // Реестр компиляции теперь строго монотипен — он всегда работает со строками!
    template <typename T, FixedString<260> AbsoluteFile, int Line, int Column>
    struct GlobalEvalRegistry {
        static inline const int cached_id = []() {
            if constexpr (std::is_enum_v<T>) {
                auto enumMetadata = ReflectedEnumInfo<T>();
                return RegisterEvalPreMain(AbsoluteFile.c_str(), "", Line, Column, enumMetadata);
            }
            else {
                return RegisterEvalPreMain(AbsoluteFile.c_str(), "", Line, Column, EnumTypeDesc{ false });
            }
            }();
    };

    template <typename T, FixedString<260> AbsoluteFile, int Line, int Column>
    struct EvalSyntaxShield {
        T value;
        constexpr EvalSyntaxShield(T val) : value(val) {}

        inline operator T() const {
            int target_id = GlobalEvalRegistry<T, AbsoluteFile, Line, Column>::cached_id;

            if (target_id < 0 || target_id >= static_cast<int>(paramDesc.size())) return value;

            std::string absPath = NormalizePath(AbsoluteFile.c_str());
            int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));
            if (real_id < 0 || real_id >= static_cast<int>(paramDesc.size())) return value;

            // На первом кадре сериализуем дефолтное значение из кода в строку
            if (!paramDesc[real_id].loaded) {
                if constexpr (std::is_enum_v<T>) {
                    paramDesc[real_id].value = std::to_string(static_cast<int>(value));
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    paramDesc[real_id].value = value ? "true" : "false";
                }
                else {
                    std::stringstream ss;
                    ss << value;
                    paramDesc[real_id].value = ss.str();
                }
                paramDesc[real_id].loaded = true;
            }

            // МЕХАНИЗМ КАСTA: Десериализуем строку обратно в целевой тип T хоста
            if constexpr (std::is_enum_v<T>) {
                std::string cleanQuery = paramDesc[real_id].value;
                cleanQuery.erase(std::remove_if(cleanQuery.begin(), cleanQuery.end(), ::isspace), cleanQuery.end());
                size_t lastCols = cleanQuery.rfind("::");
                if (lastCols != std::string::npos) cleanQuery = cleanQuery.substr(lastCols + 2);

                for (const auto& elem : paramDesc[real_id].enumInfo.elements) {
                    if (elem.name == cleanQuery) return static_cast<T>(elem.value);
                }

                std::stringstream ss(cleanQuery); int parsedInt = 0;
                if (ss >> parsedInt) return static_cast<T>(parsedInt);
            }
            else if constexpr (std::is_same_v<T, bool>) {
                std::string str = paramDesc[real_id].value;
                std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return (str == "true" || str == "1");
            }
            else {
                std::string cleanStr = paramDesc[real_id].value;
                // Счищаем вещественный суффикс 'f'/'F' для безопасного чтения из строкового потока
                if (!cleanStr.empty() && (cleanStr.back() == 'f' || cleanStr.back() == 'F')) {
                    cleanStr.pop_back();
                }

                std::stringstream ss(cleanStr);

                // Если тип T - int, но в строке плавающее число (например, при парсинге литерала -96.069f),
                // мы сначала считываем его в double, а затем безопасно приводим к T
                if constexpr (std::is_integral_v<T>) {
                    double doubleVal = 0.0;
                    if (ss >> doubleVal) return static_cast<T>(doubleVal);
                }
                else {
                    T parsedValue{};
                    if (ss >> parsedValue) return parsedValue;
                }
            }

            return value;
        }
    };

}

// КРИСТАЛЬНО ЧИСТЫЙ ОРИГИНАЛЬНЫЙ МАКРОС: Идеальные скобки для плагина Visual Studio
#define eval(value) ( \
    LivePT::EvalSyntaxShield< \
        decltype(value), \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(value) \
)
