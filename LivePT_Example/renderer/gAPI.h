#if LivePT_EditMode

#include <filesystem>
namespace fs = ::std::filesystem;

void getPathParts(const ::std::string& fullPath, ::std::string& nameOnly, ::std::string& parentFolder) {
	fs::path p(fullPath);
	nameOnly = p.stem().string();
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
#endif

using namespace dx11;

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

namespace gApi {

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

	void Start()
	{
		InputAssembler::IA(topology::triList);
		Blend::Set(blendmode::alpha, blendop::add);
		Depth::Depth(depthmode::off);
		Rasterizer::Cull(cullmode::off);

		Draw::SetRenderTarget(TexList::rt::mainRT, 0);

		dx11::ConstBuf::frame.time = XMFLOAT4((float)timer::GetCounter(), 0, 0, 0);
		dx11::ConstBuf::frame.aspect = XMFLOAT4(dx11::screen.width/ (float)dx11::screen.height, dx11::screen.height/ (float)dx11::screen.width, 0, 0);
		dx11::ConstBuf::Update(ConstBuf::cBuffer::frame);
		dx11::ConstBuf::Set(ConstBuf::cBuffer::frame, targetshader::both);
	}

	void End()
	{
		Draw::Present();
	}

	LPT_LIVE_FUNCTION(void, Clear, color3, in)
	{
		Draw::Clear((color4{ in.r/255.f,in.g / 255.f,in.b / 255.f,1.0f }));
	}

	struct spriteDesc {
		pos2 pos;
		size2 size;
		angle rotation;
		pos2 rotCentre;
		float4 color;
		int texture;
	};

	LPT_LIVE_FUNCTION(void, ShowSprite, spriteDesc, in)
	{
		Shaders::vs::sprite::params = {
			.position = float4{ in.pos.x, in.pos.y, 0.f, 0.f},
			.size = float4{ in.size.w,in.size.h, 0.f,0.f},
			.rotation = float4{in.rotation*(float)M_PI/180.f,0.f,0.f,0.f},
			.rotationCentre = float4{in.rotCentre.x,in.rotCentre.y,0.f,0.f}
		};
		ConstBuf::Update(dx11::Shaders::VS[(int)Shaders::vertex::sprite].params, Shaders::vs::sprite::params);
		Shaders::SetVSParamCB(Shaders::vertex::sprite);
		Shaders::vShader(Shaders::vertex::sprite);

		Shaders::ps::sprite::params = { in.color };
		ConstBuf::Update(dx11::Shaders::PS[(int)Shaders::pixel::sprite].params, Shaders::ps::sprite::params);
		Shaders::SetPSParamCB(Shaders::pixel::sprite);
		Textures::TextureToShader(in.texture, 0, targetshader::pixel);
		Sampler::Sampler(targetshader::pixel, 0, filter::linear, addr::wrap, addr::wrap);
		Shaders::pShader(Shaders::pixel::sprite);

		Draw::NullDrawer(1, 1);
	}

	struct cam {
		float3 eye;
		float3 at;
		float3 up;
		float angle;
	};

	LPT_LIVE_FUNCTION(void, SetCamera, cam, in)
	{
		Camera::Set(in.eye, in.at, in.up, in.angle);
	}

	struct cam2d {
		pos2 eye;
		float angle;
	};

	LPT_LIVE_FUNCTION(void, SetCamera2d, cam2d, in)
	{
		float dist = -100;
		XMVECTOR Eye = XMVECTOR{ in.eye.x,in.eye.y,dist, 0.0f }/100.;
		XMVECTOR At = XMVECTOR{ in.eye.x, in.eye.y, 0, 0.0f } / 100.;
		XMVECTOR Up = XMVECTOR{ 0, 1, 0, 0.0f };

		ConstBuf::camera[0] = {
			.world = XMMatrixIdentity(),
			.view = XMMatrixTranspose(XMMatrixLookAtLH(Eye, At, Up)),
			.proj = XMMatrixTranspose(XMMatrixPerspectiveFovLH(Camera::DegreesToRadians(in.angle), dx11::screen.iaspect, 0.01f, 100.0f))
		};

		ConstBuf::Update(ConstBuf::cBuffer::camera);
		ConstBuf::Set(ConstBuf::cBuffer::camera, targetshader::both);
	}
}