#include <windows.h>
#include <vector>
#include <source_location>
#include <string>
#include <cmath>

#define LPT_SHADER_COMPILE_FUNC recompileShader()
void recompileShader();
// ---------------- LivePT integration section ----------------
#define LivePT_EditMode true // true for activate
#include "LivePT/LivePT.h" // incude it for using lib

// ---------------------- d3d renderer ------------------------
#define EditMode true
#define DebugMode true
#define ExternalResourcesMode true
#include "dx11/dx.h"
// ------------------------------------------------------------

#define TIMER_ID 1

//simple class for demo purposes
enum class ptype { circle, box, roundbox };

struct color3 {
    unsigned char r;
    unsigned char g;
    unsigned char b;
};
#include "Widgets\color3.h"
LPT_REGISTER_TYPE("color3", Widgets::color3Callback);
LPT_REGISTER_DRAG("color3", DrugLogic::color3Drag);

struct pos2 {
    float x;
    float y;
};
#include "Widgets\pos2.h"
LPT_REGISTER_TYPE("pos2", Widgets::pos2Callback);
LPT_REGISTER_DRAG("pos2", DrugLogic::pos2Drag);

struct size2 {
    float w;
    float h;
};
LPT_REGISTER_TYPE("size2", Widgets::pos2Callback);
LPT_REGISTER_DRAG("size2", DrugLogic::size2Drag);

typedef float angle;
#include "Widgets\angle.h"
LPT_REGISTER_TYPE("angle", Widgets::angleCallback);

#include <filesystem>
namespace fs = ::std::filesystem;

void getPathParts(const ::std::string& fullPath, ::std::string& nameOnly, ::std::string& parentFolder) {
    fs::path p(fullPath);

    // 1. Имя файла без расширения (stem)
    nameOnly = p.stem().string();

    // 2. Название папки на один уровень выше
    // p.parent_path() возвращает полный путь к папке, 
    // .filename() извлекает только последнее имя в этом пути
    parentFolder = p.parent_path().filename().string();
}

void recompileShader()
{
    ::std::string name, folder;
    
    getPathParts(LivePT::g_currentActiveFile, name, folder);

    ::std::string rPath = "/" + folder + "/" + name + dx11::Shaders::shaderExtension;

    // detect vertex/pixel shader and slot
    if (folder[0] == 'v')
    {
        int i = 0;
        while (i < dx11::Shaders::vsCount)
        {
            if (!strcmp(dx11::Shaders::vsList[i], name.c_str()))
            {
                dx11::Shaders::CreateVS(i, rPath.c_str(), true);
                break;
            }
            i++;
        }

    }

    if (folder[0] == 'p')
    {
        int i = 0;
        while (i < dx11::Shaders::psCount)
        {
            if (!strcmp(dx11::Shaders::psList[i], name.c_str()))
            {
                dx11::Shaders::CreatePS(i, rPath.c_str(), true);
                break;
            }
            i++;
        }

    }
}



struct {
    HWND hWnd;
    HDC memDC;
    int w;
    int h;
} gc;

struct Primitive {
    pos2 pos;
    size2 size;
    angle Angle;
    color3 color;
    ptype type;
    bool show;
};

int drawCounter = 0;
int curSel = -1;

bool IsPrimitiveSelected(int posX, int posY, HWND hwnd) {
    POINT mousePos;
    if (!GetCursorPos(&mousePos)) {
        return false;
    }

    if (!ScreenToClient(hwnd, &mousePos)) {
        return false;
    }

    int dx = mousePos.x - posX;
    int dy = mousePos.y - posY;

    if ((dx * dx + dy * dy) > 100) {
        return false;
    }

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {

        curSel = drawCounter;
        return true;
    }

    return false;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

#include "renderer\gAPI.h"
#include "renderer\timer.h"


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

    const char CLASS_NAME[] = "WindowClass";

    dx11::screen.width = GetSystemMetrics(SM_CXSCREEN);
    dx11::screen.height = GetSystemMetrics(SM_CYSCREEN);

    auto hUser32 = GetModuleHandleA("user32.dll");
    typedef BOOL(WINAPI* PfnSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST,
        CLASS_NAME,
        "LivePT Test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        600, 400,
        NULL, NULL,
        hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    dx11::Init(hwnd);
   

    ShowWindow(hwnd, nCmdShow);

    MSG msg = { 0 };

    timer::StartCounter();

    while (msg.message != WM_QUIT)
    {

#if EditMode

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

#else

        // Проверяем сообщения Windows
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        //PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE);
        //if (GetAsyncKeyState(VK_ESCAPE) || timer::frameBeginTime / 1000. > DEMO_DURATION) { break; }

#endif

        //UpdateFrame(timer::GetCounter());
        using namespace dx11;

        InputAssembler::IA(topology::triList);
        Blend::Set(blendmode::alpha, blendop::add);
        Depth::Depth(depthmode::off);
        Rasterizer::Cull(cullmode::back);

        Draw::SetRenderTarget(eval(TexList::rt::mainRT), 0);
        Draw::Clear(eval(color4{ 0.0f,0.0f,0.0f,1.0f }));
        
        
        dx11::ConstBuf::frame.time = XMFLOAT4(timer::GetCounter(),0,0,0);
        dx11::ConstBuf::frame.aspect = XMFLOAT4(0,0,0,0);
        dx11::ConstBuf::Update(ConstBuf::cBuffer::frame);
        dx11::ConstBuf::Set(ConstBuf::cBuffer::frame, targetshader::both);

        gApi::ShowSprite({
                .pos = float4(-0.36f,0.0f,0.0f,0.0f),
                .size = float4(1.0f,1.0f,1.0f,1.0f),
                .color = float4(1.0f,1.0f,1.0f,1.0f),
                .texture = TexList::sprites::spr1
            });

        gApi::ShowSprite({
                .pos = float4(0.38f,0.00f,0.0f,0.0f),
                .size = float4(1.0f,1.0f,1.0f,1.0f),
                .color = float4(1.0f,1.0f,1.0f,1.0f),
                .texture = TexList::sprites::spr2
            });

        Draw::Present();

        LivePT::ProcessEdit();// don't forget this call

    }

    return 0;
}
