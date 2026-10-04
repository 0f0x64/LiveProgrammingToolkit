namespace LivePT {

    // Структура для возврата точных выверенных координат текстового токена
    struct PreciseCoord {
        long line = 0;
        long column = 0;
        size_t absoluteOffset = std::wstring::npos;
    };

    // Сканирует текст файла НАЗАД от сырых координат компилятора, находя точное начало L"eval"
    inline PreciseCoord FindEvalTokenBackward(const std::wstring& fileText, long rawLine, long rawColumn) {
        PreciseCoord result;
        if (fileText.empty()) return result;

        std::wstring macroName = GetConfiguredMacroName(); // Получаем L"eval"

        // КРИТИЧЕСКИЙ АРХИТЕКТУРНЫЙ СДВИГ: 
        // Препроцессор компилятора (MSVC/Clang) ВСЕГДА раздувает символ '\t' до 8 пробелов.
        // Чтобы стартовая точка поиска не улетела влево, на этапе 2 используем жесткий шаг = 8.
        const long COMPILER_TAB_SIZE = 8;

        // 1. Быстро переходим к началу физической строки rawLine
        size_t lineStartOffset = 0;
        long currentLineIdx = 1;
        while (currentLineIdx < rawLine && lineStartOffset < fileText.length()) {
            size_t nextNL = fileText.find(L'\n', lineStartOffset);
            if (nextNL != std::wstring::npos) {
                lineStartOffset = nextNL + 1;
                currentLineIdx++;
            }
            else {
                break;
            }
        }

        // 2. Вычисляем символ в строке, соответствующий rawColumn (с компиляторным учетом табов = 8)
        size_t nextNL = fileText.find(L'\n', lineStartOffset);
        if (nextNL == std::wstring::npos) nextNL = fileText.length();
        std::wstring lineText = fileText.substr(lineStartOffset, nextNL - lineStartOffset);

        long currentVisualCol = 1;
        size_t paramCharIdx = lineText.length();
        for (size_t i = 0; i < lineText.length(); ++i) {
            if (currentVisualCol >= rawColumn) {
                paramCharIdx = i;
                break;
            }
            // Разворачиваем табы строго по логике компилятора (шаг 8)
            currentVisualCol += (lineText[i] == L'\t') ? (COMPILER_TAB_SIZE - ((currentVisualCol - 1) % COMPILER_TAB_SIZE)) : 1;
        }

        // Абсолютное смещение в файле, откуда начинаем искать макрос назад
        size_t searchOrigin = lineStartOffset + paramCharIdx;
        if (searchOrigin > fileText.length()) searchOrigin = fileText.length();

        // 3. Лексическое сканирование текста НАЗАД в поисках макроса
        size_t targetMacroStart = std::wstring::npos;
        while (searchOrigin > 0) {
            size_t rfindPos = fileText.rfind(macroName, searchOrigin);
            if (rfindPos == std::wstring::npos) break;

            // Синтаксическая валидация границ токена (исключаем my_eval, eval_state и т.д.)
            bool validLeft = (rfindPos == 0 || (!iswalnum(fileText[rfindPos - 1]) && fileText[rfindPos - 1] != L'_'));
            bool validRight = (rfindPos + macroName.length() >= fileText.length() ||
                (!iswalnum(fileText[rfindPos + macroName.length()]) && fileText[rfindPos + macroName.length()] != L'_'));

            if (validLeft && validRight) {
                targetMacroStart = rfindPos;
                break;
            }

            if (rfindPos == 0) break;
            searchOrigin = rfindPos - 1;
        }

        if (targetMacroStart == std::wstring::npos) return result;

        // 4. Математический пересчет абсолютного смещения в честные Line и Column
        long preciseLine = 1;
        size_t preciseLineStartOffset = 0;
        size_t scanOffset = 0;

        while (scanOffset < targetMacroStart) {
            size_t nextNLOffset = fileText.find(L'\n', scanOffset);
            if (nextNLOffset != std::wstring::npos && nextNLOffset < targetMacroStart) {
                preciseLine++;
                preciseLineStartOffset = nextNLOffset + 1;
                scanOffset = nextNLOffset + 1;
            }
            else {
                break;
            }
        }

        // ВНИМАНИЕ: Переводим остаток строки в визуальные колонки для финальной привязки к Visual Studio!
        // Внутри GetVisualColumn() честно используется GetVSTabSize(), возвращающий пользовательские 4 пробела.
        std::wstring preciseLineText = fileText.substr(preciseLineStartOffset, targetMacroStart - preciseLineStartOffset);
        long preciseColumn = GetVisualColumn(preciseLineText, preciseLineText.length());

        result.line = preciseLine;
        result.column = preciseColumn;
        result.absoluteOffset = targetMacroStart;
        return result;
    }

    inline std::wstring ReadFileFromDisk(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);
        if (!file.is_open()) return L"";

        // Читаем весь файл в string
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        if (content.empty()) return L"";

        // Переводим UTF-8 / ANSI контент в std::wstring для работы с вашими функциями
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), NULL, 0);
        if (size_needed <= 0) {
            // Если не UTF-8, пробуем текущую ANSI кодировку
            size_needed = MultiByteToWideChar(CP_ACP, 0, content.c_str(), static_cast<int>(content.size()), NULL, 0);
            if (size_needed <= 0) return L"";
            std::wstring wstr(size_needed, 0);
            MultiByteToWideChar(CP_ACP, 0, content.c_str(), static_cast<int>(content.size()), wstr.data(), size_needed);
            return wstr;
        }

        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), wstr.data(), size_needed);
        return wstr;
    }

    bool baseIsAligned = false;

    inline void AlignDatabaseFastFromDisk() {
        
        if (baseIsAligned) return;
        baseIsAligned = true;

        // Достаем оригинальное дерево, накопленное компилятором в Pre-Main
        auto& fileCompileTree = GetFileCompileTree();
        auto& params = LivePT::getParamDesc();

        // 1. Идем строго по файлам из хэшмапы. Порядок файлов случайный, но заходим в каждый ОДИН раз.
        for (auto& [filePath, fileMap] : fileCompileTree) {

            // 2. Читаем файл с диска ровно один раз
            std::wstring fileText = ReadFileFromDisk(filePath);
            if (fileText.empty()) {
                Log("[LivePT Disk Init] File read error or file is empty: " + filePath);
                continue;
            }

            // 3. Внутренний map гарантирует, что мы идем по тексту строго сверху вниз
            for (auto& [staticKey, paramId] : fileMap) {
                if (paramId < 0 || paramId >= static_cast<int>(params.size())) continue;

                auto& p = params[paramId];

                // Ищем честный 'eval' назад от сырых координат компилятора
                PreciseCoord precise = FindEvalTokenBackward(fileText, staticKey.line, staticKey.column);

                if (precise.absoluteOffset != std::wstring::npos) {
                    // Жестко выставляем координаты на 'e' слова eval
                    p.line = static_cast<int>(precise.line);
                    p.column = static_cast<int>(precise.column);
                    p.loaded = true; // Параметр готов к рантайм-сдвигам
                }
            }
        }
    }


    inline void ShiftDatabaseCoordinates(const std::string& targetFile, long targetLine, long visualStartCol, int lineDelta, int columnDelta) {
        auto& params = LivePT::getParamDesc();
        std::string normalizedTarget = LivePT::NormalizePath(targetFile.c_str());

        for (auto& p : params) {
            if (p.fileName != normalizedTarget) continue;

            // 1. Сдвиг строк для всех элементов строго ниже изменения
            if (lineDelta != 0 && p.line > targetLine) {
                p.line += lineDelta;
            }

            // 2. Сдвиг колонок для элементов на той же строке правее изменения
            if (columnDelta != 0 && p.line == targetLine && p.column > visualStartCol) {
                p.column += columnDelta;
            }
        }
    }

} // namespace LivePT
