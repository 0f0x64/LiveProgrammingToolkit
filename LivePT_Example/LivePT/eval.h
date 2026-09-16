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

    // 1. Конвейер одного токена под точный тип поля F
    template <typename F>
    inline void ProcessSingleField(const std::string& token, const F& liveValue, F& outField) {
        if constexpr (std::is_enum_v<F>) {
            std::string cleanEnum = token;
            size_t lastCols = cleanEnum.rfind("::");
            if (lastCols != std::string::npos) cleanEnum = cleanEnum.substr(lastCols + 2);

            auto enumDesc = ReflectedEnumInfo<F>();
            for (const auto& elem : enumDesc.elements) {
                if (elem.name == cleanEnum) {
                    outField = static_cast<F>(elem.value);
                    return;
                }
            }
            int val;
            if (std::from_chars(token.data(), token.data() + token.size(), val).ec == std::errc()) {
                outField = static_cast<F>(val); return;
            }
            outField = liveValue;
        }
        else if constexpr (std::is_same_v<F, bool>) {
            if (token == "true" || token == "1") { outField = true; return; }
            if (token == "false" || token == "0") { outField = false; return; }
            outField = liveValue;
        }
        else if constexpr (std::is_arithmetic_v<F>) {
            std::string parseStr = token;
            if constexpr (std::is_floating_point_v<F>) {
                if (!parseStr.empty() && (parseStr.back() == 'f' || parseStr.back() == 'F')) parseStr.pop_back();
            }

            F val;
            auto [ptr, ec] = std::from_chars(parseStr.data(), parseStr.data() + parseStr.size(), val);
            if (ec == std::errc::invalid_argument) {
                outField = liveValue;
                return;
            }
            if (ec == std::errc::result_out_of_range) {
                val = (parseStr == "-") ? (std::numeric_limits<F>::min)() : (std::numeric_limits<F>::max)();
            }
            outField = val;
        }
    }

    // УНИВЕРСАЛЬНЫЕ ГЛОБАЛЬНЫЕ ТИПЫ ДЛЯ ЧЕСТНОЙ COMPILE-TIME ИНСПЕКЦИИ АГРЕГАТОВ
    struct AnyField { template <typename U> operator U() const; };

    // Глобальный, плоский compile-time счетчик количества полей без вложенных лямбд и requires!
    // Полностью устраняет ошибки C2951 и C1506 в MSVC
    template <typename T, size_t... Is>
    constexpr auto IsAssignableImpl(std::index_sequence<Is...>) -> decltype(T{ (Is, AnyField{})..., AnyField{} }, std::true_type{}) { return {}; }

    template <typename T, size_t... Is>
    constexpr std::false_type IsAssignableImpl(...) { return {}; }

    template <typename T, size_t N = 0>
    constexpr size_t DetectFieldsCount() {
        if constexpr (decltype(IsAssignableImpl<T>(std::make_index_sequence<N>{}))::value) {
            return DetectFieldsCount<T, N + 1>();
        }
        else {
            return N;
        }
    }

    // Вспомогательный прокси-насос полей, вынесенный на уровень namespace
    template <typename T>
    struct AggregateFieldPumper {
        size_t idx;
        const std::vector<std::string>& tks;
        const unsigned char* livePtr;
        mutable size_t byteOffset;

        template <typename F>
        operator F() const {
            F result{};

            // alignof(F) идеально находит скрытые padding-байты MSVC!
            size_t alignment = alignof(F);
            byteOffset = (byteOffset + alignment - 1) & ~(alignment - 1);

            if (idx < tks.size() && byteOffset + sizeof(F) <= sizeof(T)) {
                F liveFieldValue;
                std::memcpy(&liveFieldValue, livePtr + byteOffset, sizeof(F));

                ProcessSingleField(tks[idx], liveFieldValue, result);
            }
            else {
                if (byteOffset + sizeof(F) <= sizeof(T)) {
                    std::memcpy(&result, livePtr + byteOffset, sizeof(F));
                }
            }

            byteOffset += sizeof(F);
            return result;
        }
    };

    // Универсальный распаковщик структуры на базе index_sequence
    template <typename T, size_t... Is>
    inline T ReconstructAggregate(const std::vector<std::string>& tokens, const unsigned char* liveBytesPtr, std::index_sequence<Is...>) {
        size_t offsetTracker = 0;
        return T{ AggregateFieldPumper<T>{ Is, tokens, liveBytesPtr, offsetTracker }... };
    }

    // Главный, полностью обобщенный DefaultTypeParser
    template <typename T>
    inline bool DefaultTypeParser(const std::string& text, std::any& target) {
        if constexpr (std::is_enum_v<T>) {
            std::string cleanQuery = text;
            size_t lastCols = cleanQuery.rfind("::");
            if (lastCols != std::string::npos) cleanQuery = cleanQuery.substr(lastCols + 2);

            auto enumDesc = ReflectedEnumInfo<T>();
            for (const auto& elem : enumDesc.elements) {
                if (elem.name == cleanQuery) { target = static_cast<T>(elem.value); return true; }
            }
            std::stringstream ss(cleanQuery); int parsedValue;
            if (ss >> parsedValue) { target = static_cast<T>(parsedValue); return true; }
            return false;
        }
        else if constexpr (std::is_same_v<T, bool>) {
            std::string str = text;
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (str == "true" || str == "1") { target = true; return true; }
            if (str == "false" || str == "0") { target = false; return true; }
            target = false; return true;
        }
        else if constexpr (std::is_arithmetic_v<T>) {
            std::string parseStr = text;
            if constexpr (std::is_floating_point_v<T>) {
                if (!parseStr.empty() && (parseStr.back() == 'f' || parseStr.back() == 'F')) parseStr.pop_back();
            }

            T val;
            auto [ptr, ec] = std::from_chars(parseStr.data(), parseStr.data() + parseStr.size(), val);
            if (ec == std::errc::invalid_argument) return false;
            if (ec == std::errc::result_out_of_range) {
                val = (parseStr == "-") ? (std::numeric_limits<T>::min)() : (std::numeric_limits<T>::max)();
            }
            target = val;
            return true;
        }
        else if constexpr (std::is_aggregate_v<T>) {
            auto tokens = SplitArgsFromText(text);
            if (tokens.empty()) return false;

            T liveObj = std::any_cast<T>(target);
            const unsigned char* liveBytesPtr = reinterpret_cast<const unsigned char*>(&liveObj);

            // Идеальный плоский compile-time счетчик полей без requires
            constexpr size_t fieldsCount = DetectFieldsCount<T>();

            target = ReconstructAggregate<T>(tokens, liveBytesPtr, std::make_index_sequence<fieldsCount>{});
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
            // МАГИЯ СТАТИЧЕСКОГО КЭША: Вычисляется ровно ОДИН раз за всю жизнь программы!
            // В последующих кадрах процессор просто мгновенно возвращает готовую ссылку,
            // снижая нагрузку на CPU до абсолютного нуля (0% оверхеда в игровом цикле).
            static const EnumTypeDesc cachedDesc = []() {
                EnumTypeDesc desc;
                desc.isEnum = true;
                ExpandEnumIndices<E>(desc, std::make_integer_sequence<int, MAX_SCAN_RANGE>{});
                return desc;
                }();

            return cachedDesc;
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

#define eval(...) ( \
    LivePT::EvalSyntaxShield< \
        decltype(__VA_ARGS__), \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(__VA_ARGS__) \
)
