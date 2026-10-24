using namespace dx11::Textures;

#ifdef reg
    #undef Tex
    #undef List
    #define Tex(name, type, format, width,height, mipMaps, depth) Create(count++, type, format, width, height, mipMaps, depth);
    #define List(name) 
#else
    #define Tex(name, type, format, width,height, mipMaps, depth) name = __COUNTER__,
    #define List(name) enum class name
#endif

namespace Textures {

#ifdef reg
    static int count = 0;
    void CreateAllTextures() {
#endif

    List(rt) {
        Tex(mainRT, tType::flat, tFormat::u8, dx11::screen.width, dx11::screen.height, false, true)
        Tex(rt2, tType::flat, tFormat::u8, 256, 256, false, false)
        Tex(rt3, tType::flat, tFormat::u8, 256, 256, false, false)
    };

    List(ttt) {
        Tex(t1, tType::flat, tFormat::u8, dx11::screen.width, dx11::screen.height, false, true)
        Tex(t2, tType::flat, tFormat::u8, 256, 256, false, false)
        Tex(t3, tType::flat, tFormat::u8, 256, 256, false, false)
    };

#ifdef reg
    }
#endif


}

#ifndef reg
    #define reg
    #include "textures.h"
    #undef reg
#endif