namespace LivePT {

    struct EnumElementDesc {
        int value;
        std::string name;
    };

    struct EnumTypeDesc {
        bool isEnum = false;
        std::vector<EnumElementDesc> elements;
    };

    // Описание одного конкретного поля структуры
    struct StructMemberDesc {
        std::string name;
        DWORD offset;
        DWORD size;
        std::string typeName;
    };

    // Контейнер-кэш для хранения анатомии всей структуры
    struct StructTypeDesc {
        bool isStruct = false;
        std::vector<StructMemberDesc> members; // Сюда осядет кэш из PDB один раз
    };

    struct ref {
        std::any value;
        bool loaded = false;
        std::string fileName;
        unsigned int counterID;
        EnumTypeDesc enumInfo;

        // ТОЧЕЧНЫЙ ДОБАВОК: Сюда макрос запишет флаг, а рантайм сохранит кэш полей
        StructTypeDesc structInfo;

        long long typeMinBound = 0;
        long long typeMaxBound = 0;

        int line = 0;
        int column = 0;

        void (*stringUpdater)(std::any& targetAny, const std::string& textValue) = nullptr;
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

    // УЛЬТРА-СЖАТЫЙ И БЫСТРЫЙ МЕТОД ОБНОВЛЕНИЯ ПАМЯТИ
    inline void UpdateParamValue(int id, const std::string& newValue) {
        if (newValue.empty() || id < 0 || id >= static_cast<int>(paramDesc.size())) return;

        // Защита от нетекстовых символов в буфере
        for (char c : newValue) {
            if (static_cast<unsigned char>(c) > 127) return;
        }

        // Если это енам — его логика обработки остается изолированной (через PDB кэш селектора)
        if (paramDesc[id].enumInfo.isEnum) {
            std::string cleanName = newValue;
            size_t lastCols = cleanName.rfind("::");
            if (lastCols != std::string::npos) {
                cleanName = cleanName.substr(lastCols + 2);
            }
            for (const auto& elem : paramDesc[id].enumInfo.elements) {
                if (elem.name == cleanName) {
                    paramDesc[id].value = elem.value; // Енамы внутри any храним как плоский int
                    return;
                }
            }
            return;
        }

        // ДЛЯ ВСЕХ ОСТАЛЬНЫХ ТИПОВ: Просто вызываем сгенерированный хук. 
        // Если мы не умеем редачить этот тип (структуру) — хук будет равен nullptr, мы просто игнорируем
        // попытку текстовой записи, и объект продолжает жить в игре «как есть».
        if (paramDesc[id].stringUpdater) {
            paramDesc[id].stringUpdater(paramDesc[id].value, newValue);
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

    inline int RegisterEvalPreMain(const char* file, std::any value, int line, int column, EnumTypeDesc enumDesc) {
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
            .typeMinBound = 0,
            .typeMaxBound = 0,
            .line = line,
            .column = column
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
                return RegisterEvalPreMain(AbsoluteFile.c_str(), std::any{ static_cast<int>(T{}) }, Line, Column, EnumTypeDesc{ .isEnum = true });
            }
            else {
                // ПУЛЕНЕПРОБИВАЕМЫЙ ПРОПУСК ДЛЯ СТРУКТУР:
                // Если у типа нет дефолтного конструктора (сложная структура) или мы его не знаем,
                // мы просто пушим пустой std::any{}. Функция EvalSyntaxShield::Get на первом кадре 
                // сама запишет туда живой literalValue, переданный пользователем!
                if constexpr (std::is_default_constructible_v<T>) {
                    return RegisterEvalPreMain(AbsoluteFile.c_str(), std::any{ T{} }, Line, Column, EnumTypeDesc{ false });
                }
                else {
                    return RegisterEvalPreMain(AbsoluteFile.c_str(), std::any{}, Line, Column, EnumTypeDesc{ false });
                }
            }
            }();
    };

