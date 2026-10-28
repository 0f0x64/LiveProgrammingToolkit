using namespace dx11::Textures;

#ifdef reg
    #undef Tex
    #undef List
    #define Tex(name, type, format, width,height, mipMaps, depth, ...) Create(count++, type, format, width, height, mipMaps, depth __VA_OPT__(,) __VA_ARGS__);
    #define List(name) 
#else
    #define Tex(name, type, format, width,height, mipMaps, depth, ...) name = __COUNTER__,
    #define List(name) enum class name
#endif

namespace TexList {

#ifdef reg
    static int count = 0;
    void CreateAllTextures() {
#endif

    using namespace dx11;
    #include "../texturesList.h"

#ifdef reg
}
#endif

}

#ifndef reg
#define reg
#include "texturesResBP.h"
#undef reg
#endif

#undef Tex
#undef List