namespace vs {

    namespace sprite {

        struct
        {
            float4 position;
            float4 size;
            float4 rotation;
        } params;

    }

    namespace spriteTex {

        struct
        {
            float4 position;
        } params;

    }

}

namespace ps {

    namespace sprite {

        struct
        {
            float4 base_color;
        } params;

    }

    namespace spriteTex {

        struct
        {
            float4 base_color;
        } params;

    }

}

