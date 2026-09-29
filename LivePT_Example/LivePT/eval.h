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
        std::string funcName;
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

        if (paramDesc[id].enumInfo.isEnum) {
            std::string cleanName = newValue;
            size_t lastCols = cleanName.rfind("::");
            if (lastCols != std::string::npos) {
                cleanName = cleanName.substr(lastCols + 2);
            }
            for (const auto& elem : paramDesc[id].enumInfo.elements) {
                if (elem.name == cleanName) {
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

    inline int RegisterEvalPreMain(const char* file, std::any value, int line, int column, EnumTypeDesc enumDesc, const char* funcName) {
        std::string absolutePath = NormalizePath(file);

        static std::unordered_map<std::string, std::map<StaticOrderKey, int>> fileCompileTree;

        StaticOrderKey key{ line, column };
        auto& fileMap = fileCompileTree[absolutePath];

        if (fileMap.find(key) != fileMap.end()) {
            return fileMap[key];
        }

        int paramID = static_cast<int>(paramDesc.size());
        paramDesc.push_back({
            .funcName = funcName,
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

    template <typename T, FixedString<260> AbsoluteFile, int Line, int Column, FixedString<64> funcName>
    struct GlobalEvalRegistry {
        static inline const int cached_id = []() {
            if constexpr (std::is_enum_v<T>) {
                return RegisterEvalPreMain(AbsoluteFile.c_str(), std::any{ static_cast<int>(T{}) }, Line, Column, 
                    EnumTypeDesc{ .isEnum = true },funcName.c_str());
            }
            else {
                if constexpr (std::is_default_constructible_v<T>) {
                    return RegisterEvalPreMain(AbsoluteFile.c_str(), std::any{ T{} }, Line, Column, 
                        EnumTypeDesc{ false }, funcName.c_str());
                }
                else {
                    return RegisterEvalPreMain(AbsoluteFile.c_str(), std::any{}, Line, Column, 
                        EnumTypeDesc{ false }, funcName.c_str());
                }
            }
            }();
    };

    template <typename TargetType, FixedString<260> AbsoluteFile, int Line, int Column, FixedString<64> funcName>
    struct EvalSyntaxShield {
        template <typename TLiteral>
        inline static TargetType Get(TLiteral literalValue) {
            int target_id = GlobalEvalRegistry<TargetType, AbsoluteFile, Line, Column, funcName>::cached_id;

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

                            if (inComment) {
                                if (c == '\n' || c == '\r') {
                                    inComment = false;
                                    innerArgs += c;
                                }
                                continue;
                            }

                            if ((c == '/' && nextC == '/') || c == '#') {
                                inComment = true;
                                if (c == '/') i++;
                                continue;
                            }

                            innerArgs += c;
                        }

                        int globalId = GlobalEvalRegistry<TargetType, AbsoluteFile, Line, Column, funcName>::cached_id;
                        if (!paramDesc[globalId].structInfo.isStruct) {
                            std::string typeNameAnsi = textValue.substr(0, openBrace);
                            typeNameAnsi.erase(0, typeNameAnsi.find_first_not_of(" \t\r\n"));
                            typeNameAnsi.erase(typeNameAnsi.find_last_not_of(" \t\r\n") + 1);

                            if (typeNameAnsi.rfind("struct ", 0) == 0) typeNameAnsi.erase(0, 7);
                            if (typeNameAnsi.rfind("class ", 0) == 0) typeNameAnsi.erase(0, 6);

                            std::wstring wTypeName(typeNameAnsi.begin(), typeNameAnsi.end());

                            std::vector<std::string> fNames;
                            std::vector<DWORD> fOffsets;
                            std::vector<DWORD> fSizes;
                            std::vector<std::string> fTypes;
                            LoadStructMetadataDirect(wTypeName.c_str(), fNames, fOffsets, fSizes, fTypes);

                            for (size_t i = 0; i < fOffsets.size(); ++i) {
                                paramDesc[globalId].structInfo.members.push_back({ fNames[i], fOffsets[i], fSizes[i], fTypes[i] });
                            }
                            paramDesc[globalId].structInfo.isStruct = true;
                        }

                        TargetType* pStructInstance = std::any_cast<TargetType>(&targetAny);
                        if (!pStructInstance) return;
                        char* byteBase = reinterpret_cast<char*>(pStructInstance);

                        std::stringstream ss(innerArgs);
                        std::string token;
                        size_t fieldIndex = 0;

                        while (std::getline(ss, token, ',')) {
                            token.erase(0, token.find_first_not_of(" \t\r\n"));
                            token.erase(token.find_last_not_of(" \t\r\n") + 1);

                            if (token.empty()) continue;
                            if (fieldIndex >= paramDesc[globalId].structInfo.members.size()) break;

                            size_t eqPos = token.find('=');
                            if (eqPos == std::string::npos) eqPos = token.find(':');
                            if (eqPos != std::string::npos) {
                                token = token.substr(eqPos + 1);
                                token.erase(0, token.find_first_not_of(" \t\r\n"));
                                token.erase(token.find_last_not_of(" \t\r\n") + 1);
                            }

                            auto& member = paramDesc[globalId].structInfo.members[fieldIndex];

                            if (member.offset + member.size <= sizeof(TargetType)) {
                                char* fieldAddress = byteBase + member.offset;

                                while (!token.empty() && (token.back() == 'f' || token.back() == 'F' ||
                                    token.back() == 'u' || token.back() == 'U' ||
                                    token.back() == 'l' || token.back() == 'L')) {
                                    token.pop_back();
                                }

                                const char* strStart = token.data();
                                const char* strEnd = token.data() + token.size();

                                if (member.typeName == "float") {
                                    float val = 0.0f;
                                    auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                                    if (ec == std::errc() && ptr == strEnd) {
                                        *reinterpret_cast<float*>(fieldAddress) = val;
                                    }
                                }
                                else if (member.typeName == "double") {
                                    double val = 0.0;
                                    auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                                    if (ec == std::errc() && ptr == strEnd) {
                                        *reinterpret_cast<double*>(fieldAddress) = val;
                                    }
                                }
                                else if (member.typeName == "int" || member.typeName == "unsigned int" ||
                                    member.typeName == "long" || member.typeName == "unsigned long" ||
                                    member.typeName == "__int64" || member.typeName == "unsigned __int64") {
                                    long long val = 0;
                                    auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                                    if (ec == std::errc() && ptr == strEnd) {
                                        if (member.typeName == "int") *reinterpret_cast<int*>(fieldAddress) = static_cast<int>(val);
                                        else if (member.typeName == "unsigned int") *reinterpret_cast<unsigned int*>(fieldAddress) = static_cast<unsigned int>(val);
                                        else if (member.typeName == "long") *reinterpret_cast<long*>(fieldAddress) = static_cast<long>(val);
                                        else if (member.typeName == "unsigned long") *reinterpret_cast<unsigned long*>(fieldAddress) = static_cast<unsigned long>(val);
                                        else if (member.typeName == "__int64") *reinterpret_cast<long long*>(fieldAddress) = val;
                                        else if (member.typeName == "unsigned __int64") *reinterpret_cast<unsigned long long*>(fieldAddress) = val;
                                    }
                                }
                                else if (member.typeName == "char" || member.typeName == "unsigned char" || member.typeName == "signed char") {
                                    int val = 0; 
                                    auto [ptr, ec] = std::from_chars(strStart, strEnd, val);
                                    if (ec == std::errc() && ptr == strEnd) {
                                        if (member.typeName == "char") *reinterpret_cast<char*>(fieldAddress) = static_cast<char>(val);
                                        else if (member.typeName == "unsigned char") *reinterpret_cast<unsigned char*>(fieldAddress) = static_cast<unsigned char>(val);
                                        else if (member.typeName == "signed char") *reinterpret_cast<signed char*>(fieldAddress) = static_cast<signed char>(val);
                                    }
                                }
                                else if (member.typeName == "bool") {
                                    if (token == "true" || token == "1") {
                                        *reinterpret_cast<bool*>(fieldAddress) = true;
                                    }
                                    else if (token == "false" || token == "0") {
                                        *reinterpret_cast<bool*>(fieldAddress) = false;
                                    }
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


    template <typename LiteralType, FixedString<260> AbsoluteFile, int Line, int Column, FixedString<64> funcName>
    struct LazyTypeDetector {
        LiteralType rawValue;

        constexpr LazyTypeDetector(LiteralType val) : rawValue(val) {}

        template <typename TargetType>
        inline operator TargetType() const {
            if constexpr (std::is_enum_v<LiteralType>) {
                return static_cast<TargetType>(EvalSyntaxShield<LiteralType, AbsoluteFile, Line, Column, funcName>::Get(rawValue));
            }
            else {
                return EvalSyntaxShield<TargetType, AbsoluteFile, Line, Column, funcName>::Get(rawValue);
            }
        }

        inline operator LiteralType() const {
            return EvalSyntaxShield<LiteralType, AbsoluteFile, Line, Column, funcName>::Get(rawValue);
        }

        template <typename TTarget>
            requires (std::is_floating_point_v<TTarget> && !std::is_same_v<TTarget, LiteralType>)
        inline operator TTarget() const {
            return static_cast<TTarget>(operator LiteralType());
        }
    };



    }
