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

        // КРИТИЧЕСКИЙ ФИКС: Храним маску — какие поля структуры в IDE являются ЧИСЛАМИ
        // Индекс в векторе соответствует индексу поля в structInfo.members
        std::vector<bool> validFieldsMask;

        long long typeMinBound = 0;
        long long typeMaxBound = 0;
        int line = 0;
        int column = 0;
        std::function<void(std::any& targetAny, const std::string& textValue)> stringUpdater = nullptr;
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
                    paramDesc[target_id].stringUpdater = [target_id](std::any& targetAny, const std::string& textValue) {
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

                    paramDesc[target_id].stringUpdater = [target_id](std::any& targetAny, const std::string& textValue) {
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
                    // === ЧАСТЬ 1: РЕКУРСИВНЫЙ ПАРСЕР С АБСОЛЮТНЫМИ ОФСЕТАМИ ===
                    paramDesc[target_id].stringUpdater = [target_id](std::any& targetAny, const std::string& textValue) {
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

                        auto LocalLookupStructMembersFromParams = [](const std::string& typeNameStr) -> std::vector<StructMemberDesc> {
                            std::string cleanName = typeNameStr;
                            cleanName.erase(0, cleanName.find_first_not_of(" \t\r\n.")); cleanName.erase(cleanName.find_last_not_of(" \t\r\n") + 1);
                            if (cleanName.rfind("struct ", 0) == 0) cleanName.erase(0, 7);
                            if (cleanName.rfind("class ", 0) == 0) cleanName.erase(0, 6);

                            for (const auto& p : paramDesc) {
                                if (p.structInfo.isStruct) {
                                    std::string pTypeName = p.value.type().name();
                                    if (pTypeName.find(cleanName) != std::string::npos) {
                                        return p.structInfo.members;
                                    }
                                }
                            }
                            return std::vector<StructMemberDesc>();
                            };

                        std::function<void(char*, const std::string&, const std::string&, DWORD)> LocalWriteStructBytes =
                            [&](char* byteBase, const std::string& curTypeName, const std::string& curInnerText, DWORD absoluteBaseOffset) {

                            std::vector<StructMemberDesc> membersCache = LocalLookupStructMembersFromParams(curTypeName);

                            if (membersCache.empty()) {
                                std::string valClean = curInnerText;
                                size_t subOpen = valClean.find('('); size_t subClose = valClean.rfind(')');
                                if (subOpen != std::string::npos && subClose != std::string::npos && subClose > subOpen) {
                                    valClean = valClean.substr(subOpen + 1, subClose - subOpen - 1);
                                }
                                while (!valClean.empty() && (valClean.back() == 'f' || valClean.back() == 'F' || valClean.back() == 'u' || valClean.back() == 'U')) {
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

                                DWORD currentAbsoluteFieldOffset = absoluteBaseOffset + member->offset;
                                size_t structTotalSize = sizeof(TargetType);

                                if (paramDesc[target_id].validFieldsMask.size() < structTotalSize) {
                                    paramDesc[target_id].validFieldsMask.resize(structTotalSize, false);
                                }

                                if (!LocalLookupStructMembersFromParams(member->typeName).empty()) {
                                    size_t openB = valueStr.find('{'); size_t closeB = valueStr.rfind('}');
                                    if (openB == std::string::npos) openB = valueStr.find('(');
                                    if (closeB == std::string::npos) closeB = valueStr.rfind(')');

                                    if (openB != std::string::npos && closeB != std::string::npos && closeB > openB) {
                                        std::string nestedInnerText = valueStr.substr(openB + 1, closeB - openB - 1);
                                        LocalWriteStructBytes(fieldAddress, member->typeName, nestedInnerText, currentAbsoluteFieldOffset);
                                    }
                                }
                                else {
                                    // === ЧАСТЬ 2: РАЗБОР ТИПОВ И ЗАПОЛНЕНИЕ БАЙТОВОЙ МАСКИ ===
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

                                    size_t dColon = cleanPrimitiveStr.rfind("::");
                                    if (dColon != std::string::npos) {
                                        std::string enumValueToken = cleanPrimitiveStr.substr(dColon + 2);
                                        enumValueToken.erase(0, enumValueToken.find_first_not_of(" \t\r\n"));
                                        enumValueToken.erase(enumValueToken.find_last_not_of(" \t\r\n") + 1);

                                        int finalEnumInt = 0; bool enumFound = false;
                                        for (const auto& p : paramDesc) {
                                            if (p.enumInfo.isEnum && !p.enumInfo.elements.empty()) {
                                                for (const auto& elem : p.enumInfo.elements) {
                                                    std::string cleanElemName = elem.name;
                                                    size_t subC = cleanElemName.rfind("::");
                                                    if (subC != std::string::npos) cleanElemName = cleanElemName.substr(subC + 2);

                                                    if (cleanElemName == enumValueToken || elem.name == enumValueToken) {
                                                        finalEnumInt = elem.value; enumFound = true; break;
                                                    }
                                                }
                                            }
                                            if (enumFound) break;
                                        }

                                        if (!enumFound) {
                                            if (enumValueToken == "circle") finalEnumInt = 0;
                                            else if (enumValueToken == "box") finalEnumInt = 1;
                                            else if (enumValueToken == "roundbox") finalEnumInt = 2;
                                        }

                                        *reinterpret_cast<int*>(fieldAddress) = finalEnumInt;

                                        for (DWORD b = 0; b < member->size; ++b) {
                                            if (currentAbsoluteFieldOffset + b < structTotalSize) {
                                                paramDesc[target_id].validFieldsMask[currentAbsoluteFieldOffset + b] = true;
                                            }
                                        }
                                        continue;
                                    }

                                    const char* strStart = cleanPrimitiveStr.data();
                                    const char* strEnd = cleanPrimitiveStr.data() + cleanPrimitiveStr.size();

                                    if (member->size == 4) {
                                        float val = 0.0f;
                                        auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                                        bool isConstant = (ec == std::errc{});

                                        if (isConstant) {
                                            if (member->typeName == "int" || member->typeName == "long") {
                                                *reinterpret_cast<int*>(fieldAddress) = static_cast<int>(val);
                                            }
                                            else {
                                                *reinterpret_cast<float*>(fieldAddress) = val;
                                            }
                                        }

                                        for (DWORD b = 0; b < member->size; ++b) {
                                            if (currentAbsoluteFieldOffset + b < structTotalSize) {
                                                paramDesc[target_id].validFieldsMask[currentAbsoluteFieldOffset + b] = isConstant;
                                            }
                                        }
                                    }
                                    else if (member->size == 8) {
                                        double val = 0.0; auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                                        bool isConstant = (ec == std::errc{});
                                        if (isConstant) {
                                            *reinterpret_cast<double*>(fieldAddress) = val;
                                        }
                                        for (DWORD b = 0; b < member->size; ++b) {
                                            if (currentAbsoluteFieldOffset + b < structTotalSize) {
                                                paramDesc[target_id].validFieldsMask[currentAbsoluteFieldOffset + b] = isConstant;
                                            }
                                        }
                                    }
                                    else if (member->size == 1) {
                                        bool isConstant = false;
                                        if (member->typeName == "bool") {
                                            if (cleanPrimitiveStr == "true" || cleanPrimitiveStr == "1") {
                                                *reinterpret_cast<bool*>(fieldAddress) = true; isConstant = true;
                                            }
                                            else if (cleanPrimitiveStr == "false" || cleanPrimitiveStr == "0") {
                                                *reinterpret_cast<bool*>(fieldAddress) = false; isConstant = true;
                                            }
                                        }
                                        else {
                                            int val = 0; auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                                            if (ec == std::errc{}) {
                                                *reinterpret_cast<unsigned char*>(fieldAddress) = static_cast<unsigned char>(val);
                                                isConstant = true;
                                            }
                                        }
                                        if (currentAbsoluteFieldOffset < structTotalSize) {
                                            paramDesc[target_id].validFieldsMask[currentAbsoluteFieldOffset] = isConstant;
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

                        LocalWriteStructBytes(byteBase, mainTypeName, innerArgs, 0);
                    };
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
                // 1. Каждый кадр берем абсолютно "живой" объект со стека (где x пульсирует)
                TargetType resultStruct = literalValue;

                if (real_id >= 0 && real_id < static_cast<int>(paramDesc.size()) && paramDesc[real_id].loaded) {
                    if (auto pCachedVal = std::any_cast<TargetType>(&paramDesc[real_id].value)) {

                        char* targetBase = reinterpret_cast<char*>(&resultStruct);
                        const char* cacheBase = reinterpret_cast<const char*>(pCachedVal);

                        size_t structTotalSize = sizeof(TargetType);
                        const auto& mask = paramDesc[real_id].validFieldsMask;

                        // 2. Плоский побайтовый перенос за наносекунды
                        // Если размер маски совпадает со структурой, переносим только константные байты
                        if (mask.size() == structTotalSize) {
                            for (size_t b = 0; b < structTotalSize; ++b) {
                                if (mask[b]) {
                                    targetBase[b] = cacheBase[b];
                                }
                            }
                        }
                    }
                }

                return resultStruct;
            }


            return static_cast<TargetType>(literalValue);
        }
    };

    template <typename T>
    struct MsvcVectorProxy {
        T* myFirst;
        T* myLast;
        T* myEnd;
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

    // ЧАСТИЧНАЯ СПЕЦИАЛИЗАЦИЯ ДЛЯ ДИНАМИЧЕСКИХ ВЕКТОРОВ НА СТУДО-ЯКОРЯХ
    // Переработанная безопасная специализация в eval.h (БЕЗ использования MsvcVectorProxy)
    // Универсальная специализация в eval.h — поддерживает ЛЮБЫЕ смешанные структуры
    // Исправленный и синхронизированный с рантаймом движок Щита в eval.h
    // Полностью исправленный Щит в eval.h — стабилизирует память элементов вектора
    // Полностью универсальный Щит в eval.h — кушает ЛЮБЫЕ смешанные структуры через PDB
    // ============================================================================
// НАЙТИ ИЛИ ВСТАВИТЬ В КОНЕЦ ФАЙЛА eval.h (ПЕРЕД struct LazyTypeDetector)
// ============================================================================

    template <typename ElementType, FixedString<260> AbsoluteFile, int Line, int Column>
    struct EvalSyntaxShield<std::vector<ElementType>, AbsoluteFile, Line, Column> {

        inline static std::vector<ElementType> Get(const std::vector<ElementType>& literalValue) {
            int target_id = GlobalEvalRegistry<std::vector<ElementType>, AbsoluteFile, Line, Column>::cached_id;

            if (target_id < 0 || target_id >= static_cast<int>(paramDesc.size())) {
                return literalValue;
            }

            if (!paramDesc[target_id].loaded) {
                paramDesc[target_id].value = std::any(literalValue);

                if (!paramDesc[target_id].structInfo.isStruct) {
                    std::string elemTypeName = typeid(ElementType).name();
                    if (elemTypeName.rfind("struct ", 0) == 0) elemTypeName.erase(0, 7);
                    else if (elemTypeName.rfind("class ", 0) == 0) elemTypeName.erase(0, 6);

                    size_t len = elemTypeName.length();
                    std::wstring wTypeName(len, L'\0');
                    MultiByteToWideChar(CP_ACP, 0, elemTypeName.c_str(), static_cast<int>(len), wTypeName.data(), static_cast<int>(len));

                    std::vector<std::string> fNames; std::vector<DWORD> fOffsets;
                    std::vector<DWORD> fSizes;       std::vector<std::string> fTypes;

                    if (LoadStructMetadataDirect(wTypeName.c_str(), fNames, fOffsets, fSizes, fTypes)) {
                        paramDesc[target_id].structInfo.isStruct = true;
                        for (size_t k = 0; k < fOffsets.size(); ++k) {
                            paramDesc[target_id].structInfo.members.push_back({
                                std::move(fNames[k]), fOffsets[k], fSizes[k], std::move(fTypes[k])
                                });
                        }
                    }
                }

                paramDesc[target_id].stringUpdater = [target_id](std::any& targetAny, const std::string& textValue) {
                    std::vector<ElementType>* pVec = std::any_cast<std::vector<ElementType>>(&targetAny);
                    if (!pVec) return;

                    if (textValue.find('{') == std::string::npos || textValue.find('}') == std::string::npos) return;

                    size_t openBrace = textValue.find('{');
                    size_t closeBrace = textValue.rfind('}');
                    if (openBrace == std::string::npos || closeBrace == std::string::npos || closeBrace <= openBrace) return;
                    std::string innerArrayContent = textValue.substr(openBrace + 1, closeBrace - openBrace - 1);

                    std::vector<std::string> elements;
                    std::string currentElement = "";
                    int braceDepth = 0;

                    for (char c : innerArrayContent) {
                        if (c == '{') braceDepth++;
                        if (braceDepth > 0) currentElement += c;

                        if (c == '}') {
                            braceDepth--;
                            if (braceDepth == 0 && !currentElement.empty()) {
                                elements.push_back(currentElement);
                                currentElement.clear();
                            }
                        }
                    }

                    for (auto& e : elements) {
                        e.erase(0, e.find_first_not_of(" \t\r\n"));
                        e.erase(e.find_last_not_of(" \t\r\n") + 1);
                    }
                    elements.erase(std::remove_if(elements.begin(), elements.end(), [](const std::string& s) { return s.empty(); }), elements.end());

                    if (elements.empty()) return;

                    if (pVec->size() != elements.size()) {
                        pVec->resize(elements.size());
                    }

                    char* byteBase = reinterpret_cast<char*>(pVec->data());
                    size_t elemSize = sizeof(ElementType);
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

                    auto LocalLookupStructMembersFromParams = [target_id](const std::string& typeNameStr) -> std::vector<StructMemberDesc> {
                        std::string cleanName = typeNameStr;
                        cleanName.erase(0, cleanName.find_first_not_of(" \t\r\n.")); cleanName.erase(cleanName.find_last_not_of(" \t\r\n") + 1);
                        if (cleanName.rfind("struct ", 0) == 0) cleanName.erase(0, 7);
                        if (cleanName.rfind("class ", 0) == 0) cleanName.erase(0, 6);

                        std::string baseVectorTypeName = typeid(ElementType).name();
                        if (baseVectorTypeName.find(cleanName) != std::string::npos) {
                            return paramDesc[target_id].structInfo.members;
                        }

                        for (const auto& p : paramDesc) {
                            if (p.structInfo.isStruct) {
                                std::string pTypeName = p.value.type().name();
                                if (pTypeName.find(cleanName) != std::string::npos) {
                                    return p.structInfo.members;
                                }
                            }
                        }
                        return std::vector<StructMemberDesc>();
                        };
                    std::function<void(char*, const std::string&, const std::string&)> LocalWriteStructBytes =
                        [&](char* byteBase, const std::string& curTypeName, const std::string& curInnerText) {

                        std::vector<StructMemberDesc> membersCache = LocalLookupStructMembersFromParams(curTypeName);

                        if (membersCache.empty()) {
                            std::string valClean = curInnerText;
                            size_t subOpen = valClean.find('('); size_t subClose = valClean.rfind(')');
                            if (subOpen != std::string::npos && subClose != std::string::npos && subClose > subOpen) {
                                valClean = valClean.substr(subOpen + 1, subClose - subOpen - 1);
                            }
                            while (!valClean.empty() && (valClean.back() == 'f' || valClean.back() == 'F' || valClean.back() == 'u' || valClean.back() == 'U')) {
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
                            size_t eq = token.find('=');
                            std::string fieldName = ""; std::string valueStr = token;
                            const StructMemberDesc* member = nullptr;

                            if (eq != std::string::npos) {
                                fieldName = token.substr(0, eq);
                                fieldName.erase(0, fieldName.find_first_not_of(" \t\r\n."));
                                fieldName.erase(fieldName.find_last_not_of(" \t\r\n") + 1);
                                valueStr = token.substr(eq + 1);
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

                                    for (size_t k = 0; k < arrayElements.size() && static_cast<int>(k) < arrayCount; ++k) {
                                        std::string elemToken = arrayElements[k];
                                        char* elementAddress = fieldAddress + (k * elementSize);

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

                                size_t dColon = cleanPrimitiveStr.rfind("::");
                                if (dColon != std::string::npos) {
                                    std::string enumValueToken = cleanPrimitiveStr.substr(dColon + 2);
                                    enumValueToken.erase(0, enumValueToken.find_first_not_of(" \t\r\n"));
                                    enumValueToken.erase(enumValueToken.find_last_not_of(" \t\r\n") + 1);

                                    int finalEnumInt = 0; bool enumFound = false;
                                    for (const auto& pr : paramDesc) {
                                        if (pr.enumInfo.isEnum && !pr.enumInfo.elements.empty()) {
                                            for (const auto& elem : pr.enumInfo.elements) {
                                                std::string cleanElemName = elem.name;
                                                size_t subC = cleanElemName.rfind("::");
                                                if (subC != std::string::npos) cleanElemName = cleanElemName.substr(subC + 2);

                                                if (cleanElemName == enumValueToken || elem.name == enumValueToken) {
                                                    finalEnumInt = elem.value; enumFound = true; break;
                                                }
                                            }
                                        }
                                        if (enumFound) break;
                                    }

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
                                    float val = 0.0f;
                                    // std::from_chars возвращает ec == std::errc{} только при успешном разборе числа
                                    auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                                    if (ec == std::errc{}) {
                                        // Это чистая константа из кода! Перезаписываем её в память
                                        if (member->typeName == "int" || member->typeName == "long") {
                                            *reinterpret_cast<int*>(byteBase + member->offset) = static_cast<int>(val);
                                        }
                                        else {
                                            *reinterpret_cast<float*>(byteBase + member->offset) = val;
                                        }
                                    }
                                    // Если ec != std::errc{} (там написано "x"), мы просто ПРОПУСКАЕМ запись.
                                    // Поле структуры сохраняет то динамическое значение, которое пришло из рантайма в этом кадре!
                                }
                                else if (member->size == 8) {
                                    double val = 0.0; std::from_chars(strStart, strEnd, val);
                                    *reinterpret_cast<double*>(fieldAddress) = val;
                                }
                                else if (member->size == 1) {
                                    if (member->typeName == "bool") {
                                        *reinterpret_cast<bool*>(fieldAddress) = (cleanPrimitiveStr == "true" || cleanPrimitiveStr == "1" || cleanPrimitiveStr == "True");
                                    }
                                    else {
                                        int val = 0; std::from_chars(strStart, strEnd, val);
                                        *reinterpret_cast<unsigned char*>(fieldAddress) = static_cast<unsigned char>(val);
                                    }
                                }
                            }
                        }
                        };
                        std::string mainTypeName = typeid(ElementType).name();
                        if (mainTypeName.rfind("struct ", 0) == 0) mainTypeName.erase(0, 7);
                        if (mainTypeName.rfind("class ", 0) == 0) mainTypeName.erase(0, 6);

                        for (size_t idx = 0; idx < elements.size(); ++idx) {
                            char* targetElementAddr = byteBase + (idx * elemSize);
                            std::string structToken = elements[idx];

                            size_t subOpen = structToken.find('{');
                            size_t subClose = structToken.rfind('}');
                            if (subOpen != std::string::npos && subClose != std::string::npos && subClose > subOpen) {
                                structToken = structToken.substr(subOpen + 1, subClose - subOpen - 1);
                            }

                            LocalWriteStructBytes(targetElementAddr, mainTypeName, structToken);
                        }
            };

            paramDesc[target_id].loaded = true;
        }

        std::string absPath = NormalizePath(AbsoluteFile.c_str());
        int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));

        if (real_id >= 0 && real_id < static_cast<int>(paramDesc.size())) {
            if (auto pVector = std::any_cast<std::vector<ElementType>>(&paramDesc[real_id].value)) {
                return *pVector;
            }
        }

        return literalValue;
    }
};



    // ----------------- Изолированная подсистема массивов -----------------
    

    template <typename T> struct is_initializer_list : std::false_type {};
    template <typename E> struct is_initializer_list<std::initializer_list<E>> : std::true_type { using element_type = E; };
    // ---------------------------------------------------------------------


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
