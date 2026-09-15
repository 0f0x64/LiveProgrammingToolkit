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
        // Логика булов и энамов остается тривиальной
        if constexpr (std::is_enum_v<T>) {
            std::string cleanQuery = text;
            // Отрезаем пространство имен энама, если оно прилетело ("Primitive::ptype::box" -> "box")
            size_t lastCols = cleanQuery.rfind("::");
            if (lastCols != std::string::npos) {
                cleanQuery = cleanQuery.substr(lastCols + 2);
            }

            // Генерируем compile-time карту имен для этого типа энама T
            auto enumDesc = ReflectedEnumInfo<T>();

            // Ищем текстовое совпадение по элементам
            for (const auto& elem : enumDesc.elements) {
                if (elem.name == cleanQuery) {
                    target = static_cast<T>(elem.value);
                    return true;
                }
            }

            // Фоллбэк: если пользователь в VS ввел энам чистой цифрой (например, "1")
            std::stringstream ss(cleanQuery);
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

            if (str == "true" || str == "1") {
                target = true;
                return true;
            }
            if (str == "false" || str == "0") {
                target = false;
                return true;
            }

            // ЖЕЛЕЗОБЕТОННАЯ ЗАЩИТА: Если в буле написано что-то другое (опечатка, мусор, любые буквы),
            // мы ПРИНУДИТЕЛЬНО гасим параметр в false и возвращаем true, чтобы заблокировать байпас!
            target = false;
            return true;
        }
        // 1. АВТО-БАЙПАС АТОМАРНЫХ ЧИСЕЛ (int, float, double)
        else if constexpr (std::is_arithmetic_v<T>) {
            std::string parseStr = text;
            if constexpr (std::is_floating_point_v<T>) {
                if (!parseStr.empty() && (parseStr.back() == 'f' || parseStr.back() == 'F')) parseStr.pop_back();
            }

            T val;
            auto [ptr, ec] = std::from_chars(parseStr.data(), parseStr.data() + parseStr.size(), val);

            if (ec == std::errc::invalid_argument) return false; // Буквы переменной -> Полный БАЙПАС (вернем живое значение)
            if (ec == std::errc::result_out_of_range) {
                val = (parseStr[0] == '-') ? (std::numeric_limits<T>::min)() : (std::numeric_limits<T>::max)();
            }
            target = val;
            return true;
        }
        // 2. АВТО-БАЙПАС СТРУКТУР ЛЮБОГО РАЗМЕРА (color, Vec3, Matrix)
        else if constexpr (std::is_aggregate_v<T>) {
            auto tokens = SplitArgsFromText(text);
            if (tokens.empty()) return false;

            T obj = std::any_cast<T>(target);
            unsigned char* bytePtr = reinterpret_cast<unsigned char*>(&obj);

            // Задаем базовый шаг смещения по умолчанию на основе пропорций структуры
            size_t defaultStep = (sizeof(T) / tokens.size() == 0) ? 1 : (sizeof(T) / tokens.size());
            size_t currentOffset = 0;
            bool success = true;

            for (size_t i = 0; i < tokens.size() && currentOffset < sizeof(T); ++i) {
                std::string t = tokens[i];

                // 1. ОПРЕДЕЛЯЕМ ТИП КОМПОНЕНТА ПО СИНТАКСИСУ СТРОКИ И СМЕЩАЕМ БАЙТЫ ДИНАМИЧЕСКИ
                if (t.find('.') != std::string::npos || t.back() == 'f' || t.back() == 'F') {
                    // Это гарантированно float поле (4 байта)
                    if (currentOffset + 4 <= sizeof(T)) {
                        if (!t.empty() && (t.back() == 'f' || t.back() == 'F')) t.pop_back();
                        float val;
                        auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), val);
                        if (ec == std::errc()) {
                            if (ec == std::errc::result_out_of_range) val = (t == "-") ? -(std::numeric_limits<float>::max)() : (std::numeric_limits<float>::max)();
                            std::memcpy(bytePtr + currentOffset, &val, 4);
                        }
                    }
                    currentOffset += 4; // Шагаем на размер float
                }
                else {
                    // Это либо целое число (int/char), либо вложенный энам/переменная (color::tt::on)
                    int val;
                    auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), val);

                    if (ec == std::errc()) {
                        // Токен успешно распарсился как число! Смотрим, куда его положить
                        if (defaultStep == 1 && currentOffset + 1 <= sizeof(T)) {
                            if (val < 0) val = 0; if (val > 255) val = 255;
                            bytePtr[currentOffset] = static_cast<unsigned char>(val);
                        }
                        else if (defaultStep == 4 && currentOffset + 4 <= sizeof(T)) {
                            if (ec == std::errc::result_out_of_range) val = (t == "-") ? INT_MIN : INT_MAX;
                            std::memcpy(bytePtr + currentOffset, &val, 4);
                        }
                        currentOffset += defaultStep;
                    }
                    else {
                        // УМНЫЙ БАЙПАС ДЛЯ ВЛОЖЕННЫХ ЭНАМОВ И ПЕРЕМЕННЫХ (color::tt::on или g):
                        // std::from_chars выдал ошибку invalid_argument. Мы просто пропускаем этот кусок памяти,
                        // сохраняя в нем нативное значение из С++ кода игры, и смещаем указатель дальше!
                        currentOffset += defaultStep;
                    }
                }
            }

            target = obj;
            return true;
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
            if (static_cast<unsigned char>(c) > 127) return;
        }

        std::string cleanQuery = newValue;
        cleanQuery.erase(std::remove_if(cleanQuery.begin(), cleanQuery.end(), [](unsigned char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            }), cleanQuery.end());

        // Записываем СЫРУЮ СТРОКУ текста в базу. Теперь энамы встают на один конвейер со строками!
        paramDesc[id].value = cleanQuery;
        paramDesc[id].loaded = true;
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

            // Ленивая сборка метаданных энама (выполняется один раз при старте)
            if (!paramDesc[target_id].loaded) {
                paramDesc[target_id].loaded = true;
                if constexpr (std::is_enum_v<T>) {
                    paramDesc[target_id].enumInfo = ReflectedEnumInfo<T>();
                }
            }

            std::string absPath = NormalizePath(AbsoluteFile.c_str());
            int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));
            if (real_id < 0 || real_id >= static_cast<int>(paramDesc.size())) return value;

            if constexpr (!std::is_aggregate_v<T> && !std::is_arithmetic_v<T> && !std::is_enum_v<T>) {
                return value;
            }

            // ЕСЛИ ПОЛЬЗОВАТЕЛЬ ПРАВИЛ ТЕКСТ В VS: в базе лежит сырая строка std::string
            if (paramDesc[real_id].value.type() == typeid(std::string)) {
                std::string vsText = std::any_cast<std::string>(paramDesc[real_id].value);

                // Передаем текущее живое runtime-значение из кода (наш свежий rand()) как подложку!
                std::any mergedTarget = value;

                // Накатываем текстовую маску VS поверх живого объекта из кода
                if (DefaultTypeParser<T>(vsText, mergedTarget)) {
                    return std::any_cast<T>(mergedTarget);
                }

                // Если DefaultTypeParser вернул false (атомарная переменная "x" в eval(x)),
                // мы просто возвращаем живой "value" из С++ кода игры
                return value;
            }

            // АВТО-БАЙПАС: Если пользователь еще ни разу не наводил курсор в VS, 
            // или база пуста, мы просто отдаем живой rand() из С++ кода без изменений!
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
