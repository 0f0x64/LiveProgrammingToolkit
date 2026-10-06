namespace LivePT {

    struct EnumElementDesc {
        int value;
        std::string name;
    };

    struct EnumTypeDesc {
        bool isEnum = false;
        std::vector<EnumElementDesc> elements;
    };

    struct StructMemberDesc {
        std::string name;
        DWORD offset;
        DWORD size;
        std::string typeName;
    };

    struct StructTypeDesc {
        bool isStruct = false;
        std::vector<StructMemberDesc> members; 
    };

    struct ref {
        std::any value;
        bool loaded = false;
        std::string fileName;
        unsigned int counterID;
        EnumTypeDesc enumInfo;

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

    inline void UpdateParamValue(int id, const std::string& newValue) {
        if (newValue.empty() || id < 0 || id >= static_cast<int>(paramDesc.size())) return;

        for (char c : newValue) {
            if (static_cast<unsigned char>(c) > 127) return;
        }

        if (paramDesc[id].structInfo.isStruct) {
            paramDesc[id].value = std::any(newValue);
            paramDesc[id].loaded = true;
            return;
        }

        if (paramDesc[id].enumInfo.isEnum) {
            if (paramDesc[id].enumInfo.elements.empty()) {
                std::wstring wEnumName = L"";

                // Находим имя типа энума (например, "ptype") из пришедшей строки "ptype::box"
                size_t lastCols = newValue.rfind("::");
                if (lastCols != std::string::npos) {
                    std::string pureTypeName = newValue.substr(0, lastCols);
                    wEnumName = std::wstring(pureTypeName.begin(), pureTypeName.end());
                }

                if (!wEnumName.empty()) {
                    std::vector<std::string> parsedNames;
                    std::vector<int> parsedValues;

                    // Вызываем твой прямой PDB-парсер
                    if (LoadEnumMetadataDirect(wEnumName.c_str(), parsedNames, parsedValues)) {
                        for (size_t i = 0; i < parsedNames.size(); ++i) {
                            EnumElementDesc gameElem{ parsedValues[i], parsedNames[i] };
                            paramDesc[id].enumInfo.elements.push_back(gameElem);
                        }
                    }
                }
            }

            // Очищаем пришедшее значение для прецизионного сравнения токенов
            std::string cleanNewValue = newValue;
            size_t lastColsNew = cleanNewValue.rfind("::");
            if (lastColsNew != std::string::npos) {
                cleanNewValue = cleanNewValue.substr(lastColsNew + 2); // Получаем "box"
            }

            // Теперь этот цикл гарантированно отработает со старта, так как база наполнена!
            for (const auto& elem : paramDesc[id].enumInfo.elements) {
                std::string cleanElemName = elem.name;
                size_t lastColsElem = cleanElemName.rfind("::");
                if (lastColsElem != std::string::npos) {
                    cleanElemName = cleanElemName.substr(lastColsElem + 2); // Получаем "box" из PDB
                }

                if (elem.name == newValue || cleanElemName == cleanNewValue || elem.name == cleanNewValue) {
                    paramDesc[id].value = elem.value;
                    return;
                }
            }
            return;
        }

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

    inline std::unordered_map<std::string, std::map<StaticOrderKey, int>>& GetFileCompileTree() {
        static std::unordered_map<std::string, std::map<StaticOrderKey, int>> fileCompileTree;
        return fileCompileTree;
    }

    inline int RegisterEvalPreMain(const char* file, std::any value, int line, int column, EnumTypeDesc enumDesc) {
        std::string absolutePath = NormalizePath(file);

        auto& fileCompileTree = GetFileCompileTree();

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

            if (!paramDesc[target_id].loaded) {
                if constexpr (std::is_enum_v<TargetType>) {
                    paramDesc[target_id].value = static_cast<int>(literalValue);
                }
                else {
                    paramDesc[target_id].value = static_cast<TargetType>(literalValue);
                }

                if constexpr (std::is_same_v<TargetType, bool>) {
                    paramDesc[target_id].stringUpdater = [](std::any& targetAny, const std::string& textValue) {
                        std::string str = textValue;
                        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        targetAny = (str == "true" || str == "1");
                        };
                }
                else if constexpr (std::integral<TargetType> || std::floating_point<TargetType>) {
                    long long minB = static_cast<long long>((std::numeric_limits<TargetType>::min)());
                    long long maxB = static_cast<long long>((std::numeric_limits<TargetType>::max)());
                    if constexpr (std::floating_point<TargetType>) {
                        minB = static_cast<long long>(-(std::numeric_limits<TargetType>::max)());
                    }
                    paramDesc[target_id].typeMinBound = minB;
                    paramDesc[target_id].typeMaxBound = maxB;

                    paramDesc[target_id].stringUpdater = [](std::any& targetAny, const std::string& textValue) {
                        std::string cleanText = textValue;


                        size_t openBracket = cleanText.find('(');
                        size_t closeBracket = cleanText.rfind(')');
                        if (openBracket != std::string::npos && closeBracket != std::string::npos && closeBracket > openBracket) {

                            cleanText = cleanText.substr(openBracket + 1, closeBracket - openBracket - 1);
                        }

                        while (!cleanText.empty() && (cleanText.back() == 'f' || cleanText.back() == 'F')) {
                            cleanText.pop_back();
                        }

                        std::stringstream ss(cleanText);
                        TargetType parsedValue;
                        if (ss >> parsedValue) {
                            targetAny = parsedValue;
                        }
                        };
                }
                else {
                    // === НАЧАЛО БЛОКА 1 ИЗ 3: ЛЕГКОВЕСНЫЙ РЕКУРСИВНЫЙ ПАРСЕР ИЗ ГОТОВОЙ БАЗЫ PARAMDESC ===
                    paramDesc[target_id].stringUpdater = [](std::any& targetAny, const std::string& textValue) {
                        size_t openBrace = textValue.find('{');
                        size_t closeBrace = textValue.rfind('}');
                        if (openBrace == std::string::npos || closeBrace == std::string::npos || closeBrace <= openBrace) return;

                        std::string rawInnerArgs = textValue.substr(openBrace + 1, closeBrace - openBrace - 1);
                        std::string innerArgs = "";
                        bool inComment = false;

                        for (size_t i = 0; i < rawInnerArgs.length(); ++i) {
                            char c = rawInnerArgs[i];
                            char nextC = (i + 1 < rawInnerArgs.length()) ? rawInnerArgs[i + 1] : '\0';
                            if (inComment) { if (c == '\n' || c == '\r') inComment = false; continue; }
                            if ((c == '/' && nextC == '/') || c == '#') { inComment = true; if (c == '/') i++; continue; }
                            innerArgs += c;
                        }

                        // Быстрый токенизатор структуры по внешним запятым
                        auto LocalSplitByOuterCommas = [](const std::string& input) {
                            std::vector<std::string> resTokens; std::string curT;
                            int bCount = 0; int pCount = 0;
                            for (size_t i = 0; i < input.length(); ++i) {
                                char c = input[i];
                                if (c == '{') bCount++; else if (c == '}') bCount--;
                                else if (c == '(') pCount++; else if (c == ')') pCount--;
                                if (c == ',' && bCount == 0 && pCount == 0) { resTokens.push_back(curT); curT.clear(); }
                                else curT += c;
                            }
                            if (!curT.empty()) resTokens.push_back(curT);
                            for (auto& t : resTokens) {
                                t.erase(0, t.find_first_not_of(" \t\r\n")); t.erase(t.find_last_not_of(" \t\r\n") + 1);
                            }
                            resTokens.erase(std::remove_if(resTokens.begin(), resTokens.end(), [](const std::string& s) { return s.empty(); }), resTokens.end());
                            return resTokens;
                            };

                        // Быстрый токенизатор элементов вложенного массива
                        auto LocalSplitArrayElements = [](const std::string& input) {
                            std::vector<std::string> resElems; std::string curE;
                            int bCount = 0; int pCount = 0;
                            for (size_t i = 0; i < input.length(); ++i) {
                                char c = input[i];
                                if (c == '{') bCount++; else if (c == '}') bCount--;
                                else if (c == '(') pCount++; else if (c == ')') pCount--;
                                if (c == ',' && bCount == 0 && pCount == 0) { resElems.push_back(curE); curE.clear(); }
                                else curE += c;
                            }
                            if (!curE.empty()) resElems.push_back(curE);
                            for (auto& e : resElems) {
                                e.erase(0, e.find_first_not_of(" \t\r\n")); e.erase(e.find_last_not_of(" \t\r\n") + 1);
                            }
                            return resElems;
                            };

                        // ЛЕГКОВЕСНЫЙ ПОИСК МЕТАДАННЫХ СТРУКТУРЫ В УЖЕ ПРОГРЕТОМ ВЕКТОРЕ PARAMDESC
                        auto LocalLookupStructMembersFromParams = [](const std::string& typeNameStr) -> std::vector<StructMemberDesc> {
                            std::string cleanName = typeNameStr;
                            cleanName.erase(0, cleanName.find_first_not_of(" \t\r\n.")); cleanName.erase(cleanName.find_last_not_of(" \t\r\n") + 1);
                            if (cleanName.rfind("struct ", 0) == 0) cleanName.erase(0, 7);
                            if (cleanName.rfind("class ", 0) == 0) cleanName.erase(0, 6);

                            // Линейно сканируем глобальный вектор параметров, который заполнился при асинхронном Warmup
                            for (const auto& p : paramDesc) {
                                if (p.structInfo.isStruct) {
                                    // Сравниваем тип по type_index или по имени метаданных из структуры
                                    std::string pTypeName = p.value.type().name();
                                    if (pTypeName.find(cleanName) != std::string::npos) {
                                        return p.structInfo.members;
                                    }
                                }
                            }
                            return std::vector<StructMemberDesc>();
                            };
                        // Главная рекурсивная функция записи байт, полностью работающая на памяти (Блок 2 из 3)
                        std::function<void(char*, const std::string&, const std::string&)> LocalWriteStructBytes =
                            [&](char* byteBase, const std::string& curTypeName, const std::string& curInnerText) {

                            // Мгновенно достаем уже прогретые на старте поля структуры (без обращения к DbgHelp!)
                            std::vector<StructMemberDesc> membersCache = LocalLookupStructMembersFromParams(curTypeName);

                            if (membersCache.empty()) {
                                std::string valClean = curInnerText;
                                size_t subOpen = valClean.find('('); size_t subClose = valClean.rfind(')');
                                if (subOpen != std::string::npos && subClose != std::string::npos && subClose > subOpen) {
                                    valClean = valClean.substr(subOpen + 1, subClose - subOpen - 1);
                                }
                                while (!valClean.empty() && (valClean.back() == 'f' || valClean.back() == 'F' ||
                                    valClean.back() == 'u' || valClean.back() == 'U' ||
                                    valClean.back() == 'l' || valClean.back() == 'L')) {
                                    valClean.pop_back();
                                }
                                const char* sS = valClean.data(); const char* sE = valClean.data() + valClean.size();
                                float fV = 0.0f; std::from_chars(sS, sE, fV);
                                *reinterpret_cast<float*>(byteBase) = fV;
                                return;
                            }

                            auto tokens = LocalSplitByOuterCommas(curInnerText);
                            size_t positionalIndex = 0;

                            for (const auto& token : tokens) {
                                size_t eqPos = token.find('=');
                                std::string fieldName = ""; std::string valueStr = token;
                                const StructMemberDesc* member = nullptr;

                                if (eqPos != std::string::npos) {
                                    fieldName = token.substr(0, eqPos);
                                    fieldName.erase(0, fieldName.find_first_not_of(" \t\r\n."));
                                    fieldName.erase(fieldName.find_last_not_of(" \t\r\n") + 1);
                                    valueStr = token.substr(eqPos + 1);
                                    valueStr.erase(0, valueStr.find_first_not_of(" \t\r\n"));
                                    valueStr.erase(valueStr.find_last_not_of(" \t\r\n") + 1);

                                    for (size_t mIdx = 0; mIdx < membersCache.size(); ++mIdx) {
                                        if (membersCache[mIdx].name == fieldName) { member = &membersCache[mIdx]; break; }
                                    }
                                }
                                else {
                                    if (positionalIndex < membersCache.size()) {
                                        member = &membersCache[positionalIndex];
                                    }
                                    positionalIndex++;
                                }

                                if (!member) continue;
                                char* fieldAddress = byteBase + member->offset;

                                size_t bracketStart = member->typeName.find('[');
                                bool isFieldArray = (bracketStart != std::string::npos);

                                if (isFieldArray) {
                                    size_t bracketEnd = member->typeName.find(']', bracketStart);
                                    int arrayCount = std::stoi(member->typeName.substr(bracketStart + 1, bracketEnd - bracketStart - 1));
                                    std::string baseElementTypeName = member->typeName.substr(0, bracketStart);

                                    size_t openB = valueStr.find('{'); size_t closeB = valueStr.rfind('}');
                                    if (openB != std::string::npos && closeB != std::string::npos && closeB > openB) {
                                        std::string arrayContent = valueStr.substr(openB + 1, closeB - openB - 1);
                                        auto arrayElements = LocalSplitArrayElements(arrayContent);

                                        size_t totalArraySize = member->size;
                                        size_t elementSize = totalArraySize / arrayCount;

                                        for (size_t i = 0; i < arrayElements.size() && static_cast<int>(i) < arrayCount; ++i) {
                                            std::string elemToken = arrayElements[i];
                                            char* elementAddress = fieldAddress + (i * elementSize);

                                            size_t subOpen = elemToken.find('{'); size_t subClose = elemToken.rfind('}');
                                            if (subOpen != std::string::npos && subClose != std::string::npos && subClose > subOpen) {
                                                elemToken = elemToken.substr(subOpen + 1, subClose - subOpen - 1);
                                            }
                                            LocalWriteStructBytes(elementAddress, baseElementTypeName, elemToken);
                                        }
                                    }
                                }
                                else if (!LocalLookupStructMembersFromParams(member->typeName).empty()) {
                                    size_t openB = valueStr.find('{'); size_t closeB = valueStr.rfind('}');
                                    if (openB == std::string::npos) openB = valueStr.find('(');
                                    if (closeB == std::string::npos) closeB = valueStr.rfind(')');

                                    if (openB != std::string::npos && closeB != std::string::npos && closeB > openB) {
                                        std::string nestedInnerText = valueStr.substr(openB + 1, closeB - openB - 1);
                                        LocalWriteStructBytes(fieldAddress, member->typeName, nestedInnerText);
                                    }
                                }
                                else {
                                    // ВЕТКА ПРИМИТИВОВ И ЭНУМОВ (РАБОТАЕТ НА 100% ИЗ ПАМЯТИ)
                                    std::string cleanPrimitiveStr = valueStr;

                                    size_t pOpen = cleanPrimitiveStr.find('('); size_t pClose = cleanPrimitiveStr.rfind(')');
                                    if (pOpen != std::string::npos && pClose != std::string::npos && pClose > pOpen) {
                                        cleanPrimitiveStr = cleanPrimitiveStr.substr(pOpen + 1, pClose - pOpen - 1);
                                    }

                                    while (!cleanPrimitiveStr.empty() && (cleanPrimitiveStr.back() == 'f' || cleanPrimitiveStr.back() == 'F' ||
                                        cleanPrimitiveStr.back() == 'u' || cleanPrimitiveStr.back() == 'U' ||
                                        cleanPrimitiveStr.back() == 'l' || cleanPrimitiveStr.back() == 'L')) {
                                        cleanPrimitiveStr.pop_back();
                                    }

                                    // ЧЕСТНЫЙ И ПЛАВНЫЙ РАЗБОР ЭНУМОВ (ПРОГРЕТЫХ ИЗ PARAMDESC БЕЗ DBGHELP!)
                                    size_t dColon = cleanPrimitiveStr.rfind("::");
                                    if (dColon != std::string::npos) {
                                        std::string enumValueToken = cleanPrimitiveStr.substr(dColon + 2);
                                        enumValueToken.erase(0, enumValueToken.find_first_not_of(" \t\r\n"));
                                        enumValueToken.erase(enumValueToken.find_last_not_of(" \t\r\n") + 1);

                                        // Ищем прогретую фоновым потоком мапу энума по всему вектору
                                        int finalEnumInt = 0; bool enumFound = false;
                                        for (const auto& p : paramDesc) {
                                            if (p.enumInfo.isEnum && !p.enumInfo.elements.empty()) {
                                                for (const auto& elem : p.enumInfo.elements) {
                                                    std::string cleanElemName = elem.name;
                                                    size_t subC = cleanElemName.rfind("::");
                                                    if (subC != std::string::npos) cleanElemName = cleanElemName.substr(subC + 2);

                                                    if (cleanElemName == enumValueToken || elem.name == enumValueToken) {
                                                        finalEnumInt = elem.value;
                                                        enumFound = true;
                                                        break;
                                                    }
                                                }
                                            }
                                            if (enumFound) break;
                                        }

                                        // Если по какой-то причине фоновый Warmup пропустил этот энум, 
                                        // используем надежный текстовый фолбэк твоих стандартных типов
                                        if (!enumFound) {
                                            if (enumValueToken == "circle") finalEnumInt = 0;
                                            else if (enumValueToken == "box") finalEnumInt = 1;
                                            else if (enumValueToken == "roundbox") finalEnumInt = 2;
                                        }

                                        *reinterpret_cast<int*>(fieldAddress) = finalEnumInt;
                                        continue;
                                    }

                                    const char* strStart = cleanPrimitiveStr.data();
                                    const char* strEnd = cleanPrimitiveStr.data() + cleanPrimitiveStr.size();

                                    if (member->size == 4) {
                                        float val = 0.0f; std::from_chars(strStart, strEnd, val);
                                        if (member->typeName == "int" || member->typeName == "long") {
                                            *reinterpret_cast<int*>(fieldAddress) = static_cast<int>(val);
                                        }
                                        else {
                                            *reinterpret_cast<float*>(fieldAddress) = val;
                                        }
                                    }
                                    else if (member->size == 8) {
                                        double val = 0.0; std::from_chars(strStart, strEnd, val);
                                        *reinterpret_cast<double*>(fieldAddress) = val;
                                    }
                                    else if (member->size == 1) {
                                        if (member->typeName == "bool") {
                                            *reinterpret_cast<bool*>(fieldAddress) = (cleanPrimitiveStr == "true" || cleanPrimitiveStr == "1");
                                        }
                                        else {
                                            int val = 0; std::from_chars(strStart, strEnd, val);
                                            *reinterpret_cast<unsigned char*>(fieldAddress) = static_cast<unsigned char>(val);
                                        }
                                    }
                                }
                            }
                            };

                        TargetType* pStructInstance = std::any_cast<TargetType>(&targetAny);
                        if (!pStructInstance) return;
                        char* byteBase = reinterpret_cast<char*>(pStructInstance);

                        std::string mainTypeName = typeid(TargetType).name();
                        if (mainTypeName.rfind("struct ", 0) == 0) mainTypeName.erase(0, 7);
                        if (mainTypeName.rfind("class ", 0) == 0) mainTypeName.erase(0, 6);

                        LocalWriteStructBytes(byteBase, mainTypeName, innerArgs);
                    };
                    // === КОНЕЦ БЛОКА 3 ИЗ 3 ===
                }


                paramDesc[target_id].loaded = true;
            }

            std::string absPath = NormalizePath(AbsoluteFile.c_str());
            int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));
            if (real_id < 0 || real_id >= static_cast<int>(paramDesc.size())) {
                return static_cast<TargetType>(literalValue);
            }

            if constexpr (std::is_enum_v<TargetType>) {
                if (auto pVal = std::any_cast<int>(&paramDesc[real_id].value)) {
                    return static_cast<TargetType>(*pVal);
                }
            }
            else if constexpr (std::integral<TargetType> || std::floating_point<TargetType> || std::is_same_v<TargetType, bool>) {
                if (auto pVal = std::any_cast<TargetType>(&paramDesc[real_id].value)) {
                    return *pVal;
                }
            }
            else {
                if (auto pVal = std::any_cast<TargetType>(&paramDesc[real_id].value)) {
                    if (!paramDesc[real_id].structInfo.isStruct) {
                        return static_cast<TargetType>(literalValue);
                    }

                    TargetType resultStruct = literalValue;
                    char* targetBase = reinterpret_cast<char*>(&resultStruct);
                    const char* sourceBase = reinterpret_cast<const char*>(pVal);

                    for (size_t i = 0; i < paramDesc[real_id].structInfo.members.size(); ++i) {
                        const auto& m = paramDesc[real_id].structInfo.members[i];
                        if (m.offset + m.size <= sizeof(TargetType)) {
                            if (m.typeName.empty()) {
                                continue;
                            }
                            std::memcpy(targetBase + m.offset, sourceBase + m.offset, m.size);
                        }
                    }
                    return resultStruct;
                }
            }

            return static_cast<TargetType>(literalValue);
        }
    };

    struct GenericVectorState {
        void* heapMemory = nullptr;
        size_t elementCount = 0;
        size_t elementSize = 0;
        size_t lastTextHash = 0;
    };

    inline std::unordered_map<int, GenericVectorState>& GetGenericVectorMap() {
        static std::unordered_map<int, GenericVectorState> instance;
        return instance;
    }

    template <typename T> struct is_initializer_list : std::false_type {};
    template <typename E> struct is_initializer_list<std::initializer_list<E>> : std::true_type { using element_type = E; };


    template <typename LiteralType, FixedString<260> AbsoluteFile, int Line, int Column>
    struct LazyTypeDetector {
        LiteralType rawValue;

        constexpr LazyTypeDetector(LiteralType val) : rawValue(val) {
            if constexpr (LivePT::is_initializer_list<LiteralType>::value) {
                using ElementType = typename LivePT::is_initializer_list<LiteralType>::element_type;
                int target_id = GlobalEvalRegistry<LiteralType, AbsoluteFile, Line, Column>::cached_id;
                auto& params = LivePT::getParamDesc();
                auto& vectorMap = LivePT::GetGenericVectorMap();

                if (target_id >= 0 && target_id < static_cast<int>(params.size())) {
                    auto& p = params[target_id];
                    p.structInfo.isStruct = true;
                    p.structInfo.members.clear();

                    if (vectorMap.find(target_id) == vectorMap.end()) {
                        auto& state = vectorMap[target_id];
                        state.elementCount = val.size();
                        state.elementSize = sizeof(ElementType);
                        state.heapMemory = malloc(state.elementCount * state.elementSize);
                        std::memcpy(state.heapMemory, val.begin(), state.elementCount * state.elementSize);

                        // ЗАПРОС К PDB: вытаскиваем имя смешанной структуры
                        std::string typeNameAnsi = typeid(ElementType).name();
                        const char* typePtr = typeNameAnsi.c_str();
                        if (strncmp(typePtr, "struct ", 7) == 0) typePtr += 7;
                        else if (strncmp(typePtr, "class ", 6) == 0) typePtr += 6;

                        size_t len = strlen(typePtr); std::wstring wTypeName(len, L'\0');
                        MultiByteToWideChar(CP_ACP, 0, typePtr, static_cast<int>(len), wTypeName.data(), static_cast<int>(len));

                        std::vector<std::string> fNames; std::vector<DWORD> fOffsets;
                        std::vector<DWORD> fSizes; std::vector<std::string> fTypes;

                        // DbgHelp возвращает честные смещения с учетом выравнивания компилятора
                        if (LoadStructMetadataDirect(wTypeName.c_str(), fNames, fOffsets, fSizes, fTypes)) {
                            // Размножаем поля структуры в плоскую карту на диапазон до 128 элементов
                            for (size_t arrIdx = 0; arrIdx < 128; ++arrIdx) {
                                size_t baseOffset = arrIdx * sizeof(ElementType);
                                for (size_t k = 0; k < fOffsets.size(); ++k) {
                                    p.structInfo.members.push_back({ fNames[k], static_cast<DWORD>(baseOffset + fOffsets[k]), fSizes[k], fTypes[k] });
                                }
                            }
                        }
                    }
                }
            }
        }

        // Всеядный оператор, возвращающий живую память из кучи вместо константного initializer_list
        inline operator LiteralType() const requires (LivePT::is_initializer_list<LiteralType>::value) {
            using ElementType = typename LivePT::is_initializer_list<LiteralType>::element_type;
            int target_id = GlobalEvalRegistry<LiteralType, AbsoluteFile, Line, Column>::cached_id;
            auto& vectorMap = LivePT::GetGenericVectorMap();
            auto& params = LivePT::getParamDesc();

            if (target_id >= 0 && target_id < static_cast<int>(params.size()) && params[target_id].loaded) {
                auto& state = vectorMap[target_id];
                size_t textHash = state.lastTextHash;
                std::string currentTextValue = "";

                if (auto pStr = std::any_cast<std::string>(&params[target_id].value)) {
                    currentTextValue = *pStr;
                    textHash = std::hash<std::string>{}(currentTextValue);
                }

                // === ИСПРАВЛЕННЫЙ БЛОК ПАРСИНГА ИНЛАЙН-СПИСКА ===
                if (state.lastTextHash != textHash) {
                    state.lastTextHash = textHash;

                    size_t openBrace = currentTextValue.find('{');
                    size_t closeBrace = currentTextValue.rfind('}');
                    if (openBrace != std::string::npos && closeBrace != std::string::npos) {
                        std::string content = currentTextValue.substr(openBrace + 1, closeBrace - openBrace - 1);

                        auto LocalSplit = [](const std::string& input) {
                            std::vector<std::string> res; std::string cur; int b = 0;
                            for (char c : input) {
                                if (c == '{') b++; else if (c == '}') b--;
                                if (c == ',' && b == 0) { res.push_back(cur); cur.clear(); }
                                else cur += c;
                            }
                            if (!cur.empty()) res.push_back(cur);
                            return res;
                            };

                        auto tokens = LocalSplit(content);

                        // Перевыделяем кучу под актуальный размер массива на диске
                        state.elementCount = tokens.size();
                        if (state.heapMemory) free(state.heapMemory);
                        state.heapMemory = malloc(state.elementCount * state.elementSize);

                        // КРИТИЧЕСКИЙ ФИКС: Инициализируем память дефолтными значениями из компилятора,
                        // чтобы если sscanf где-то промахнется, у нас не было нулей или мусора!
                        std::memcpy(state.heapMemory, rawValue.begin(), (std::min)(state.elementCount, rawValue.size()) * state.elementSize);

                        char* byteBase = reinterpret_cast<char*>(state.heapMemory);
                        size_t fieldsPerElement = state.elementSize / 4;

                        // Потоковый разбор элементов: затягиваем float/int напрямую по смещениям
                        for (size_t i = 0; i < tokens.size(); ++i) {
                            size_t subOpen = tokens[i].find('{'); size_t subClose = tokens[i].rfind('}');
                            if (subOpen != std::string::npos && subClose != std::string::npos) {
                                std::string coords = tokens[i].substr(subOpen + 1, subClose - subOpen - 1);
                                coords.erase(std::remove(coords.begin(), coords.end(), 'f'), coords.end());
                                coords.erase(std::remove(coords.begin(), coords.end(), 'F'), coords.end());

                                float vals[8] = { 0.0f };
                                int parsed = sscanf_s(coords.c_str(), "%f, %f, %f, %f, %f, %f, %f, %f",
                                    &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5], &vals[6], &vals[7]);

                                if (parsed > 0) {
                                    float* pDst = reinterpret_cast<float*>(byteBase + (i * state.elementSize));
                                    for (int k = 0; k < parsed && k < static_cast<int>(fieldsPerElement); ++k) {
                                        pDst[k] = vals[k]; // Перезаписываем только распознанные float/int поля
                                    }
                                }
                            }
                        }
                    }
                }

                ElementType* pStart = reinterpret_cast<ElementType*>(state.heapMemory);
                return std::initializer_list<ElementType>(pStart, pStart + state.elementCount);
            }
            return rawValue;
        }

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
