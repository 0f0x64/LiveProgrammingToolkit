namespace DrugLogic {

    // Helper functions for color translation (encapsulated inside DrugLogic)
    inline void LPT_RGBtoHSV(float r, float g, float b, float& h, float& s, float& v) {
        r /= 255.0f; g /= 255.0f; b /= 255.0f;
        float maxVal = (std::max)({ r, g, b });
        float minVal = (std::min)({ r, g, b });
        float delta = maxVal - minVal;

        v = maxVal;
        s = (maxVal > 0.0f) ? (delta / maxVal) : 0.0f;
        h = 0.0f;

        if (delta > 0.0f) {
            if (maxVal == r)      h = 60.0f * (std::fmod(((g - b) / delta), 6.0f));
            else if (maxVal == g) h = 60.0f * (((b - r) / delta) + 2.0f);
            else if (maxVal == b) h = 60.0f * (((r - g) / delta) + 4.0f);
            if (h < 0.0f) h += 360.0f;
        }
    }

    inline void LPT_HSVtoRGB(float h, float s, float v, float& r, float& g, float& b) {
        float c = v * s;
        float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
        float m = v - c;
        float r1 = 0, g1 = 0, b1 = 0;
        int hDiv = static_cast<int>(h / 60.0f) % 6;

        switch (hDiv) {
        case 0: r1 = c; g1 = x; b1 = 0; break;
        case 1: r1 = x; g1 = c; b1 = 0; break;
        case 2: r1 = 0; g1 = c; b1 = x; break;
        case 3: r1 = 0; g1 = x; b1 = c; break;
        case 4: r1 = x; g1 = 0; b1 = c; break;
        case 5: r1 = c; g1 = 0; b1 = x; break;
        }
        r = (r1 + m) * 255.0f;
        g = (g1 + m) * 255.0f;
        b = (b1 + m) * 255.0f;
    }

    inline std::string color3Drag(const std::string& currentArgsStr, const ::LivePT::DragMathInput& input) {
        int r = 0, g = 0, b = 0;

        // Extract values regardless of formatting (.r=82 or pure numbers)
        if (sscanf_s(currentArgsStr.c_str(), "%*[^0-9-]%d%*[^0-9-]%d%*[^0-9-]%d", &r, &g, &b) != 3) {
            if (sscanf_s(currentArgsStr.c_str(), "%d, %d, %d", &r, &g, &b) != 3) {
                return currentArgsStr;
            }
        }

        if (input.shift) {
            float h = 0, s = 0, v = 0;
            LPT_RGBtoHSV((float)r, (float)g, (float)b, h, s, v);

            if (input.mouseFrameDeltaX != 0) {
                h += static_cast<float>(input.mouseFrameDeltaX) * 0.5f;
                h = std::fmod(h, 360.0f);
                if (h < 0.0f) h += 360.0f;
            }

            if (input.mouseFrameDeltaY != 0) {
                float deltaS = static_cast<float>(input.mouseFrameDeltaY) * 0.005f;
                s += deltaS;
                s = std::clamp(s, 0.0f, 1.0f);

                if (deltaS > 0.0f && v < 0.5f) {
                    v += deltaS * 0.5f;
                    v = std::clamp(v, 0.0f, 1.0f);
                }
                v = v * s + 0.5f * (1.0f - s);
                v = std::clamp(v, 0.0f, 1.0f);
            }

            float fr = 0, fg = 0, fb = 0;
            LPT_HSVtoRGB(h, s, v, fr, fg, fb);
            r = std::clamp(static_cast<int>(fr), 0, 255);
            g = std::clamp(static_cast<int>(fg), 0, 255);
            b = std::clamp(static_cast<int>(fb), 0, 255);
        }
        else {
            float multiplier = 1.0f + (input.mouseFrameDeltaY * 0.005f);
            if (multiplier < 0.0f) multiplier = 0.0f;

            r = std::clamp(static_cast<int>((float)r * multiplier), 0, 255);
            g = std::clamp(static_cast<int>((float)g * multiplier), 0, 255);
            b = std::clamp(static_cast<int>((float)b * multiplier), 0, 255);
        }

        char outBuf[256]{};
        if (currentArgsStr.find(".r") != std::string::npos) {
            sprintf_s(outBuf, "\n\t\t\t\t.r=%d, \n\t\t\t\t.g=%d,  \n\t\t\t\t.b=%d\n\t\t\t\t", r, g, b);
        }
        else {
            sprintf_s(outBuf, "%d, %d, %d", r, g, b);
        }
        return outBuf;
    }

    inline std::string pos2Drag(const std::string& currentArgsStr, const ::LivePT::DragMathInput& input) {
        float x = 0.0f, y = 0.0f;
        bool parsed = false;

        // 1. НАДЕЖНЫЙ МНОГОСТРОЧНЫЙ ПАРСИНГ (.x = ... .y = ...)
        size_t xPos = currentArgsStr.find(".x");
        size_t yPos = currentArgsStr.find(".y");

        if (xPos != std::string::npos && yPos != std::string::npos) {
            size_t xValStart = currentArgsStr.find('=', xPos);
            size_t yValStart = currentArgsStr.find('=', yPos);
            if (xValStart != std::string::npos && yValStart != std::string::npos) {
                x = std::stof(currentArgsStr.substr(xValStart + 1));
                y = std::stof(currentArgsStr.substr(yValStart + 1));
                parsed = true;
            }
        }

        // 2. ОДНОСТРОЧНЫЙ ФОЛБЭК (Числа через запятую: 268.01f, 14.10f)
        if (!parsed) {
            std::string cleanStr = currentArgsStr;
            cleanStr.erase(std::remove(cleanStr.begin(), cleanStr.end(), 'f'), cleanStr.end());
            cleanStr.erase(std::remove(cleanStr.begin(), cleanStr.end(), 'F'), cleanStr.end());
            if (sscanf_s(cleanStr.c_str(), "%f, %f", &x, &y) != 2) {
                return currentArgsStr; // Если и тут крах — выходим
            }
        }

        // 3. Вычисляем дельту движения мыши
        float speedScale = input.ctrl ? 1.0f : (input.shift ? 0.01f : 0.1f);
        x += static_cast<float>(input.mouseFrameDeltaX) * speedScale;
        y -= static_cast<float>(input.mouseFrameDeltaY) * speedScale;

        // 4. Сохраняем исходное форматирование табов
        std::string prefixTabs = "";
        size_t firstNewLine = currentArgsStr.find('\n');
        if (firstNewLine != std::string::npos) {
            size_t idx = firstNewLine + 1;
            while (idx < currentArgsStr.length() && (currentArgsStr[idx] == '\t' || currentArgsStr[idx] == ' ')) {
                prefixTabs += currentArgsStr[idx];
                idx++;
            }
        }
        if (prefixTabs.empty()) prefixTabs = "\t\t\t\t";

        char outBuf[256]{};
        if (currentArgsStr.find(".x") != std::string::npos) {
            sprintf_s(outBuf, "\n%s.x = %.2ff,\n%s.y = %.2ff\n%s",
                prefixTabs.c_str(), x, prefixTabs.c_str(), y, prefixTabs.c_str());
        }
        else {
            sprintf_s(outBuf, "%.2ff, %.2ff", x, y);
        }
        return outBuf;
    }

    inline std::string size2Drag(const std::string& currentArgsStr, const ::LivePT::DragMathInput& input) {
        float w = 0.0f, h = 0.0f;
        bool parsed = false;

        // 1. НАДЕЖНЫЙ МНОГОСТРОЧНЫЙ ПАРСИНГ (.w = ... .h = ...)
        size_t wPos = currentArgsStr.find(".w");
        size_t hPos = currentArgsStr.find(".h");

        if (wPos != std::string::npos && hPos != std::string::npos) {
            size_t wValStart = currentArgsStr.find('=', wPos);
            size_t hValStart = currentArgsStr.find('=', hPos);
            if (wValStart != std::string::npos && hValStart != std::string::npos) {
                w = std::stof(currentArgsStr.substr(wValStart + 1));
                h = std::stof(currentArgsStr.substr(hValStart + 1));
                parsed = true;
            }
        }

        // 2. ОДНОСТРОЧНЫЙ ФОЛБЭК
        if (!parsed) {
            std::string cleanStr = currentArgsStr;
            cleanStr.erase(std::remove(cleanStr.begin(), cleanStr.end(), 'f'), cleanStr.end());
            cleanStr.erase(std::remove(cleanStr.begin(), cleanStr.end(), 'F'), cleanStr.end());
            if (sscanf_s(cleanStr.c_str(), "%f, %f", &w, &h) != 2) {
                return currentArgsStr;
            }
        }

        // 3. Масштабирование
        float multiplier = 1.0f + (input.mouseFrameDeltaY * 0.005f);
        if (multiplier < 0.0f) multiplier = 0.0f;

        w *= multiplier;
        h *= multiplier;

        if (w == 0.0f || h == 0.0f) {
            float speedScale = input.ctrl ? 0.1f : (input.shift ? 0.001f : 0.01f);
            float added = static_cast<float>(input.mouseFrameDeltaY) * speedScale;
            if (w == 0.0f) w += added;
            if (h == 0.0f) h += added;
        }

        // 4. Сохраняем исходное форматирование табов
        std::string prefixTabs = "";
        size_t firstNewLine = currentArgsStr.find('\n');
        if (firstNewLine != std::string::npos) {
            size_t idx = firstNewLine + 1;
            while (idx < currentArgsStr.length() && (currentArgsStr[idx] == '\t' || currentArgsStr[idx] == ' ')) {
                prefixTabs += currentArgsStr[idx];
                idx++;
            }
        }
        if (prefixTabs.empty()) prefixTabs = "\t\t\t\t";

        char outBuf[256]{};
        if (currentArgsStr.find(".w") != std::string::npos) {
            sprintf_s(outBuf, "\n%s.w = %.2ff,\n%s.h = %.2ff\n%s",
                prefixTabs.c_str(), w, prefixTabs.c_str(), h, prefixTabs.c_str());
        }
        else {
            sprintf_s(outBuf, "%.2ff, %.2ff", w, h);
        }
        return outBuf;
    }

}
