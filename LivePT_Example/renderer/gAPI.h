#if LivePT_EditMode

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
#endif

using namespace dx11;

namespace gApi {

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
		Rasterizer::Cull(cullmode::back);

		Draw::SetRenderTarget(eval(TexList::rt::mainRT), 0);

		dx11::ConstBuf::frame.time = XMFLOAT4(timer::GetCounter(), 0, 0, 0);
		dx11::ConstBuf::frame.aspect = XMFLOAT4(0, 0, 0, 0);
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
		float4 color;
	};

	LPT_LIVE_FUNCTION(void, ShowSprite, spriteDesc, in)
	{
		Shaders::vs::sprite::params = {
			.position = float4{ in.pos.x, in.pos.y, 0.f, 0.f},
			.size = float4{ in.size.w,in.size.h, 0.f,0.f},
			.rotation = float4{in.rotation,0.f,0.f,0.f}
		};
		ConstBuf::Update(dx11::Shaders::VS[(int)Shaders::vertex::sprite].params, Shaders::vs::sprite::params);
		Shaders::SetVSParamCB(Shaders::vertex::sprite);
		Shaders::vShader(Shaders::vertex::sprite);

		Shaders::ps::sprite::params = { in.color };
		ConstBuf::Update(dx11::Shaders::PS[(int)Shaders::pixel::sprite].params, Shaders::ps::sprite::params);
		Shaders::SetPSParamCB(Shaders::pixel::sprite);
		//Textures::TextureToShader(in.texture, 0, targetshader::pixel);
		Textures::TextureToShader(0, 0, targetshader::pixel);
		Sampler::Sampler(targetshader::pixel, 0, filter::linear, addr::wrap, addr::wrap);
		Shaders::pShader(Shaders::pixel::sprite);

		Draw::NullDrawer(1, 1);
	}
}