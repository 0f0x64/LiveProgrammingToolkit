#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <any>
#include <sstream>
#include <algorithm>
#include <type_traits>
#include <cctype>
#include <memory>

namespace LivePT {

    constexpr int MAX_SCAN_RANGE = 1024;

    inline std::string NormalizePath(const char* fullPath) {
        std::string path(fullPath);
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    template <typename E, E V>
    inline std::string_view GetEnumNameRaw() { return __FUNCSIG__; }

    struct EnumElementDesc { int value; std::string name; };
    struct EnumTypeDesc { bool isEnum = false; std::vector<EnumElementDesc> elements; };

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
        if (lastCols != std::string::npos) valueStr = valueStr.substr(lastCols + 2);
        if (!valueStr.empty()) desc.elements.push_back({ V, valueStr });
    }

    template <typename E, int I> inline void ProcessSingleIndex(EnumTypeDesc& desc) { ParseSignature(GetEnumNameRaw<E, static_cast<E>(I)>(), I, desc); }
    template <typename E, int... Is> inline void ExpandEnumIndices(EnumTypeDesc& desc, std::integer_sequence<int, Is...>) { (ProcessSingleIndex<E, Is>(desc), ...); }
    template <typename E> inline EnumTypeDesc ReflectedEnumInfo() {
        if constexpr (!std::is_enum_v<E>) return EnumTypeDesc{ false };
        else { EnumTypeDesc desc; desc.isEnum = true; ExpandEnumIndices<E>(desc, std::make_integer_sequence<int, MAX_SCAN_RANGE>{}); return desc; }
    }

    struct ref {
        std::string value;
        bool loaded = false;
        std::string fileName;
        unsigned int counterID;
        EnumTypeDesc enumInfo;
    };

    inline std::vector<ref>& getParamDesc() { static std::vector<ref> instance; return instance; }
    inline std::unordered_map<std::string, int>& getRegistry() { static std::unordered_map<std::string, int> instance; return instance; }
    inline std::vector<ref>& paramDesc = getParamDesc();
    inline std::unordered_map<std::string, int>& registry = getRegistry();

    inline int getID(const std::string& key) {
        auto it = registry.find(key);
        if (it != registry.end()) return it->second;
        return -1;
    }

    inline void UpdateParamValue(int id, const std::string& newValue) {
        if (newValue.empty() || id < 0 || id >= static_cast<int>(paramDesc.size())) return;
        paramDesc[id].value = newValue;
    }

