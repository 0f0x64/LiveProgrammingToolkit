namespace LivePT {

    // Структура для возврата точных выверенных координат текстового токена
    struct PreciseCoord {
        long line = 0;
        long column = 0;
        size_t absoluteOffset = std::wstring::npos;
    };

    inline PreciseCoord FindEvalTokenBackward(const std::wstring& fileText, const std::string& filePath, long rawLine, long rawColumn, const std::string& macroNameStr, size_t minAllowedOffset = 0) {
        PreciseCoord result;
        if (fileText.empty()) return result;

        // Конвертируем имя макроса/функции из базы данных (std::string) в wstring для работы с текстом
        std::wstring macroName(macroNameStr.begin(), macroNameStr.end());
        long tabSize = GetVSTabSize();

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
            if (lineText[i] == L'\r') continue;

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

        // 3. Лексическое сканирование текста НАЗАД в поисках конкретного токена p.macroName
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

        for (auto& [filePath, fileMap] : fileCompileTree) {
            std::wstring fileText = ReadFileFromDisk(filePath);
            if (fileText.empty()) {
                Log("[LivePT Disk Init] File read error or file is empty: " + filePath);
                continue;
            }

            // Вырезаем \r для идеальной синхронности с рантайм-буфером VS
            fileText.erase(std::remove(fileText.begin(), fileText.end(), L'\r'), fileText.end());

            for (auto& [staticKey, paramId] : fileMap) {
                if (paramId < 0 || paramId >= static_cast<int>(params.size())) continue;

                auto& p = params[paramId];
                std::wstring wMacroName(p.macroName.begin(), p.macroName.end());

                // 1. Переходим к физической строке, которую передал компилятор (staticKey.line)
                size_t lineStartOffset = 0;
                long currentLineIdx = 1;
                while (currentLineIdx < staticKey.line && lineStartOffset < fileText.length()) {
                    size_t nextNL = fileText.find(L'\n', lineStartOffset);
                    if (nextNL != std::wstring::npos) {
                        lineStartOffset = nextNL + 1;
                        currentLineIdx++;
                    }
                    else break;
                }

                size_t nextNL = fileText.find(L'\n', lineStartOffset);
                if (nextNL == std::wstring::npos) nextNL = fileText.length();
                std::wstring lineText = fileText.substr(lineStartOffset, nextNL - lineStartOffset);

                // 2. Считаем индекс макроса на этой строке среди его соседей
                int orderIndexOnLine = 0;
                for (const auto& [subKey, subParamId] : fileMap) {
                    if (subKey.line == staticKey.line && subKey.column < staticKey.column) {
                        orderIndexOnLine++;
                    }
                }

                // 3. Пробуем найти токен на текущей строке
                size_t macroPosInLine = lineText.find(wMacroName);
                int currentScanIndex = 0;

                while (macroPosInLine != std::wstring::npos) {
                    size_t absPos = lineStartOffset + macroPosInLine;
                    bool validLeft = (absPos == 0 || (!iswalnum(fileText[absPos - 1]) && fileText[absPos - 1] != L'_'));
                    bool validRight = (absPos + wMacroName.length() >= fileText.length() ||
                        (!iswalnum(fileText[absPos + wMacroName.length()]) && fileText[absPos + wMacroName.length()] != L'_'));

                    if (validLeft && validRight) {
                        if (currentScanIndex == orderIndexOnLine) break;
                        currentScanIndex++;
                    }
                    macroPosInLine = lineText.find(wMacroName, macroPosInLine + 1);
                }

                // 4. СКАНИРУЮЩИЙ НАЗАД ПОИСК ДЛЯ МНОГОСТРОЧНИКОВ:
                // Если слово "eval" не нашлось на строке закрытия (как у .pos), мы идем по тексту 
                // строго НАЗАД (вверх по файлу), чтобы найти истинное начало этого макроса!
                size_t finalAbsoluteOffset = std::wstring::npos;

                if (macroPosInLine != std::wstring::npos) {
                    finalAbsoluteOffset = lineStartOffset + macroPosInLine;
                }
                else {
                    size_t reverseSearchStart = nextNL;
                    while (reverseSearchStart > 0) {
                        size_t rfindPos = fileText.rfind(wMacroName, reverseSearchStart);
                        if (rfindPos == std::wstring::npos) break;

                        bool vL = (rfindPos == 0 || (!iswalnum(fileText[rfindPos - 1]) && fileText[rfindPos - 1] != L'_'));
                        bool vR = (rfindPos + wMacroName.length() >= fileText.length() || (!iswalnum(fileText[rfindPos + wMacroName.length()]) && fileText[rfindPos + wMacroName.length()] != L'_'));

                        if (vL && vR) {
                            size_t openBrk = fileText.find(L'(', rfindPos + wMacroName.length());
                            if (openBrk != std::wstring::npos) {
                                size_t closeBrk = FindMacroCloseBracket(fileText, openBrk);
                                if (closeBrk != std::wstring::npos && closeBrk >= lineStartOffset && closeBrk <= nextNL) {
                                    finalAbsoluteOffset = rfindPos; // Нашли истинный старт многострочника
                                    break;
                                }
                            }
                        }
                        if (rfindPos == 0) break;
                        reverseSearchStart = rfindPos - 1;
                    }
                }

                // 5. Пересчет и фиксация координат в params
                if (finalAbsoluteOffset != std::wstring::npos) {
                    long preciseLine = 1;
                    size_t preciseLineStartOffset = 0;
                    size_t scanOffset = 0;

                    while (scanOffset < finalAbsoluteOffset) {
                        size_t nextNLOffset = fileText.find(L'\n', scanOffset);
                        if (nextNLOffset != std::wstring::npos && nextNLOffset < finalAbsoluteOffset) {
                            preciseLine++;
                            preciseLineStartOffset = nextNLOffset + 1;
                            scanOffset = nextNLOffset + 1;
                        }
                        else break;
                    }

                    size_t nextLineEnd = fileText.find(L'\n', preciseLineStartOffset);
                    if (nextLineEnd == std::wstring::npos) nextLineEnd = fileText.length();

                    // ИСПРАВЛЕНО: берем подстроку от правильного точного смещения начала строки
                    std::wstring preciseLineText = fileText.substr(preciseLineStartOffset, nextLineEnd - preciseLineStartOffset);

                    size_t localIdx = finalAbsoluteOffset - preciseLineStartOffset;
                    long preciseColumn = GetVisualColumn(preciseLineText, localIdx);

                    p.line = static_cast<int>(preciseLine);
                    p.column = static_cast<int>(preciseColumn);
                    p.loaded = true;
                }
                else {
                    p.line = static_cast<int>(staticKey.line);
                    p.column = static_cast<int>(staticKey.column);
                    p.loaded = true;
                }
            }
        }
    }


    inline void ShiftDatabaseCoordinates(const std::string& targetFile, long targetLine, long visualStartCol, int lineDelta, int columnDelta) {
        // Если колоночный дельта пришел нулевым и строки не менялись, делать нечего
        if (lineDelta == 0 && columnDelta == 0) return;

        auto& params = LivePT::getParamDesc();
        std::string normalizedTarget = LivePT::NormalizePath(targetFile.c_str());
        long tabSize = GetVSTabSize();

        for (auto& p : params) {
            if (p.fileName != normalizedTarget) continue;

            // 1. Вычисляем потенциальные новые координаты ПОСЛЕ сдвига
            long newParamLine = p.line;
            long newParamColumn = p.column;

            bool affectsLine = (lineDelta != 0 && p.line > targetLine);
            bool affectsColumn = (columnDelta != 0 && p.line == targetLine && p.column > visualStartCol);

            if (affectsLine)   newParamLine += lineDelta;
            if (affectsColumn) newParamColumn += columnDelta;

            // 2. Если элемент попадает под сдвиг, валидируем его БУДУЩЕЕ положение в НОВОМ тексте
            if (affectsLine || affectsColumn) {

                bool validationFailed = true;

                if (!g_currentFileFullText.empty()) {
                    // Ищем физическое смещение начала целевой строки в новом тексте
                    size_t lineStartOffset = 0;
                    long currentLineIdx = 1;
                    while (currentLineIdx < newParamLine && lineStartOffset < g_currentFileFullText.length()) {
                        size_t nextNL = g_currentFileFullText.find(L'\n', lineStartOffset);
                        if (nextNL != std::wstring::npos) {
                            lineStartOffset = nextNL + 1;
                            currentLineIdx++;
                        }
                        else break;
                    }

                    // Вырезаем текст строки
                    size_t nextNL = g_currentFileFullText.find(L'\n', lineStartOffset);
                    if (nextNL == std::wstring::npos) nextNL = g_currentFileFullText.length();
                    std::wstring lineText = g_currentFileFullText.substr(lineStartOffset, nextNL - lineStartOffset);

                    // ПЕРЕВОД: виз. колонка -> физический индекс wchar_t (с учетом динамического размера табов)
                    long currentVisualCol = 1;
                    size_t paramCharIdx = lineText.length();
                    for (size_t i = 0; i < lineText.length(); ++i) {
                        if (currentVisualCol >= newParamColumn) {
                            paramCharIdx = i;
                            break;
                        }
                        if (lineText[i] == L'\r') continue; // Полное игнорирование CRLF артефактов

                        // Честный расчет шага табуляции по сетке Visual Studio
                        if (lineText[i] == L'\t') {
                            currentVisualCol += tabSize - ((currentVisualCol - 1) % tabSize);
                        }
                        else {
                            currentVisualCol++;
                        }
                    }

                    // [ИСПРАВЛЕНО]: Переводим ANSI-имя из базы данных (p.macroName) в wstring для валидации
                    std::wstring macroName(p.macroName.begin(), p.macroName.end());

                    // Проверяем, действительно ли по вычисленному индексу символа сидит наш макрос/функция
                    if (paramCharIdx + macroName.length() <= lineText.length()) {
                        std::wstring possibleMacro = lineText.substr(paramCharIdx, macroName.length());
                        if (possibleMacro == macroName) {
                            validationFailed = false; // Проверка пройдена!
                        }
                    }
                }

                // 3. Защитный барьер: если сдвиг уводит в мусорный код, изолируем только этот элемент
                if (validationFailed) {
                    Log("[LivePT Integrity Error] Shift validation failed! Element would move to Line " +
                        std::to_string(newParamLine) + ", Col " + std::to_string(newParamColumn) +
                        " but it doesn't contain '" + p.macroName + "'. Shift skipped for this element.");
                    continue; // Мягко пропускаем порчу этой координаты, но продолжаем двигать соседние
                }

                // Переносим проверенные координаты в рабочую базу данных параметров
                p.line = newParamLine;
                p.column = newParamColumn;
            }
        }
    }


} // namespace LivePT
