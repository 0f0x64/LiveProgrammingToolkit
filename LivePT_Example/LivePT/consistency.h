namespace LivePT {

    // Структура для возврата точных выверенных координат текстового токена
    struct PreciseCoord {
        long line = 0;
        long column = 0;
        size_t absoluteOffset = std::wstring::npos;
    };

    inline PreciseCoord FindEvalTokenBackward(const std::wstring& fileText, const std::string& filePath, long rawLine, long rawColumn, size_t minAllowedOffset = 0) {
        PreciseCoord result;
        if (fileText.empty()) return result;

        std::wstring macroName = GetConfiguredMacroName();
        long tabSize = GetVSTabSize(); // Динамический размер вместо жестко зашитого COMPILER_TAB_SIZE = 8

        // 1. Переходим к началу физической строки rawLine
        size_t lineStartOffset = 0;
        long currentLineIdx = 1;
        while (currentLineIdx < rawLine && lineStartOffset < fileText.length()) {
            size_t nextNL = fileText.find(L'\n', lineStartOffset);
            if (nextNL != std::wstring::npos) {
                lineStartOffset = nextNL + 1;
                currentLineIdx++;
            }
            else break;
        }

        // 2. Выделяем текст текущей строки
        size_t nextNL = fileText.find(L'\n', lineStartOffset);
        if (nextNL == std::wstring::npos) nextNL = fileText.length();
        std::wstring lineText = fileText.substr(lineStartOffset, nextNL - lineStartOffset);

        // Вычисляем символ в строке, соответствующий rawColumn
        long currentVisualCol = 1;
        size_t paramCharIdx = lineText.length();
        for (size_t i = 0; i < lineText.length(); ++i) {
            if (currentVisualCol >= rawColumn) {
                paramCharIdx = i;
                break;
            }
            if (lineText[i] == L'\r') continue; // Игнорируем возврат каретки

            if (lineText[i] == L'\t') {
                currentVisualCol += (tabSize - ((currentVisualCol - 1) % tabSize));
            }
            else {
                currentVisualCol += 1;
            }
        }

        size_t searchOrigin = lineStartOffset + paramCharIdx;
        if (searchOrigin > fileText.length()) searchOrigin = fileText.length();

        if (searchOrigin < minAllowedOffset) {
            return result;
        }

        // 3. Лексическое сканирование текста НАЗАД в поисках макроса
        size_t targetMacroStart = std::wstring::npos;
        while (searchOrigin > 0 && searchOrigin >= minAllowedOffset) {
            size_t rfindPos = fileText.rfind(macroName, searchOrigin);
            if (rfindPos == std::wstring::npos || rfindPos < minAllowedOffset) break;

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

        if (targetMacroStart == std::wstring::npos) {
            return result;
        }

        // 4. Математический пересчет абсолютного смещения в Line и Column
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
            else break;
        }

        std::wstring preciseLineText = fileText.substr(preciseLineStartOffset, targetMacroStart - preciseLineStartOffset);

        // КРИТИЧЕСКАЯ ОЧИСТКА ДЛЯ CRLF: вырезаем все возможные \r, чтобы они не искажали GetVisualColumn
        preciseLineText.erase(std::remove(preciseLineText.begin(), preciseLineText.end(), L'\r'), preciseLineText.end());

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

        std::wstring wstr;
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), NULL, 0);
        if (size_needed > 0) {
            wstr.resize(size_needed);
            MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), wstr.data(), size_needed);
        }
        else {
            // Если не UTF-8, пробуем текущую ANSI кодировку (например, CP1251 для русских комментариев)
            size_needed = MultiByteToWideChar(CP_ACP, 0, content.c_str(), static_cast<int>(content.size()), NULL, 0);
            if (size_needed <= 0) return L"";
            wstr.resize(size_needed);
            MultiByteToWideChar(CP_ACP, 0, content.c_str(), static_cast<int>(content.size()), wstr.data(), size_needed);
        }

        // ЗАЩИТА ОТ BOM: Удаляем невидимый маркер Unicode в начале файла, чтобы он не сдвигал колонки
        if (!wstr.empty() && wstr[0] == L'\xFEFF') {
            wstr.erase(0, 1);
        }

        return wstr;
    }


    bool baseIsAligned = false;

    inline void AlignDatabaseFastFromDisk() {

        if (baseIsAligned) return;
        baseIsAligned = true;

        auto& fileCompileTree = GetFileCompileTree();
        auto& params = LivePT::getParamDesc();
        std::wstring macroName = GetConfiguredMacroName();

        // 1. Идем строго по файлам из хэшмапы.
        for (auto& [filePath, fileMap] : fileCompileTree) {

            // 2. Читаем файл с диска ровно один раз
            std::wstring fileText = ReadFileFromDisk(filePath);
            if (fileText.empty()) {
                Log("[LivePT Disk Init] File read error or file is empty: " + filePath);
                continue;
            }

            // Переменная для отслеживания нижней границы предыдущего макроса в этом файле
            size_t lastValidMacroEndOffset = 0;

            // 3. Внутренний map гарантирует, что мы идем по тексту строго сверху вниз
            for (auto& [staticKey, paramId] : fileMap) {
                if (paramId < 0 || paramId >= static_cast<int>(params.size())) continue;

                auto& p = params[paramId];

                // Передаем filePath для логов и lastValidMacroEndOffset для защиты от захода за границу предыдущего
                PreciseCoord precise = FindEvalTokenBackward(fileText, filePath, staticKey.line, staticKey.column, lastValidMacroEndOffset);

                if (precise.absoluteOffset != std::wstring::npos) {
                    // Жестко выставляем координаты на 'e' слова eval
                    p.line = static_cast<int>(precise.line);
                    p.column = static_cast<int>(precise.column);
                    p.loaded = true; // Параметр готов к рантайм-сдвигам

                    // Фиксируем конец текущего макроса: его начало + длина самого слова (например, L"eval")
                    lastValidMacroEndOffset = precise.absoluteOffset + macroName.length();
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