    struct StaticOrderKey {
        int line;
        int column;
        bool operator<(const StaticOrderKey& other) const {
            if (line != other.line) return line < other.line;
            return column < other.column;
        }
    };
    inline int RegisterEvalPreMain(const char* file, const std::string& value, int line, int column, EnumTypeDesc enumDesc) {
        std::string absolutePath = NormalizePath(file);
        static std::unordered_map<std::string, std::map<StaticOrderKey, int>> fileCompileTree;

        StaticOrderKey key{ line, column };
        auto& fileMap = fileCompileTree[absolutePath];

        if (fileMap.find(key) != fileMap.end()) return fileMap[key];

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
            for (size_t i = 0; i < N - 1 && str[i] != '\0'; ++i) buf[i] = str[i];
        }
        constexpr const char* c_str() const { return buf; }
    };
    template <size_t N> FixedString(const char(&str)[N]) -> FixedString<N>;

    template <typename T, FixedString<260> AbsoluteFile, int Line, int Column>
    struct GlobalEvalRegistry {
        static inline const int cached_id = []() {
            if constexpr (std::is_enum_v<T>) {
                return RegisterEvalPreMain(AbsoluteFile.c_str(), "", Line, Column, ReflectedEnumInfo<T>());
            }
            else {
                return RegisterEvalPreMain(AbsoluteFile.c_str(), "", Line, Column, EnumTypeDesc{ false });
            }
            }();
    };

    template <typename T, FixedString<260> AbsoluteFile, int Line, int Column>
    struct EvalSyntaxShield {
        std::any anyVal;
        size_t objSize;

        template <typename V>
        constexpr EvalSyntaxShield(V&& val, size_t size) : anyVal(std::make_any<std::decay_t<V>>(std::forward<V>(val))), objSize(size) {}

        inline operator T() const {
            int target_id = GlobalEvalRegistry<T, AbsoluteFile, Line, Column>::cached_id;
            if (target_id < 0 || target_id >= static_cast<int>(paramDesc.size())) return std::any_cast<T>(anyVal);

            std::string absPath = NormalizePath(AbsoluteFile.c_str());
            int real_id = getID(absPath + ":" + std::to_string(paramDesc[target_id].counterID));
            if (real_id < 0 || real_id >= static_cast<int>(paramDesc.size())) return std::any_cast<T>(anyVal);

            // 1. ¿¬“Œ-—≈–»¿À»«¿÷»ﬂ Õ¿ œ≈–¬ŒÃ  ¿ƒ–≈ »« Àﬁ¡Œ√Œ — ¿Àﬂ–¿ »À» ¿√–≈√¿“¿
            if (!paramDesc[real_id].loaded) {
                if constexpr (std::is_enum_v<T>) {
                    paramDesc[real_id].value = std::to_string(static_cast<int>(std::any_cast<T>(anyVal)));
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    paramDesc[real_id].value = std::any_cast<T>(anyVal) ? "true" : "false";
                }
                else if constexpr (std::is_aggregate_v<T>) {
                    std::stringstream ss;
                    const T& obj = std::any_cast<const T&>(anyVal);
                    const int* words = reinterpret_cast<const int*>(&obj);
                    size_t wordCount = objSize / 4;
                    for (size_t i = 0; i < wordCount; ++i) {
                        ss << (i == 0 ? "" : " ") << words[i];
                    }
                    paramDesc[real_id].value = ss.str();
                }
                else {
                    std::stringstream ss; ss << std::any_cast<T>(anyVal); paramDesc[real_id].value = ss.str();
                }
                paramDesc[real_id].loaded = true;
            }

            if (paramDesc[real_id].value.empty()) {
                return std::any_cast<T>(anyVal);
            }

            // 2. ƒ≈—≈–»¿À»«¿÷»ﬂ »« —“–Œ » Œ¡–¿“ÕŒ ¬ Œ¡⁄≈ “ ’Œ—“¿
            if constexpr (std::is_enum_v<T>) {
                std::string cleanQuery = paramDesc[real_id].value;
                cleanQuery.erase(std::remove_if(cleanQuery.begin(), cleanQuery.end(), ::isspace), cleanQuery.end());
                size_t lastCols = cleanQuery.rfind("::"); if (lastCols != std::string::npos) cleanQuery = cleanQuery.substr(lastCols + 2);
                for (const auto& elem : paramDesc[real_id].enumInfo.elements) { if (elem.name == cleanQuery) return static_cast<T>(elem.value); }
                std::stringstream ss(cleanQuery); int parsedInt = 0; if (ss >> parsedInt) return static_cast<T>(parsedInt);
            }
            else if constexpr (std::is_same_v<T, bool>) {
                std::string str = paramDesc[real_id].value;
                std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return (str == "true" || str == "1");
            }
            else if constexpr (std::is_aggregate_v<T>) {
                T liveInstance = std::any_cast<T>(anyVal);
                std::stringstream ss(paramDesc[real_id].value);

                int* memoryWords = reinterpret_cast<int*>(&liveInstance);
                size_t wordCount = objSize / 4;

                for (size_t i = 0; i < wordCount; ++i) {
                    std::string token;
                    if (!(ss >> token)) break;

                    if (!token.empty() && !std::isdigit(token[0]) && token[0] != '-') {
                        bool resolved = false;
                        for (const auto& rDesc : paramDesc) {
                            if (rDesc.enumInfo.isEnum) {
                                for (const auto& enumItem : rDesc.enumInfo.elements) {
                                    if (enumItem.name == token) {
                                        memoryWords[i] = enumItem.value;
                                        resolved = true;
                                        break;
                                    }
                                }
                            }
                            if (resolved) break;
                        }
                        if (resolved) continue;

                        if (token == "true" || token == "TRUE") { memoryWords[i] = 1; continue; }
                        if (token == "false" || token == "FALSE") { memoryWords[i] = 0; continue; }
                    }

                    std::stringstream tokenConverter(token);
                    double elementVal = 0.0;
                    if (tokenConverter >> elementVal) {
                        memoryWords[i] = static_cast<int>(elementVal);
                    }
                }
                return liveInstance;
            }
            else {
                std::string cleanStr = paramDesc[real_id].value;
                if (!cleanStr.empty() && (cleanStr.back() == 'f' || cleanStr.back() == 'F')) cleanStr.pop_back();
                std::stringstream ss(cleanStr);
                if constexpr (std::is_integral_v<T>) {
                    double doubleVal = 0.0; if (ss >> doubleVal) return static_cast<T>(doubleVal);
                }
                else {
                    if constexpr (!std::is_aggregate_v<T>) {
                        T parsedValue{}; if (ss >> parsedValue) return parsedValue;
                    }
                }
            }

            return std::any_cast<T>(anyVal);
        }
    };
}

#define eval(...) ([&]() { \
    auto _live_tmp = __VA_ARGS__; \
    return LivePT::EvalSyntaxShield< \
        std::remove_cvref_t<decltype(_live_tmp)>, \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(_live_tmp, sizeof(_live_tmp)); \
}())

