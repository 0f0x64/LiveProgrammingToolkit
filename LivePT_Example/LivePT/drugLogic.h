
namespace LivePT {

    template <typename T>
    inline void size2DragMath(std::vector<float>& values, const LivePT::DragMathInput& input) {
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
    inline void pos2DragMath(std::vector<float>& values, const LivePT::DragMathInput& input) {
        if (values.size() < 2) return;

        float speedScale = input.ctrl ? 1.0f : (input.shift ? 0.01f : 0.1f);

        if (values[0] != -999999.0f) values[0] += static_cast<float>(input.mouseFrameDeltaX) * speedScale;
        if (values[1] != -999999.0f) values[1] -= static_cast<float>(input.mouseFrameDeltaY) * speedScale;
    }

    template <typename T>
    inline void color3DragMath(std::vector<float>& values, const LivePT::DragMathInput& input) {
        if (values.size() < 3) return;

        for (int id = 0; float& val : values) {
            if (val == -999999.0f) continue;

            float multiplier = 1.0f + (input.mouseFrameDeltaY * 0.005f);
            if (multiplier < 0.0f) multiplier = 0.0f;

            val *= multiplier;
            if (val == 0.0f) {
                float speedScale = input.ctrl ? 0.1f : (input.shift ? 0.001f : 0.01f);
                val += static_cast<float>(input.mouseFrameDeltaY) * speedScale;
            }

            val = std::clamp(val, 0.0f, 255.0f);
            g_dragState.originalStructValues[id] = std::clamp(g_dragState.originalStructValues[id], 0.f, 255.f);


            id++;
        }
    }
}