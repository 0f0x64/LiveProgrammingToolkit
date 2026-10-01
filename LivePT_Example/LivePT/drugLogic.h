
namespace DrugLogic {

    template <typename T>
    inline void size2Drag(std::vector<float>& values, const LivePT::DragMathInput& input) {
        for (float& val : values) {
            if (val == -999999.0f) continue;

            float multiplier = 1.0f + (input.mouseFrameDeltaY * 0.005f);
            if (multiplier < 0.0f) multiplier = 0.0f;

            val *= multiplier;
            if (val == 0.0f) {
                float speedScale = input.ctrl ? 0.1f : (input.shift ? 0.001f : 0.01f);
                val += static_cast<float>(input.mouseFrameDeltaY) * speedScale;
            }
        }
    }

    template <typename T>
    inline void pos2Drag(std::vector<float>& values, const LivePT::DragMathInput& input) {
        if (values.size() < 2) return;

        float speedScale = input.ctrl ? 1.0f : (input.shift ? 0.01f : 0.1f);

        if (values[0] != -999999.0f) values[0] += static_cast<float>(input.mouseFrameDeltaX) * speedScale;
        if (values[1] != -999999.0f) values[1] -= static_cast<float>(input.mouseFrameDeltaY) * speedScale;
    }

    // Вспомогательные функции для перевода цвета (инкапсулированы внутри DrugLogic)
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

    template <typename T>
    inline void color3Drag(std::vector<float>& values, const ::LivePT::DragMathInput& input) {
        if (values.size() < 3) return;
        if (values[0] == -999999.0f || values[1] == -999999.0f || values[2] == -999999.0f) return;

        if (input.shift) {
            float h = 0, s = 0, v = 0;
            LPT_RGBtoHSV(values[0], values[1], values[2], h, s, v);

            // 1. Горизонтальное движение — чистый Hue
            if (input.mouseFrameDeltaX != 0) {
                h += static_cast<float>(input.mouseFrameDeltaX) * 0.5f;
                h = std::fmod(h, 360.0f);
                if (h < 0.0f) h += 360.0f;
            }

            // 2. Адаптивное вертикальное движение — умная сочность (Насыщенность + Яркость)
            if (input.mouseFrameDeltaY != 0) {
                float deltaS = static_cast<float>(input.mouseFrameDeltaY) * 0.005f;
                s += deltaS;
                s = std::clamp(s, 0.0f, 1.0f);

                // Адаптивная компенсация: если мы делаем цвет сочнее (deltaS > 0),
                // но общая яркость цвета слишком низкая (v < 0.6), мы аккуратно 
                // «подтягиваем» яркость вверх, чтобы цвет не оставался грязным.
                if (deltaS > 0.0f && v < 0.6f) {
                    v += deltaS * 0.5f; // Коэффициент 0.5f подмешивает яркость мягко
                    v = std::clamp(v, 0.0f, 1.0f);
                }
                // Наоборот: если мы полностью вымываем цвет в серый (s -> 0),
                // имеет смысл слегка приглушить экстремальную яркость, чтобы серый не был ядовито-белым.
                if (deltaS < 0.0f && v > 0.8f) {
                    v += deltaS * 0.3f;
                    v = std::clamp(v, 0.0f, 1.0f);
                }
            }

            float r = 0, g = 0, b = 0;
            LPT_HSVtoRGB(h, s, v, r, g, b);

            values[0] = std::clamp(r, 0.0f, 255.0f);
            values[1] = std::clamp(g, 0.0f, 255.0f);
            values[2] = std::clamp(b, 0.0f, 255.0f);

            for (int i = 0; i < 3; ++i) {
                ::LivePT::g_dragState.originalStructValues[i] = std::clamp(values[i], 0.f, 255.f);
            }
        }
        else {
            // === СТАНДАРТНЫЙ РЕЖИМ (Твой оригинальный алгоритм яркости) ===
            int id = 0;
            for (float& val : values) {
                if (val == -999999.0f) { id++; continue; }

                float multiplier = 1.0f + (input.mouseFrameDeltaY * 0.005f);
                if (multiplier < 0.0f) multiplier = 0.0f;

                val *= multiplier;
                if (val == 0.0f) {
                    val += static_cast<float>(input.mouseFrameDeltaY) * 0.01f;
                }

                val = std::clamp(val, 0.0f, 255.0f);
                ::LivePT::g_dragState.originalStructValues[id] = std::clamp(::LivePT::g_dragState.originalStructValues[id], 0.f, 255.f);

                id++;
            }
        }
    }
}