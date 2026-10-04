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

    // === FIX: Universal string-based color3Drag ===
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

    // === FIX: Universal string-based pos2Drag ===
    inline std::string pos2Drag(const std::string& currentArgsStr, const ::LivePT::DragMathInput& input) {
        float x = 0.0f, y = 0.0f;
        if (sscanf_s(currentArgsStr.c_str(), "%*[^0-9.-]%f%*[^0-9.-]%f", &x, &y) != 2) {
            if (sscanf_s(currentArgsStr.c_str(), "%f, %f", &x, &y) != 2) {
                return currentArgsStr;
            }
        }

        float speedScale = input.ctrl ? 1.0f : (input.shift ? 0.01f : 0.1f);
        x += static_cast<float>(input.mouseFrameDeltaX) * speedScale;
        y -= static_cast<float>(input.mouseFrameDeltaY) * speedScale;

        char outBuf[256]{};
        if (currentArgsStr.find(".x") != std::string::npos) {
            sprintf_s(outBuf, "\n\t\t\t\t.x = %.2ff,\n\t\t\t\t.y = %.2ff\n\t\t\t\t", x, y);
        }
        else {
            sprintf_s(outBuf, "%.2ff, %.2ff", x, y);
        }
        return outBuf;
    }

    // === FIX: Universal string-based size2Drag ===
    inline std::string size2Drag(const std::string& currentArgsStr, const ::LivePT::DragMathInput& input) {
        float w = 0.0f, h = 0.0f;
        if (sscanf_s(currentArgsStr.c_str(), "%*[^0-9.-]%f%*[^0-9.-]%f", &w, &h) != 2) {
            if (sscanf_s(currentArgsStr.c_str(), "%f, %f", &w, &h) != 2) {
                return currentArgsStr;
            }
        }

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

        char outBuf[256]{};
        if (currentArgsStr.find(".w") != std::string::npos) {
            sprintf_s(outBuf, "\n\t\t\t\t.w = %.2ff,\n\t\t\t\t.h = %.2ff\n\t\t\t\t", w, h);
        }
        else {
            sprintf_s(outBuf, "%.2ff, %.2ff", w, h);
        }
        return outBuf;
    }

} // namespace DrugLogic
