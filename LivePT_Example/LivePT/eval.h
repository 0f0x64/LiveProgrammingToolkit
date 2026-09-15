#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <variant>
#include <any>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <type_traits>
#include <cctype>
#include <string_view>

namespace LivePT {

    // ... твой метод UpdateParamValue ...

    // ВРЕМЕННАЯ ЗАГЛУШКА ДЛЯ ЛИНКЕРА:
    // Возвращает false, так как в текстовом режиме мышь гарантированно не драгает
    inline bool isMouseDragging() {
        return false;
    }
}

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

    struct ref {
        std::any value; // Наше новое универсальное хранилище
        bool loaded = false;
        std::string fileName;
        unsigned int counterID;
        EnumTypeDesc enumInfo;

        std::string typeName;
        bool isEnum = false;
        bool (*parseFromString)(const std::string& text, std::any& target) = nullptr;
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
}


namespace LivePT {

    // Вспомогательный метод разбиения строки аргументов "color{10,20,30}" -> ["10","20","30"]
    inline std::vector<std::string> SplitArgsFromText(const std::string& text) {
        std::vector<std::string> tokens;
        size_t start = text.find('{');
        size_t end = text.find_last_of('}');
        if (start == std::string::npos || end == std::string::npos || end <= start) return tokens;

        std::stringstream ss(text.substr(start + 1, end - start - 1));
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty() && (token.back() == 'f' || token.back() == 'F')) token.pop_back();
            tokens.push_back(token);
        }
        return tokens;
    }

    // Универсальный парсер по умолчанию (безопасный для чисел, булов, текстовых энамов и любых агрегатов)
    template <typename T>
    inline bool DefaultTypeParser(const std::string& text, std::any& target) {
        if constexpr (std::is_enum_v<T>) {
            std::stringstream ss(text);
            int parsedValue;
            if (ss >> parsedValue) {
                target = static_cast<T>(parsedValue);
                return true;
            }
            return false;
        }
        else if constexpr (std::is_same_v<T, bool>) {
            std::string str = text;
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (str == "true" || str == "1") { target = true; return true; }
            if (str == "false" || str == "0") { target = false; return true; }
            return false;
        }
        else if constexpr (std::is_arithmetic_v<T>) {
            std::stringstream ss(text);
            T parsedValue;
            if (ss >> parsedValue) {
                target = parsedValue;
                return true;
            }
            return false;
        }
        else if constexpr (std::is_aggregate_v<T>) {
            auto tokens = SplitArgsFromText(text);
            if (tokens.empty()) return false;

            T obj{};
            size_t tokenIdx = 0;
            bool success = true;

            unsigned char* bytePtr = reinterpret_cast<unsigned char*>(&obj);
            size_t bytesPerComponent = sizeof(T) / tokens.size();

            for (size_t i = 0; i < tokens.size(); ++i) {
                std::stringstream ss(tokens[i]);
                float val;
                if (ss >> val) {
                    if (bytesPerComponent == 1) { // 1-байтовые поля (наш color из unsigned char)
                        bytePtr[i] = static_cast<unsigned char>(val);
                    }
                    else if (bytesPerComponent == 4) { // 4-байтовые поля (int / float)
                        std::memcpy(bytePtr + (i * 4), &val, 4);
                    }
                    else if (bytesPerComponent == 8) { // 8-байтовые поля (double)
                        double dVal = val;
                        std::memcpy(bytePtr + (i * 8), &dVal, 8);
                    }
                }
                else {
                    success = false;
                    break;
                }
            }

            if (success) {
                target = obj;
                return true;
            }
            return false;
        }
        return false;
    }



    
}
namespace LivePT {

    template <typename T>
    inline std::string_view GetTypeName() {
        std::string_view sig = __FUNCSIG__;
        size_t start = sig.find("GetTypeName<");
        if (start == std::string_view::npos) return "UnknownType";
        start += 12; // Смещение за "GetTypeName<"
        size_t end = sig.find_last_of('>');
        if (end == std::string_view::npos || end <= start) return "UnknownType";

        std::string_view rawName = sig.substr(start, end - start);

        if (rawName.rfind("struct ", 0) == 0) rawName.remove_prefix(7);
        else if (rawName.rfind("class ", 0) == 0) rawName.remove_prefix(6);

        return rawName;
    }

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
}
namespace LivePT {

