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
            char16_t, wchar_t,
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

        // ЖЕЛЕЗНЫЕ ЛИМИТЫ ТИПА: Заполняются один раз компилятором при старте,
        // чтобы DragNumericValue мгновенно зажимал мышку без хардкода логики
        long long typeMinBound = 0;
        long long typeMaxBound = 0;
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
            .enumInfo = enumDesc,
            .typeMinBound = 0, // Инициализируем нулями, щит сам перезапишет их под нужный тип
            .typeMaxBound = 0
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

    // 1. Главный синтаксический щит, который работает в рантайме 60 FPS.
// Теперь он полностью шаблонный по TargetType (типу поля структуры, а не литерала!)
    template <typename TargetType, FixedString<260> AbsoluteFile, int Line, int Column>
    struct EvalSyntaxShield {
        inline static TargetType Get(int literalValue) {
            // Регистрируем параметр в глобальной базе данных под ИСТИННЫМ типом TargetType
            int target_id = GlobalEvalRegistry<TargetType, AbsoluteFile, Line, Column>::cached_id;

            if (target_id < 0 || target_id >= static_cast<int>(paramDesc.size())) {
                return static_cast<TargetType>(literalValue);
            }

            // Кадр 1: Инициализируем variant ИСТИННЫМ типом
            if (!paramDesc[target_id].loaded) {
                if constexpr (std::is_enum_v<TargetType>) {
                    paramDesc[target_id].value = static_cast<int>(literalValue);
                }
                else {
                    paramDesc[target_id].value = static_cast<TargetType>(literalValue);
                }

                // СИСТЕМНЫЙ ЗАЖИМ: Поскольку TargetType нам точно известен,
                // мы пишем в базу абсолютно верные аппаратные лимиты (например, [0; 255] для unsigned char)
                if constexpr (std::integral<TargetType> || std::floating_point<TargetType>) {
                    long long minB = static_cast<long long>((std::numeric_limits<TargetType>::min)());
                    long long maxB = static_cast<long long>((std::numeric_limits<TargetType>::max)());
                    if constexpr (std::floating_point<TargetType>) {
                        minB = static_cast<long long>(-(std::numeric_limits<TargetType>::max)());
                    }
                    paramDesc[target_id].typeMinBound = minB;
                    paramDesc[target_id].typeMaxBound = maxB;
                }

                paramDesc[target_id].loaded = true;
            }

            std::string absPath = NormalizePath(AbsoluteFile.c_str());
            int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));
            if (real_id < 0 || real_id >= static_cast<int>(paramDesc.size())) {
                return static_cast<TargetType>(literalValue);
            }

            // Извлекаем измененные мышкой данные из variant
            if constexpr (std::is_enum_v<TargetType>) {
                if (auto pVal = std::get_if<int>(&paramDesc[real_id].value)) {
                    return static_cast<TargetType>(*pVal);
                }
            }
            else {
                if (auto pVal = std::get_if<TargetType>(&paramDesc[real_id].value)) {
                    return *pVal;
                }
            }

            return static_cast<TargetType>(literalValue);
        }
    };

    // 2. Легкая фабрика-болванка, которая перехватывает тип левой части (Type Deduction)
        // 2. Универсальная фабрика-болванка с шаблонным конструктором
    template <FixedString<260> AbsoluteFile, int Line, int Column>
    struct LazyTypeDetector {

        // Делаем тип сохраняемого значения полностью шаблонным (универсальный контейнер)
        template <typename TValue>
        struct Container {
            TValue value;
            constexpr Container(TValue val) : value(val) {}
        };

        // Мы храним внутри variant только те базовые типы, которые могут прилететь из макроса eval
        std::variant<int, float, bool, long long, double> rawValue;
        int enumOrCharFallback = 0; // Спец-запас для хранения бинарных значений енамов/чаров

        // Шаблонный конструктор: теперь он с радостью сожрёт и int, и float, и bool, и enum class!
        template <typename TValue>
        constexpr LazyTypeDetector(TValue val) {
            if constexpr (std::is_enum_v<TValue>) {
                // Если прилетел enum class — безопасно кастим его к int и сохраняем в fallback
                enumOrCharFallback = static_cast<int>(val);
            }
            else if constexpr (std::is_integral_v<TValue> || std::is_floating_point_v<TValue>) {
                // Все стандартные примитивы раскладываем по полочкам
                if constexpr (std::is_same_v<TValue, bool>) rawValue = static_cast<bool>(val);
                else if constexpr (std::is_floating_point_v<TValue>) rawValue = static_cast<double>(val);
                else enumOrCharFallback = static_cast<int>(val);
            }
        }

        // Магический C++20 оператор автоматического вывода левой части
        template <typename TargetType>
        inline operator TargetType() const {

            // Восстанавливаем исходное числовое значение, которое передал пользователь
            int finalLiteralValue = enumOrCharFallback;

            if (std::holds_alternative<bool>(rawValue)) {
                finalLiteralValue = std::get<bool>(rawValue) ? 1 : 0;
            }
            else if (std::holds_alternative<double>(rawValue)) {
                // Для float временно кастим к int для передачи через get (внутри EvalSyntaxShield всё восстановится)
                finalLiteralValue = static_cast<int>(std::get<double>(rawValue));
            }

            // Передаем управление в рантайм-щит под ИСТИННЫМ типом поля структуры!
            return EvalSyntaxShield<TargetType, AbsoluteFile, Line, Column>::Get(finalLiteralValue);
        }
    };

}

// 3. Ультра-чистый макрос eval без фиксации decltype(value).
// Он просто создает болванку, а тип протащится сам в момент присвоения!
#define eval(value) \
    LivePT::LazyTypeDetector< \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(value)
    