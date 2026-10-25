struct int4 { int x; int y; int z; int w; };
struct float4 { float x; float y; float z; float w; };
struct color4 { float r; float g; float b; float a; };

enum class blendmode { off, on, alpha };
enum class blendop { add, sub, revsub, min, max };
enum class depthmode { off, on, readonly, writeonly };
enum class filter { linear, point, minPoint_magLinear };
enum class addr { clamp, wrap };
enum class cullmode { off, front, back, wireframe };
enum class topology { triList, lineList, lineStrip, triStrip };
enum class targetshader { vertex, pixel, both };
enum class tType { flat, cube };
enum class tFormat { u8, s8, s16, s32 };