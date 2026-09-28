// User space settings

#define LivePT_WheelEditMode true // true for mouse drag, switch, enums with context menu
#define LivePT_WindowManagement true //auto split screen on single monitor
#define LivePT_AppToSecondaryDisplay false //auto move your app to second monitor
#define LivePT_TriggerButton VK_LBUTTON  // you can use VK_MBUTTON for alternative

// -------------------

#if LivePT_EditMode

    #include <vector>
    #include <string>
    #include <unordered_map>
    #include <map>
    #include <variant>
    #include <cstdlib>
    #include <algorithm>
    #include <sstream>
    #include <type_traits>
    #include <cctype>
    #include <string_view>
    #include <array>
    #include <charconv>
    #include <limits>
    #include <any>
    #include <fstream>
    #include <source_location>
    #include <atlbase.h>
    #include <tlhelp32.h>
    #include <functional>
    #include <unordered_set>

    #pragma comment(lib, "dbghelp.lib")
    #include <dbghelp.h>


namespace LivePT {

    inline std::wstring GetConfiguredMacroName() {
        static const std::wstring cachedMacroName = []() {
            constexpr auto location = std::source_location::current();
            std::string selfPath = location.file_name();

            std::ifstream file(selfPath);
            if (!file.is_open()) {
                return std::wstring(L"eval");
            }

            std::string line;
            std::string marker = "//EVAL_NAME_MARKER - DO'NT TOUCH THIS LINE";

            while (std::getline(file, line)) {

                if (line.find(marker) != std::string::npos) {

                    while (std::getline(file, line)) {

                        line.erase(0, line.find_first_not_of(" \t\r\n"));
                        if (line.empty()) continue; 

                        if (line.rfind("#define", 0) == 0) {
                            line.erase(0, 7); 
                            line.erase(0, line.find_first_not_of(" \t\r\n"));

                            size_t endPos = line.find_first_of(" \t(");
                            if (endPos != std::string::npos) {
                                std::string name = line.substr(0, endPos);
                                return std::wstring(name.begin(), name.end());
                            }
                        }
                    }
                    break;
                }
            }
            return std::wstring(L"eval");
            }();

        return cachedMacroName;
    }

    inline void Log(const std::string& text, bool nl = true) {
        OutputDebugStringA(text.c_str());
        if (nl) OutputDebugStringA("\n");
    }
}

    #include "DbgHelpRef.h"
    #include "eval.h"
    #include "uiCallBackBridge.h"
    #include "vsEditor.h"

    #if LivePT_WheelEditMode
        #include "liveWheelEdit.h"
    #endif

    #if LivePT_WindowManagement
        #include "windowManagement.h"
    #endif

namespace LivePT {

    class LptRuntimeLifetimeManager {
    private:
        HRESULT m_coInitResult = E_FAIL;

    public:
        LptRuntimeLifetimeManager() {

            m_coInitResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

            if (FAILED(m_coInitResult)) {
                LivePT::Log("COM Infrastructure error");
            }
        }

        ~LptRuntimeLifetimeManager() {

            if (pDTE) {
                pDTE->Release();
                pDTE = nullptr;
            }

            if (m_coInitResult == S_OK || m_coInitResult == S_FALSE) {
                CoUninitialize();
            }
        }
    };