    template <typename TargetType, FixedString<260> AbsoluteFile, int Line, int Column>
    struct EvalSyntaxShield {
        template <typename TLiteral>
        inline static TargetType Get(TLiteral literalValue) {
            int target_id = GlobalEvalRegistry<TargetType, AbsoluteFile, Line, Column>::cached_id;

            if (target_id < 0 || target_id >= static_cast<int>(paramDesc.size())) {
                return static_cast<TargetType>(literalValue);
            }

            // Инициализация при первом проходе (кадре)
            if (!paramDesc[target_id].loaded) {
                if constexpr (std::is_enum_v<TargetType>) {
                    paramDesc[target_id].value = static_cast<int>(literalValue);
                }
                else {
                    paramDesc[target_id].value = static_cast<TargetType>(literalValue);
                }

                // ВЕТКА А: Генерация хука для булевых флагов
                if constexpr (std::is_same_v<TargetType, bool>) {
                    paramDesc[target_id].stringUpdater = [](std::any& targetAny, const std::string& textValue) {
                        std::string str = textValue;
                        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        targetAny = (str == "true" || str == "1");
                        };
                }
                // ВЕТКА Б: Генерация хука для базовых числовых типов C++ (int, float, double и т.д.)
                else if constexpr (std::integral<TargetType> || std::floating_point<TargetType>) {
                    long long minB = static_cast<long long>((std::numeric_limits<TargetType>::min)());
                    long long maxB = static_cast<long long>((std::numeric_limits<TargetType>::max)());
                    if constexpr (std::floating_point<TargetType>) {
                        minB = static_cast<long long>(-(std::numeric_limits<TargetType>::max)());
                    }
                    paramDesc[target_id].typeMinBound = minB;
                    paramDesc[target_id].typeMaxBound = maxB;

                    paramDesc[target_id].stringUpdater = [](std::any& targetAny, const std::string& textValue) {
                        std::stringstream ss(textValue);
                        TargetType parsedValue;
                        if (ss >> parsedValue) {
                            targetAny = parsedValue;
                        }
                        };
                }
                // ВЕТКА В: Автоматическая генерация текстового парсера для пользовательских STRUCT / CLASS
                else {
                    paramDesc[target_id].stringUpdater = [](std::any& targetAny, const std::string& textValue) {
                        size_t openBrace = textValue.find('{');
                        size_t closeBrace = textValue.rfind('}');
                        if (openBrace == std::string::npos || closeBrace == std::string::npos || closeBrace <= openBrace) {
                            return;
                        }

                        std::string innerArgs = textValue.substr(openBrace + 1, closeBrace - openBrace - 1);

                        static std::vector<std::string> fNames;
                        static std::vector<DWORD> fOffsets;
                        static std::vector<DWORD> fSizes;
                        static std::vector<std::string> fTypes;
                        static bool metadataCached = false;

                        if (!metadataCached) {
                            std::string typeNameAnsi = textValue.substr(0, openBrace);
                            typeNameAnsi.erase(0, typeNameAnsi.find_first_not_of(" \t\r\n"));
                            typeNameAnsi.erase(typeNameAnsi.find_last_not_of(" \t\r\n") + 1);

                            std::wstring wTypeName(typeNameAnsi.begin(), typeNameAnsi.end());
                            LoadStructMetadataDirect(wTypeName.c_str(), fNames, fOffsets, fSizes, fTypes);
                            metadataCached = true;
                        }

                        if (fOffsets.empty()) return;

                        std::stringstream ss(innerArgs);
                        std::string token;
                        size_t fieldIndex = 0;

                        TargetType* pStructInstance = std::any_cast<TargetType>(&targetAny);
                        if (!pStructInstance) return;
                        char* byteBase = reinterpret_cast<char*>(pStructInstance);

                        while (std::getline(ss, token, ',') && fieldIndex < fOffsets.size()) {
                            token.erase(0, token.find_first_not_of(" \t\r\n"));
                            token.erase(token.find_last_not_of(" \t\r\n") + 1);

                            if (!token.empty()) {
                                // Отрезаем ".имя =" или "имя:", если они есть перед значением
                                size_t eqPos = token.find('=');
                                if (eqPos == std::string::npos) eqPos = token.find(':');
                                if (eqPos != std::string::npos) {
                                    token = token.substr(eqPos + 1);
                                    token.erase(0, token.find_first_not_of(" \t\r\n"));
                                    token.erase(token.find_last_not_of(" \t\r\n") + 1);
                                }

                                DWORD offset = fOffsets[fieldIndex];
                                std::string type = fTypes[fieldIndex];
                                char* fieldAddress = byteBase + offset;

                                if (type == "char" || type == "unsigned char" || type == "signed char") {
                                    *reinterpret_cast<unsigned char*>(fieldAddress) = static_cast<unsigned char>(std::stoi(token));
                                }
                                else if (type == "int" || type == "unsigned int") {
                                    *reinterpret_cast<int*>(fieldAddress) = std::stoi(token);
                                }
                                else if (type == "float") {
                                    *reinterpret_cast<float*>(fieldAddress) = std::stof(token);
                                }
                                else if (type == "double") {
                                    *reinterpret_cast<double*>(fieldAddress) = std::stod(token);
                                }
                                else if (type == "bool") {
                                    *reinterpret_cast<bool*>(fieldAddress) = (token == "true" || token == "1");
                                }
                            }
                            fieldIndex++;
                        }
                        };
                }




                paramDesc[target_id].loaded = true;
            }

            std::string absPath = NormalizePath(AbsoluteFile.c_str());
            int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));
            if (real_id < 0 || real_id >= static_cast<int>(paramDesc.size())) {
                return static_cast<TargetType>(literalValue);
            }

            // Проводник значений обратно в игровой цикл (вызывается на каждом кадре)
            if constexpr (std::is_enum_v<TargetType>) {
                if (auto pVal = std::any_cast<int>(&paramDesc[real_id].value)) {
                    return static_cast<TargetType>(*pVal);
                }
            }
            else {
                if (auto pVal = std::any_cast<TargetType>(&paramDesc[real_id].value)) {
                    return *pVal;
                }
            }

            return static_cast<TargetType>(literalValue);
        }
    };


    template <typename LiteralType, FixedString<260> AbsoluteFile, int Line, int Column>
    struct LazyTypeDetector {
        LiteralType rawValue;

        constexpr LazyTypeDetector(LiteralType val) : rawValue(val) {}

        template <typename TargetType>
        inline operator TargetType() const {
            if constexpr (std::is_enum_v<LiteralType>) {
                return static_cast<TargetType>(EvalSyntaxShield<LiteralType, AbsoluteFile, Line, Column>::Get(rawValue));
            }
            else {
                return EvalSyntaxShield<TargetType, AbsoluteFile, Line, Column>::Get(rawValue);
            }
        }

        inline operator LiteralType() const {
            return EvalSyntaxShield<LiteralType, AbsoluteFile, Line, Column>::Get(rawValue);
        }

        template <typename TTarget>
            requires (std::is_floating_point_v<TTarget> && !std::is_same_v<TTarget, LiteralType>)
        inline operator TTarget() const {
            return static_cast<TTarget>(operator LiteralType());
        }
    };



    }

    // 3. Обновленный ультра-чистый макрос, пробрасывающий decltype(value) в шаблон детектора
// ИСПРАВЛЕННЫЙ ВАРИАТИВНЫЙ МАКРОС: Автоматически съедает запятые структур агрегатной инициализации!
#define eval(...) \
    LivePT::LazyTypeDetector< \
        std::decay_t<decltype(__VA_ARGS__)>, \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(__VA_ARGS__)

