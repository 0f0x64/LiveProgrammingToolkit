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

    struct ref {
        using typeVariant = std::variant<
            bool,
            char, unsigned char, signed char,
            char16_t, wchar_t,                  // <--- ДОБАВИЛИ 16-БИТНЫЕ СИМВОЛЬНЫЕ ТИПЫ
            short, unsigned short,
            int, unsigned int,
            long, unsigned long,
            long long, unsigned long long,
            float, double
        >;
        typeVariant value;
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

    inline void UpdateParamValue(int id, const std::string& newValue) {
        if (newValue.empty() || id < 0 || id >= static_cast<int>(paramDesc.size())) return;

        // Защита от нетекстовых символов в буфере
        for (char c : newValue) {
            if (static_cast<unsigned char>(c) > 127) {
                return;
            }
        }

        std::visit([&newValue, id](auto& activeValue) {
            using T = std::decay_t<decltype(activeValue)>;

            // ВЕТКА А: Обработка перечислений (Enum)
            if (paramDesc[id].enumInfo.isEnum) {
                std::string cleanQuery = newValue;
                cleanQuery.erase(std::remove_if(cleanQuery.begin(), cleanQuery.end(), ::isspace), cleanQuery.end());

                size_t lastCols = cleanQuery.rfind("::");
                if (lastCols != std::string::npos) {
                    cleanQuery = cleanQuery.substr(lastCols + 2);
                }

                for (const auto& elem : paramDesc[id].enumInfo.elements) {
                    if (elem.name == cleanQuery) {
                        activeValue = elem.value;
                        return;
                    }
                }

                std::stringstream ss(cleanQuery);
                int parsedInt;
                if (ss >> parsedInt) {
                    activeValue = parsedInt;
                }
                return;
            }

            // ВЕТКА Б: Обработка булевых флагов (bool)
            if constexpr (std::is_same_v<T, bool>) {
                std::string str = newValue;
                std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                    });

                if (str == "true" || str == "1") {
                    activeValue = true;
                }
                else if (str == "false" || str == "0") {
                    activeValue = false;
                }
            }
            // ВЕТКА В: Обработка числовых литералов (Все стандартные типы C++)
            else {
                // ЖЕСТКИЙ ФИКС ДЛЯ ВСЕХ СИМВОЛЬНЫХ ТИПОВ (8-бит и 16-бит):
                // std::stringstream считает char, wchar_t и char16_t за текстовые буквы.
                // Чтобы строка "120" превратилась в число 120, а не в символ 'x',
                // мы принудительно парсим текст через промежуточный int.
                if constexpr (std::is_same_v<T, char> || std::is_same_v<T, unsigned char> || std::is_same_v<T, signed char> ||
                    std::is_same_v<T, char16_t> || std::is_same_v<T, wchar_t>)
                {
                    std::stringstream ss(newValue);
                    int parsedInt;
                    if (ss >> parsedInt) {
                        activeValue = static_cast<T>(parsedInt);
                    }
                }
                // Для всех остальных типов (int, short, long, float, double) парсим нативно
                else {
                    std::stringstream ss(newValue);
                    T parsedValue;
                    if (ss >> parsedValue) {
                        activeValue = parsedValue;
                    }
                }
            }
            }, paramDesc[id].value);
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

    inline int RegisterEvalPreMain(const char* file, ref::typeVariant value, int line, int column, EnumTypeDesc enumDesc) {
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

    template <typename T, FixedString<260> AbsoluteFile, int Line, int Column>
    struct GlobalEvalRegistry {
        static inline const int cached_id = []() {
            if constexpr (std::is_enum_v<T>) {
                auto enumMetadata = ReflectedEnumInfo<T>();
                return RegisterEvalPreMain(AbsoluteFile.c_str(), ref::typeVariant{ static_cast<int>(T{}) }, Line, Column, enumMetadata);
            }
            else {
                return RegisterEvalPreMain(AbsoluteFile.c_str(), ref::typeVariant{ T{} }, Line, Column, EnumTypeDesc{ false });
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

            if (!paramDesc[target_id].loaded) {
                if constexpr (std::is_enum_v<T>) {
                    paramDesc[target_id].value = static_cast<int>(value);
                }
                else {
                    paramDesc[target_id].value = value;
                }
                paramDesc[target_id].loaded = true;
            }

            std::string absPath = NormalizePath(AbsoluteFile.c_str());
            int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));
            if (real_id < 0 || real_id >= static_cast<int>(paramDesc.size())) return value;

            if constexpr (std::is_enum_v<T>) {
                if (auto pVal = std::get_if<int>(&paramDesc[real_id].value)) {
                    return static_cast<T>(*pVal);
                }
            }
            else {
                if (auto pVal = std::get_if<T>(&paramDesc[real_id].value)) return *pVal;
            }

            return value;
        }
    };

}

#define eval(value) ( \
    LivePT::EvalSyntaxShield< \
        decltype(value), \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(value) \
)