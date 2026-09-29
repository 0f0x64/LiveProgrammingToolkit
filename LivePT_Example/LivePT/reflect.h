#pragma once
#include <windows.h>
#include <source_location>
#include <string>
#include <vector>
#include <unordered_map>
#include <typeinfo>

namespace LivePT {

    struct CallSiteParamField {
        std::string fieldName;
        std::string typeName;
        DWORD offset;
        DWORD size;
        void* pRuntimeMemory;
    };

    struct CallSiteDesc {
        std::string fileName;
        int line;
        void* pBaseAddress;
        std::vector<CallSiteParamField> fields;
    };

    inline std::unordered_map<std::string, CallSiteDesc>& GetCallSitesRegistry() {
        static std::unordered_map<std::string, CallSiteDesc> instance;
        return instance;
    }

    template<typename TStruct>
    inline void ManualRegisterCallSite(void* pBase, const std::source_location& loc) {
        if (!pBase) return;

        std::string fileKey = NormalizePath(loc.file_name()) + ":" + std::to_string(loc.line());
        auto& registry = GetCallSitesRegistry();

        // Если эта строка кода уже вызывалась, просто двигаем адрес стека
        auto it = registry.find(fileKey);
        if (it != registry.end()) {
            it->second.pBaseAddress = pBase;
            for (auto& field : it->second.fields) {
                field.pRuntimeMemory = reinterpret_cast<void*>(reinterpret_cast<char*>(pBase) + field.offset);
            }
            return;
        }

        CallSiteDesc desc;
        desc.fileName = NormalizePath(loc.file_name());
        desc.line = static_cast<int>(loc.line());
        desc.pBaseAddress = pBase;

        // ВМЕСТО ВЫЗОВА DBGHELP: Ищем этот тип в уже прогретом кэше paramDesc!
        bool fieldsFound = false;
        auto& globalParams = LivePT::getParamDesc();

        std::string targetTypeName = typeid(TStruct).name();
        // Очищаем префиксы для точного сопоставления строк
        if (targetTypeName.rfind("struct ", 0) == 0) targetTypeName.erase(0, 7);
        if (targetTypeName.rfind("class ", 0) == 0) targetTypeName.erase(0, 6);

        for (const auto& p : globalParams) {
            if (!p.structInfo.isStruct || !p.value.has_value()) continue;

            std::string pTypeName = p.value.type().name();
            if (pTypeName.rfind("struct ", 0) == 0) pTypeName.erase(0, 7);
            if (pTypeName.rfind("class ", 0) == 0) pTypeName.erase(0, 6);

            // Если нашли структуру такого же типа, которую Warmup уже успешно распарсил
            if (pTypeName == targetTypeName && !p.structInfo.members.empty()) {
                for (const auto& member : p.structInfo.members) {
                    CallSiteParamField field;
                    field.fieldName = member.name;
                    field.offset = member.offset;
                    field.size = member.size;
                    field.typeName = member.typeName;
                    field.pRuntimeMemory = reinterpret_cast<void*>(reinterpret_cast<char*>(pBase) + member.offset);

                    desc.fields.push_back(field);
                }
                fieldsFound = true;
                break;
            }
        }

        // Если в прогретом кэше типа пока нет (например, новая структура drws ни разу не вызывалась в eval),
        // то только в этом крайнем случае аккуратно стучимся в PDB напрямую, если поток прогрева уже завершен
        if (!fieldsFound) {
            // Резервный ленивый вызов, если тип абсолютно новый
            std::wstring wTypeName(targetTypeName.begin(), targetTypeName.end());
            std::vector<std::string> fNames;
            std::vector<DWORD> fOffsets;
            std::vector<DWORD> fSizes;
            std::vector<std::string> fTypes;

            // Вызываем только если EnsureDbgHelpInitialized() не заблокирован фоновым потоком
            if (g_isDbgHelpGlobalReady && LoadStructMetadataDirect(wTypeName.c_str(), fNames, fOffsets, fSizes, fTypes)) {
                for (size_t i = 0; i < fOffsets.size(); ++i) {
                    CallSiteParamField field;
                    field.fieldName = fNames[i];
                    field.offset = fOffsets[i];
                    field.size = fSizes[i];
                    field.typeName = fTypes[i];
                    field.pRuntimeMemory = reinterpret_cast<void*>(reinterpret_cast<char*>(pBase) + fOffsets[i]);

                    desc.fields.push_back(field);
                }
                fieldsFound = true;
            }
        }

        if (fieldsFound) {
            registry[fileKey] = desc;
        }
    }



    // Чистый агрегатный холдер для source_location
    struct CallSiteTracker {
        std::source_location loc;

        template<typename TStruct>
        const TStruct& Bind(const TStruct& obj) const {
            ManualRegisterCallSite<TStruct>(const_cast<TStruct*>(&obj), loc);
            return obj;
        }
    };
}

// ОБНОВЛЕННЫЕ МАКРОСЫ С std::source_location 
// Вызывая std::source_location::current() прямо здесь, мы заставляем компилятор
// подставить контекст СТРОКИ ВЫЗОВА, так как это выражение аргумента по умолчанию!
#define reflect(ArgumentType, ArgumentName) \
    ArgumentType ArgumentName, const ::LivePT::CallSiteTracker& _lpt_tracker = ::LivePT::CallSiteTracker{ \
        std::source_location::current() \
    }

#define reflect_init(ArgumentName) \
    _lpt_tracker.Bind(ArgumentName)


struct drws {
    int x;
    int y;
};

// Компилируется без единой ошибки! 'p' внутри функции ведет себя в точности как drws
void drw(reflect(drws, p)) {
    auto a = &p; // Можно спокойно брать адрес
    int currentX = p.x; // Полный нативный доступ к полям

    static bool logged = false;
    if (!logged) {
        auto& registry = ::LivePT::GetCallSitesRegistry();

        ::LivePT::Log("=== LivePT CALL SITES REGISTRY ===");
        for (const auto& [key, desc] : registry) {
            ::LivePT::Log("Call Site Key: " + key);
            ::LivePT::Log("File: " + desc.fileName + " | Line: " + std::to_string(desc.line));
            ::LivePT::Log("Base Address in RAM: " + std::to_string(reinterpret_cast<UINT_PTR>(desc.pBaseAddress)));

            for (const auto& field : desc.fields) {
                ::LivePT::Log("  -> Field: ." + field.fieldName +
                    " | Type: " + field.typeName +
                    " | Offset: " + std::to_string(field.offset) +
                    " | Size: " + std::to_string(field.size) +
                    " | Live Value: " + std::to_string(*reinterpret_cast<int*>(field.pRuntimeMemory)));
            }
        }
        ::LivePT::Log("==================================");
        logged = true; // Выведем один раз, чтобы не спамить в цикле
    }
    // ----------------------------------------
}

