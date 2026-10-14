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

        std::vector<bool> validFieldsMask;

        long long typeMinBound = 0;
        long long typeMaxBound = 0;
        int line = 0;
        int column = 0;
        std::function<void(std::any& targetAny, const std::string& textValue)> stringUpdater = nullptr;
        std::string macroName;
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

                size_t lastCols = newValue.rfind("::");
                if (lastCols != std::string::npos) {
                    std::string pureTypeName = newValue.substr(0, lastCols);
                    wEnumName = std::wstring(pureTypeName.begin(), pureTypeName.end());
                }

                if (!wEnumName.empty()) {
                    std::vector<std::string> parsedNames;
                    std::vector<int> parsedValues;

                    if (LoadEnumMetadataDirect(wEnumName.c_str(), parsedNames, parsedValues)) {
                        for (size_t i = 0; i < parsedNames.size(); ++i) {
                            EnumElementDesc gameElem{ parsedValues[i], parsedNames[i] };
                            paramDesc[id].enumInfo.elements.push_back(gameElem);
                        }
                    }
                }
            }

            std::string cleanNewValue = newValue;
            size_t lastColsNew = cleanNewValue.rfind("::");
            if (lastColsNew != std::string::npos) {
                cleanNewValue = cleanNewValue.substr(lastColsNew + 2);             }

            for (const auto& elem : paramDesc[id].enumInfo.elements) {
                std::string cleanElemName = elem.name;
                size_t lastColsElem = cleanElemName.rfind("::");
                if (lastColsElem != std::string::npos) {
                    cleanElemName = cleanElemName.substr(lastColsElem + 2);
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

    inline int RegisterEvalPreMain(const char* file, const char* macroName, std::any value, int line, int column, EnumTypeDesc enumDesc) {
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
            .validFieldsMask = {}, // Явное обнуление полей структуры для агрегата
            .typeMinBound = 0,
            .typeMaxBound = 0,
            .line = line,
            .column = column,
            .macroName = std::string(macroName) // <-- Железное сохранение имени в std::string
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

    template <typename T, auto MacroName, FixedString<260> AbsoluteFile, int Line, int Column>
    struct GlobalEvalRegistry {
        static inline const int cached_id = []() {
            if constexpr (std::is_enum_v<T>) {
                return RegisterEvalPreMain(AbsoluteFile.c_str(), MacroName.c_str(), std::any{ static_cast<int>(T{}) }, Line, Column, EnumTypeDesc{ .isEnum = true });
            }
            else {
                if constexpr (std::is_default_constructible_v<T>) {
                    return RegisterEvalPreMain(AbsoluteFile.c_str(), MacroName.c_str(), std::any{ T{} }, Line, Column, EnumTypeDesc{ false });
                }
                else {
                    return RegisterEvalPreMain(AbsoluteFile.c_str(), MacroName.c_str(), std::any{}, Line, Column, EnumTypeDesc{ false });
                }
            }
            }();
    };
    //----

    inline std::vector<StructMemberDesc> LookupStructMembersFromParams(const std::string& typeNameStr) {
        std::string cleanName = typeNameStr;
        cleanName.erase(0, cleanName.find_first_not_of(" \t\r\n."));
        cleanName.erase(cleanName.find_last_not_of(" \t\r\n") + 1);

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
    }

    inline std::vector<std::string> SplitByOuterCommas(const std::string& input) {
        std::vector<std::string> resTokens;
        std::string curT;
        int bCount = 0;
        int pCount = 0;

        for (size_t i = 0; i < input.length(); ++i) {
            char c = input[i];
            if (c == '{') bCount++;
            else if (c == '}') bCount--;
            else if (c == '(') pCount++;
            else if (c == ')') pCount--;

            if (c == ',' && bCount == 0 && pCount == 0) {
                resTokens.push_back(curT);
                curT.clear();
            }
            else {
                curT += c;
            }
        }
        if (!curT.empty()) resTokens.push_back(curT);

        for (auto& t : resTokens) {
            t.erase(0, t.find_first_not_of(" \t\r\n"));
            t.erase(t.find_last_not_of(" \t\r\n") + 1);
        }

        resTokens.erase(std::remove_if(resTokens.begin(), resTokens.end(),
            [](const std::string& s) { return s.empty(); }), resTokens.end());

        return resTokens;
    }

    inline bool ExtractAndStripComments(const std::string& textValue, std::string& outInnerArgs) {
        size_t openBrace = textValue.find('{');
        size_t closeBrace = textValue.rfind('}');
        if (openBrace == std::string::npos || closeBrace == std::string::npos || closeBrace <= openBrace) {
            return false;
        }

        std::string rawInnerArgs = textValue.substr(openBrace + 1, closeBrace - openBrace - 1);
        outInnerArgs.clear();
        outInnerArgs.reserve(rawInnerArgs.length());

        bool inComment = false;

        for (size_t i = 0; i < rawInnerArgs.length(); ++i) {
            char c = rawInnerArgs[i];
            char nextC = (i + 1 < rawInnerArgs.length()) ? rawInnerArgs[i + 1] : '\0';

            if (inComment) {
                if (c == '\n' || c == '\r') inComment = false;
                continue;
            }
            if ((c == '/' && nextC == '/') || c == '#') {
                inComment = true;
                if (c == '/') i++;
                continue;
            }
            outInnerArgs += c;
        }

        return true;
    }

        // Полная копия рантайм-логики LocalWriteStructBytes, переведенная в классическую функцию
    inline void WriteStructBytes(
        char* byteBase,
        const std::string& curTypeName,
        const std::string& curInnerText,
        DWORD absoluteBaseOffset,
        int targetId,
        size_t structTotalSize)
        {
            std::vector<StructMemberDesc> membersCache = LivePT::LookupStructMembersFromParams(curTypeName);

            if (membersCache.empty()) {
                std::string valClean = curInnerText;
                size_t subOpen = valClean.find('(');
                size_t subClose = valClean.rfind(')');
                if (subOpen != std::string::npos && subClose != std::string::npos && subClose > subOpen) {
                    valClean = valClean.substr(subOpen + 1, subClose - subOpen - 1);
                }
                while (!valClean.empty() && (valClean.back() == 'f' || valClean.back() == 'F' || valClean.back() == 'u' || valClean.back() == 'U')) {
                    valClean.pop_back();
                }
                const char* sS = valClean.data();
                const char* sE = valClean.data() + valClean.size();
                float fV = 0.0f;
                std::from_chars(sS, sE, fV);
                *reinterpret_cast<float*>(byteBase) = fV;
                return;
            }

            auto tokens = LivePT::SplitByOuterCommas(curInnerText);
            size_t positionalIndex = 0;

            for (const auto& token : tokens) {
                std::string fieldName = "";
                std::string valueStr = token;
                const StructMemberDesc* member = nullptr;

                size_t eqPos = std::string::npos;
                int bCount = 0;
                int pCount = 0;

                for (size_t i = 0; i < token.length(); ++i) {
                    char c = token[i];
                    if (c == '{') bCount++;
                    else if (c == '}') { bCount--; if (bCount < 0) bCount = 0; }
                    else if (c == '(') pCount++;
                    else if (c == ')') { pCount--; if (pCount < 0) pCount = 0; }

                    if (c == '=' && bCount == 0 && pCount == 0) {
                        eqPos = i;
                        break;
                    }
                }

                if (eqPos != std::string::npos) {
                    fieldName = token.substr(0, eqPos);
                    fieldName.erase(0, fieldName.find_first_not_of(" \t\r\n."));
                    fieldName.erase(fieldName.find_last_not_of(" \t\r\n") + 1);
                    valueStr = token.substr(eqPos + 1);
                    valueStr.erase(0, valueStr.find_first_not_of(" \t\r\n"));
                    valueStr.erase(valueStr.find_last_not_of(" \t\r\n") + 1);

                    for (size_t mIdx = 0; mIdx < membersCache.size(); ++mIdx) {
                        if (membersCache[mIdx].name == fieldName) {
                            member = &membersCache[mIdx];
                            positionalIndex = mIdx + 1;
                            break;
                        }
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

                if (paramDesc[targetId].validFieldsMask.size() < structTotalSize) {
                    paramDesc[targetId].validFieldsMask.resize(structTotalSize, false);
                }

                if (!LivePT::LookupStructMembersFromParams(member->typeName).empty()) {
                    size_t openB = valueStr.find('{');
                    size_t closeB = valueStr.rfind('}');
                    if (openB == std::string::npos) openB = valueStr.find('(');
                    if (closeB == std::string::npos) closeB = valueStr.rfind(')');

                    if (openB != std::string::npos && closeB != std::string::npos && closeB > openB) {
                        std::string nestedInnerText = valueStr.substr(openB + 1, closeB - openB - 1);

                        // Рекурсивный вызов уже новой функции
                        WriteStructBytes(fieldAddress, member->typeName, nestedInnerText, currentAbsoluteFieldOffset, targetId, structTotalSize);
                    }
                }
                else {
                    std::string cleanPrimitiveStr = valueStr;
                    size_t pOpen = cleanPrimitiveStr.find('(');
                    size_t pClose = cleanPrimitiveStr.rfind(')');
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

                        int finalEnumInt = 0;
                        bool enumFound = false;
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

                        if (!enumFound) {
                            if (enumValueToken == "circle") finalEnumInt = 0;
                            else if (enumValueToken == "box") finalEnumInt = 1;
                            else if (enumValueToken == "roundbox") finalEnumInt = 2;
                        }

                        *reinterpret_cast<int*>(fieldAddress) = finalEnumInt;

                        for (DWORD b = 0; b < member->size; ++b) {
                            if (currentAbsoluteFieldOffset + b < structTotalSize) {
                                paramDesc[targetId].validFieldsMask[currentAbsoluteFieldOffset + b] = true;
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
                                paramDesc[targetId].validFieldsMask[currentAbsoluteFieldOffset + b] = isConstant;
                            }
                        }
                    }
                    else if (member->size == 8) {
                        double val = 0.0;
                        auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                        bool isConstant = (ec == std::errc{});
                        if (isConstant) {
                            *reinterpret_cast<double*>(fieldAddress) = val;
                        }
                        for (DWORD b = 0; b < member->size; ++b) {
                            if (currentAbsoluteFieldOffset + b < structTotalSize) {
                                paramDesc[targetId].validFieldsMask[currentAbsoluteFieldOffset + b] = isConstant;
                            }
                        }
                    }
                    else if (member->size == 1) {
                        bool isConstant = false;
                        if (member->typeName == "bool") {
                            if (cleanPrimitiveStr == "true" || cleanPrimitiveStr == "1") {
                                *reinterpret_cast<bool*>(fieldAddress) = true;
                                isConstant = true;
                            }
                            else if (cleanPrimitiveStr == "false" || cleanPrimitiveStr == "0") {
                                *reinterpret_cast<bool*>(fieldAddress) = false;
                                isConstant = true;
                            }
                        }
                        else {
                            int val = 0;
                            auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                            if (ec == std::errc{}) {
                                *reinterpret_cast<unsigned char*>(fieldAddress) = static_cast<unsigned char>(val);
                                isConstant = true;
                            }
                        }
                        if (currentAbsoluteFieldOffset < structTotalSize) {
                            paramDesc[targetId].validFieldsMask[currentAbsoluteFieldOffset] = isConstant;
                        }
                    }
                }
            }
    }

    inline bool ExtractVectorElements(const std::string& textValue, std::vector<std::string>& outElements) {
        outElements.clear();

        size_t openBrace = textValue.find('{');
        size_t closeBrace = textValue.rfind('}');
        if (openBrace == std::string::npos || closeBrace == std::string::npos || closeBrace <= openBrace) {
            return false;
        }

        std::string innerArrayContent = textValue.substr(openBrace + 1, closeBrace - openBrace - 1);
        std::string currentElement = "";
        int braceDepth = 0;

        for (char c : innerArrayContent) {
            if (c == '{') braceDepth++;
            if (braceDepth > 0) currentElement += c;

            if (c == '}') {
                braceDepth--;
                if (braceDepth == 0 && !currentElement.empty()) {
                    outElements.push_back(currentElement);
                    currentElement.clear();
                }
            }
        }

        for (auto& e : outElements) {
            e.erase(0, e.find_first_not_of(" \t\r\n"));
            e.erase(e.find_last_not_of(" \t\r\n") + 1);
        }

        outElements.erase(std::remove_if(outElements.begin(), outElements.end(),
            [](const std::string& s) { return s.empty(); }), outElements.end());

        return !outElements.empty();
    }

    //----
    template <typename TargetType, auto MacroName, FixedString<260> AbsoluteFile, int Line, int Column>
    struct EvalSyntaxShield {
        template <typename TLiteral>
        inline static TargetType Get(TLiteral literalValue) {
            // Передаем MacroName дальше в реестр
            int target_id = GlobalEvalRegistry<TargetType, MacroName, AbsoluteFile, Line, Column>::cached_id;

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
                    // === ЧАСТЬ 1: ОЧИЩЕННЫЙ РЕКУРСИВНЫЙ ПАРСЕР СТРУКТУР ===
                    paramDesc[target_id].stringUpdater = [target_id](std::any& targetAny, const std::string& textValue) {
                        std::string innerArgs;
                        if (!LivePT::ExtractAndStripComments(textValue, innerArgs)) return;

                        TargetType* pStructInstance = std::any_cast<TargetType>(&targetAny);
                        if (!pStructInstance) return;
                        char* byteBase = reinterpret_cast<char*>(pStructInstance);

                        std::string mainTypeName = typeid(TargetType).name();
                        if (mainTypeName.rfind("struct ", 0) == 0) mainTypeName.erase(0, 7);
                        if (mainTypeName.rfind("class ", 0) == 0) mainTypeName.erase(0, 6);

                        size_t structTotalSize = sizeof(TargetType);

                        // Перенаправляем выполнение в чистую вынесенную функцию
                        WriteStructBytes(byteBase, mainTypeName, innerArgs, 0, target_id, structTotalSize);
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
    template <typename ElementType, auto MacroName, FixedString<260> AbsoluteFile, int Line, int Column>
    struct EvalSyntaxShield<std::vector<ElementType>, MacroName, AbsoluteFile, Line, Column> {

        inline static std::vector<ElementType> Get(const std::vector<ElementType>& literalValue) {
            int target_id = GlobalEvalRegistry<std::vector<ElementType>, MacroName, AbsoluteFile, Line, Column>::cached_id;

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

                    std::vector<std::string> elements;
                    if (!LivePT::ExtractVectorElements(textValue, elements)) return;

                    if (pVec->size() != elements.size()) {
                        pVec->resize(elements.size());
                    }

                    char* byteBase = reinterpret_cast<char*>(pVec->data());
                    size_t elemSize = sizeof(ElementType);
                    size_t vectorTotalBytes = pVec->size() * elemSize;

                    // Гарантируем, что размер маски валидности соответствует общему числу байт вектора
                    if (paramDesc[target_id].validFieldsMask.size() < vectorTotalBytes) {
                        paramDesc[target_id].validFieldsMask.resize(vectorTotalBytes, false);
                    }

                    // Получаем чистое текстовое имя типа элемента вектора для метаданных PDB
                    std::string mainTypeName = typeid(ElementType).name();
                    if (mainTypeName.rfind("struct ", 0) == 0) mainTypeName.erase(0, 7);
                    if (mainTypeName.rfind("class ", 0) == 0) mainTypeName.erase(0, 6);

                    // ПЕРЕИСПОЛЬЗОВАНИЕ: Последовательно гоним каждый элемент вектора через общую WriteStructBytes
                    for (size_t idx = 0; idx < elements.size(); ++idx) {
                        char* targetElementAddr = byteBase + (idx * elemSize);
                        std::string structToken = elements[idx];

                        // Снимаем внешние фигурные скобки самого элемента, если они есть (например, {1, 2} -> 1, 2)
                        size_t subOpen = structToken.find('{');
                        size_t subClose = structToken.rfind('}');
                        if (subOpen != std::string::npos && subClose != std::string::npos && subClose > subOpen) {
                            structToken = structToken.substr(subOpen + 1, subClose - subOpen - 1);
                        }

                        // Считаем сквозное абсолютное смещение байт для текущего элемента массива в маске
                        DWORD elementAbsoluteBaseOffset = static_cast<DWORD>(idx * elemSize);

                        // Вызываем общую проверенную функцию одиночной структуры
                        LivePT::WriteStructBytes(targetElementAddr, mainTypeName, structToken, elementAbsoluteBaseOffset, target_id, vectorTotalBytes);
                    }
                }; 
            paramDesc[target_id].loaded = true;
        }

        std::string absPath = NormalizePath(AbsoluteFile.c_str());
        int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));

        std::vector<ElementType> resultVector = literalValue;

        if (real_id >= 0 && real_id < static_cast<int>(paramDesc.size()) && paramDesc[real_id].loaded) {
            if (auto pCachedVector = std::any_cast<std::vector<ElementType>>(&paramDesc[real_id].value)) {

                if (literalValue.size() != pCachedVector->size()) {
                    return *pCachedVector;
                }

                std::vector<ElementType> resultVector = literalValue;

                char* targetBase = reinterpret_cast<char*>(resultVector.data());
                const char* cacheBase = reinterpret_cast<const char*>(pCachedVector->data());

                size_t vectorTotalBytes = resultVector.size() * sizeof(ElementType);
                const auto& mask = paramDesc[real_id].validFieldsMask;

                if (mask.size() == vectorTotalBytes) {
                    for (size_t b = 0; b < vectorTotalBytes; ++b) {
                        if (mask[b]) {
                            targetBase[b] = cacheBase[b];
                        }
                    }
                }

                return resultVector;
            }
        }

        return literalValue;



    }
};


    template <typename LiteralType, auto MacroName, FixedString<260> AbsoluteFile, int Line, int Column>
    struct LazyTypeDetector {
        LiteralType rawValue;

        constexpr LazyTypeDetector(LiteralType val) : rawValue(val) {}

        template <typename TargetType>
        inline operator TargetType() const {
            if constexpr (std::is_enum_v<LiteralType>) {
                return static_cast<TargetType>(EvalSyntaxShield<LiteralType, MacroName, AbsoluteFile, Line, Column>::Get(rawValue));
            }
            else {
                return EvalSyntaxShield<TargetType, MacroName, AbsoluteFile, Line, Column>::Get(rawValue);
            }
        }

        inline operator LiteralType() const {
            return EvalSyntaxShield<LiteralType, MacroName, AbsoluteFile, Line, Column>::Get(rawValue);
        }

        template <typename TTarget>
            requires (std::is_floating_point_v<TTarget> && !std::is_same_v<TTarget, LiteralType>)
        inline operator TTarget() const {
            return static_cast<TTarget>(operator LiteralType());
        }
    };



    }