    inline void UpdateParamValue(int id, const std::string& newValue) {
        if (newValue.empty() || id < 0 || id >= static_cast<int>(paramDesc.size())) return;

        for (char c : newValue) {
            if (static_cast<unsigned char>(c) > 127) {
                return;
            }
        }

        std::string cleanQuery = newValue;
        cleanQuery.erase(std::remove_if(cleanQuery.begin(), cleanQuery.end(), ::isspace), cleanQuery.end());

        // ХЕНДЛЕР ДЛЯ ЕНАМОВ: Ищем текстовое имя в скомпилированной карте элементов
        if (paramDesc[id].isEnum) {
            size_t lastCols = cleanQuery.rfind("::");
            if (lastCols != std::string::npos) {
                cleanQuery = cleanQuery.substr(lastCols + 2);
            }

            // Проверяем текстовое имя по базе элементов енама
            for (const auto& elem : paramDesc[id].enumInfo.elements) {
                if (elem.name == cleanQuery) {
                    // Нашли! Записываем базовое значение int, приведенное к типу енама
                    // Используем сохраненную функцию парсинга, передав ей строковое число
                    if (paramDesc[id].parseFromString) {
                        paramDesc[id].parseFromString(std::to_string(elem.value), paramDesc[id].value);
                    }
                    return;
                }
            }

            // Фоллбэк: если пользователь в VS ввел енам чистой цифрой (например, "1")
            std::stringstream ss(cleanQuery);
            int parsedInt;
            if (ss >> parsedInt) {
                if (paramDesc[id].parseFromString) {
                    paramDesc[id].parseFromString(cleanQuery, paramDesc[id].value);
                }
            }
            return;
        }

        // ХЕНДЛЕР ДЛЯ ВСЕХ ОСТАЛЬНЫХ ТИПОВ (числа, булы, кастомный color)
        if (paramDesc[id].parseFromString) {
            paramDesc[id].parseFromString(cleanQuery, paramDesc[id].value);
        }
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
}
namespace LivePT {

    inline int RegisterEvalPreMain(const char* file, std::any value, int line, int column, std::string typeName, bool isEnum, EnumTypeDesc enumDesc) {
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
            .enumInfo = enumDesc,
            .typeName = typeName,
            .isEnum = isEnum,
            .parseFromString = nullptr
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

    template <typename T, FixedString<260> AbsoluteFile, int Line, int Column>
    struct GlobalEvalRegistry {
        static inline const int cached_id = []() {
            std::string tName(GetTypeName<T>());
            bool isEnum = std::is_enum_v<T>;

            // Просто регистрируем ID, передавая пустой EnumTypeDesc
            int paramID = RegisterEvalPreMain(AbsoluteFile.c_str(), std::any{ T{} }, Line, Column, tName, isEnum, EnumTypeDesc{ false });

            // Привязываем базовый парсер
            paramDesc[paramID].parseFromString = &DefaultTypeParser<T>;

            return paramID;
            }();
    };



    template <typename T, FixedString<260> AbsoluteFile, int Line, int Column>
    struct EvalSyntaxShield {
        T value;
        constexpr EvalSyntaxShield(T val) : value(val) {}

        inline operator T() const {
            int target_id = GlobalEvalRegistry<T, AbsoluteFile, Line, Column>::cached_id;

            if (target_id < 0 || target_id >= static_cast<int>(paramDesc.size())) return value;

            if (!paramDesc[target_id].loaded) {
                paramDesc[target_id].value = value;
                paramDesc[target_id].loaded = true;

                if constexpr (std::is_enum_v<T>) {
                    paramDesc[target_id].enumInfo = ReflectedEnumInfo<T>();
                }
            }

            std::string absPath = NormalizePath(AbsoluteFile.c_str());
            int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));
            if (real_id < 0 || real_id >= static_cast<int>(paramDesc.size())) return value;

            // БАЙПАС НЕПОДДЕРЖИВАЕМЫХ ТИПОВ (Указатели, строки и сложные классы пролетают насквозь)
            if constexpr (!std::is_aggregate_v<T> && !std::is_arithmetic_v<T> && !std::is_enum_v<T>) {
                return value;
            }

            if (auto pVal = std::any_cast<T>(&paramDesc[real_id].value)) {
                return *pVal;
            }

            // БАЙПАС ОШИБКИ ПАРСИНГА СТРОКИ: Если парсер завалился на имени переменной,
            // возвращаем исходное безопасное значение из C++ кода
            return value;
        }

    };

}

// Вариативный макрос, корректно собирающий __VA_ARGS__ при наличии запятых во входящем выражении
#define eval(...) ( \
    LivePT::EvalSyntaxShield< \
        decltype(__VA_ARGS__), \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(__VA_ARGS__) \
)