    static LptRuntimeLifetimeManager g_runtimeLifetimeManager;

    
    inline void WarmupAllDatabaseParams() {
        static bool isWarmedUp = false;
        if (isWarmedUp) return;

        auto& params = LivePT::getParamDesc();
        const size_t totalParams = params.size();
        if (totalParams == 0) return;

        // Выделяем память под кэш метаданных заранее, чтобы избежать реаллокаций в куче
        std::unordered_map<std::string, decltype(params[0].structInfo.members)> structMetadataCache;
        structMetadataCache.reserve(8); // Задаем хэш-таблице стартовую емкость (для pos2, color3, size2 и т.д.)

        std::string typeNameAnsi;
        typeNameAnsi.reserve(64); // Кэшируем буфер строки, чтобы не аллоцировать память на каждом шаге цикла

        for (size_t i = 0; i < totalParams; ++i) {
            auto& p = params[i]; // Работаем строго по ссылке, убираем копирование тяжелых структур

            // Быстрый отсев: если это enum, или структура уже прогрета, или значения нет — мгновенный пропуск
            if (p.enumInfo.isEnum || p.structInfo.isStruct || !p.value.has_value()) {
                continue;
            }

            // Получаем ANSI имя типа из рантайма C++
            typeNameAnsi = p.value.type().name();
            if (typeNameAnsi.empty()) continue;

            // Ассемблерно-быстрая очистка префиксов MSVC без вызова тяжелого .erase() по центру строки
            const char* typePtr = typeNameAnsi.c_str();
            if (strncmp(typePtr, "struct ", 7) == 0) typePtr += 7;
            else if (strncmp(typePtr, "class ", 6) == 0) typePtr += 6;

            // Отсекаем примитивные типы за 1 такт процессора
            if (*typePtr == 'f' && strcmp(typePtr, "float") == 0) continue;
            if (*typePtr == 'i' && strcmp(typePtr, "int") == 0) continue;
            if (*typePtr == 'b' && strcmp(typePtr, "bool") == 0) continue;
            if (*typePtr == 'd' && strcmp(typePtr, "double") == 0) continue;
            if (*typePtr == 'c' && strcmp(typePtr, "char") == 0) continue;

            // СВЕРХБЫСТРЫЙ КЭШ: проверяем, делали ли мы уже этот тип
            auto it = structMetadataCache.find(typePtr);
            if (it != structMetadataCache.end()) {
                p.structInfo.members = it->second; // Мгновенное блочное копирование вектора из L1/L2 кэша процессора
                p.structInfo.isStruct = true;
                continue;
            }

            // Если тип встретился впервые — парсим PDB-символы через DbgHelp
            size_t len = strlen(typePtr);
            std::wstring wTypeName(len, L'\0');
            MultiByteToWideChar(CP_ACP, 0, typePtr, static_cast<int>(len), wTypeName.data(), static_cast<int>(len));

            std::vector<std::string> fNames;
            std::vector<DWORD> fOffsets;
            std::vector<DWORD> fSizes;
            std::vector<std::string> fTypes;

            if (LoadStructMetadataDirect(wTypeName.c_str(), fNames, fOffsets, fSizes, fTypes)) {
                decltype(p.structInfo.members) loadedMembers;
                const size_t fieldsCount = fOffsets.size();
                loadedMembers.reserve(fieldsCount); // Исключаем реаллокации при пуше полей

                for (size_t k = 0; k < fieldsCount; ++k) {
                    loadedMembers.push_back({ std::move(fNames[k]), fOffsets[k], fSizes[k], std::move(fTypes[k]) });
                }

                p.structInfo.members = loadedMembers;
                p.structInfo.isStruct = true;

                // Сохраняем готовую структуру полей в кэш для всех последующих аналогичных типов
                structMetadataCache.emplace(typePtr, std::move(loadedMembers));
            }
        }

        isWarmedUp = true;
        Log("[LivePT Hyper-Warmup] Complete. Processing speed optimized to the limit.");
    }

    void ProcessEdit()
    {
        WarmupAllDatabaseParams();

        #if LivePT_WindowManagement
            GetWindowManager().Tick();
        #endif

        #if LivePT_WheelEditMode
            FlushPendingWritesToVS();
            Update();
        #endif

            vsEditor();
    }

}

//EVAL_NAME_MARKER - DO'NT TOUCH THIS LINE
#define eval(...) \
    LivePT::LazyTypeDetector< \
        std::decay_t<decltype(__VA_ARGS__)>, \
        LivePT::FixedString<260>{__FILE__}, \
        static_cast<int>(__LINE__), \
        static_cast<int>(__builtin_COLUMN()) \
    >(__VA_ARGS__)


#else

#define eval(...) __VA_ARGS__

namespace LivePT {
    void ProcessEdit() {}
}

#endif