#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "winmm.lib")

typedef unsigned long uint32;
typedef long int32;

static inline int32 _log2(float x)
{
	uint32 ix = (uint32&)x;
	uint32 exp = (ix >> 23) & 0xFF;
	int32 log2 = int32(exp) - 127;

	return log2;
}

#define FRAMES_PER_SECOND 60
#define DirectXDebugMode true

struct float4 {
	float x; float y; float z; float w;
};

struct color4 {
	float r; float g; float b; float a;
};


enum class blendmode { off, on, alpha };
enum class blendop { add, sub, revsub, min, max };
enum class depthmode {off, on, readonly, writeonly};
enum class filter {linear, point, minPoint_magLinear};
enum class addr { clamp, wrap};
enum class cullmode { off, front, back, wireframe};
enum class topology { triList, lineList, lineStrip, triStrip};
enum class targetshader { vertex, pixel, both};


#include <d3d11.h>
#include <d3dcompiler.h>
#include "DirectXMath.h"
#include <DirectXPackedVector.h>

using namespace DirectX;

namespace dx11
{

	#if DebugMode | EditMode

		inline void Log(const std::string& text, const std::string& prefix = "[dx11] ", bool nl = true) {
			#if EditMode
				OutputDebugStringA((prefix + text).c_str());
				if (nl) OutputDebugStringA("\n");
			#else	
				MessageBox(hWnd, message, prefix, MB_OK);
			#endif	
		}

		#define LogIfError(text)  if (FAILED(hr)) { Log(text); return; } 
		#define LogBlobIfError if (FAILED(hr)) { char title[10];_itoa(n,title,10); Log((char*)pErrorBlob->GetBufferPointer(), title); }

	#else
		
		#define LogIfError(text)  
		#define LogBlobIfError

	#endif

	ID3D11Device* device = NULL;
	ID3D11DeviceContext* context = NULL;
	IDXGISwapChain* swapChain = NULL;
	
	struct {
		int width;
		int height;
		float aspect;
		float iaspect;
	} screen;

	#include "constBuf.h"
//	#include "camera.h"
	#include "blend.h"
	#include "rasterizer.h"
/*	#include "depth.h"
	#include "sampler.h"*/
	#include "textures.h"
	#include "device.h"
//	#include "shaders.h"
	#include "draw.h"
//	#include "IA.h"*/
	//#include "audio.h"

	void Init(HWND hwnd)
	{
//		Audio::Init();
		Device::Init(hwnd);
/*		Rasterizer::Init();
		Depth::Init();
		Blend::Init();
		ConstBuf::Init();
		Sampler::Init();
		Shaders::Init();
		Textures::Init();*/
	}

#if EditMode

/*	void Resize()
	{
		aspect = float(height) / float(width);
		iaspect = float(width) / float(height);

		ID3D11RenderTargetView* nullViews[] = { nullptr };
		context->OMSetRenderTargets(1, nullViews, 0);

		Textures::Texture[0].RenderTargetView[0][0]->Release();
		Textures::Texture[0].DepthStencilView[0]->Release();

		swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&Textures::Texture[0].pTexture);
		device->CreateRenderTargetView(Textures::Texture[0].pTexture, NULL, &Textures::Texture[0].RenderTargetView[0][0]);
		Textures::Texture[0].pTexture->Release();
		Textures::Texture[0].pDepth->Release();
		Textures::Create(0, Textures::tType::flat, Textures::tFormat::u8, XMFLOAT2((float)width, (float)height), false, true);
		
		Camera::viewCam.pInit();
	}
	*/
#endif

}
