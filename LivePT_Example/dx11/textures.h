#if !NoExternalResources
	#define STB_IMAGE_IMPLEMENTATION
	#include "thirdParty/stb_image.h"
#endif

namespace Textures
{

	#define max_tex 255
	#define mainRTIndex 0

	DXGI_FORMAT dxTFormat[] = { 
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_R8G8B8A8_SNORM,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R32G32B32A32_FLOAT 
	};


	D3D11_SHADER_RESOURCE_VIEW_DESC svDesc;
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;

	ID3D11RenderTargetView* mrtView[8];

	struct {

		ID3D11Texture2D* pTexture;
		D3D11_TEXTURE2D_DESC tdesc;
		ID3D11ShaderResourceView* TextureResView;
		ID3D11RenderTargetView* RenderTargetView[16][6];//16 for possible mips? // 6 for cubemap

		ID3D11Texture2D* pDepth;
		ID3D11ShaderResourceView* DepthResView;
		ID3D11DepthStencilView* DepthStencilView[16];

		tType type;
		tFormat format;
		XMFLOAT2 size;
		bool mipMaps;
		bool depth;
#if !NoExternalResources
		unsigned char* imgData = nullptr;
		char imgName[MAX_PATH];
#endif

	} Texture[max_tex];

	byte currentRT = 0;
	int texturesCount = 0;

	void FillTDesc(auto &cTex)
	{
		cTex.tdesc = {
			.Width = (UINT)cTex.size.x,
			.Height = (UINT)cTex.size.y,
			.MipLevels = cTex.mipMaps ? (UINT)0 : 1,
			.ArraySize = 1,
			.Format = dxTFormat[(int)cTex.format],
			.SampleDesc = {
				.Count = 1,
				.Quality = 0,
				},
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS
		};
	}

	void CreateTex(int i)
	{

		auto cTex = Texture[i];

		FillTDesc(cTex);
		
		if (cTex.type == tType::cube)
		{
			cTex.tdesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | cTex.tdesc.MiscFlags;
			cTex.tdesc.ArraySize = 6;
		}

		HRESULT hr = device->CreateTexture2D(&cTex.tdesc, nullptr, &Texture[i].pTexture);

		LogIfError("CreateTexture2D error");
	}

	void CreateShaderRes(int i)
	{
		svDesc.Format = Texture[i].tdesc.Format;
		svDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

		if (Texture[i].type == tType::cube)
		{
			svDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			svDesc.TextureCube.MostDetailedMip = 0;
			svDesc.TextureCube.MipLevels = -1;

		}
		else
		{
			svDesc.Texture2D.MipLevels = -1;
			svDesc.Texture2D.MostDetailedMip = 0;
		}

		HRESULT hr = device->CreateShaderResourceView(Texture[i].pTexture, &svDesc, &Texture[i].TextureResView);
		LogIfError("CreateShaderResourceView error\n");
	}


	void CreateRTView(int i)
	{
		renderTargetViewDesc.Format = Texture[i].tdesc.Format;
		renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		renderTargetViewDesc.Texture2D.MipSlice = 0;

		if (Texture[i].type == tType::cube)
		{
			renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			renderTargetViewDesc.Texture2DArray.ArraySize = 1;
			renderTargetViewDesc.Texture2DArray.MipSlice = 0;

			for (int j = 0; j < 6; j++)
			{
				renderTargetViewDesc.Texture2DArray.FirstArraySlice = j;
				HRESULT hr = device->CreateRenderTargetView(Texture[i].pTexture, &renderTargetViewDesc, &Texture[i].RenderTargetView[0][j]);
				LogIfError("CreateRenderTargetView error\n");
			}
		}
		else
		{
			for (unsigned int m = 0; m < Texture[i].tdesc.MipLevels; m++)
			{
				renderTargetViewDesc.Texture2D.MipSlice = m;
				HRESULT hr = device->CreateRenderTargetView(Texture[i].pTexture, &renderTargetViewDesc, &Texture[i].RenderTargetView[m][0]);
				LogIfError("CreateRenderTargetView error\n");
			}
		}
	}

	void CreateDepth(int i)
	{
		auto cTex = Texture[i];

		FillTDesc(cTex);

		cTex.tdesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		cTex.tdesc.Format = DXGI_FORMAT_R32_TYPELESS;
		cTex.tdesc.MiscFlags = 0;

		HRESULT hr = device->CreateTexture2D(&cTex.tdesc, NULL, &Texture[i].pDepth);
		
		LogIfError ("CreateDepthStencilView error\n");

		descDSV.Format = DXGI_FORMAT_D32_FLOAT;
		descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		descDSV.Flags = 0;

		for (unsigned int m = 0; m < max(1, cTex.tdesc.MipLevels); m++)
		{
			descDSV.Texture2D.MipSlice = m;
			HRESULT hr = device->CreateDepthStencilView(Texture[i].pDepth, &descDSV, &Texture[i].DepthStencilView[m]);
			LogIfError("CreateDepthStencilView error\n");
		}
	}

	void CreateShaderResDepth(int i)
	{
		svDesc.Format = DXGI_FORMAT_R32_FLOAT;
		svDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		svDesc.Texture2D.MostDetailedMip = 0;
		svDesc.Texture2D.MipLevels = 1;

		HRESULT hr = device->CreateShaderResourceView(Texture[i].pDepth, &svDesc, &Texture[i].DepthResView);
		LogIfError("CreateShaderResourceView for Depth error\n");
	}

#if !NoExternalResources
	int getTextureIDfromName(const char* fileName)
	{
		for (int i = 0; i < texturesCount; i++)
		{
			if (strcmp(Texture[i].imgName, fileName) == 0) return i;
		}
		return -1;
	}

	void LoadTexture(int i, const char* fileName)
	{
		auto cTex = &Texture[i];

		if (fileName && cTex->type != tType::cube) {

			strcpy_s(cTex->imgName, MAX_PATH, fileName);
			char path[MAX_PATH];
			strcpy_s(path, MAX_PATH, "textures/");
			strcat_s(path, MAX_PATH, fileName);

			int width = 0, height = 0, channels = 0;

			stbi_set_flip_vertically_on_load(true);
			cTex->imgData = stbi_load(path, &width, &height, &channels, 4);

			cTex->size.x = width;
			cTex->size.y = height;

			//Log((std::string(fileName) + " loaded").c_str());
		}
		else {
			Log("Failed to load PNG file from disk");
		}
	}

	void TextureToGPU(int i) {

		auto cTex = &Texture[i];
		D3D11_SUBRESOURCE_DATA initData{};
		D3D11_SUBRESOURCE_DATA* pInitData = nullptr;

		if (cTex->imgData) {

			cTex->tdesc.Width = cTex->size.x;
			cTex->tdesc.Height = cTex->size.y;
			cTex->tdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

			cTex->tdesc.MipLevels = cTex->mipMaps ? (UINT)0 : 1;

			initData.pSysMem = cTex->imgData;
			initData.SysMemPitch = cTex->tdesc.Width * 4;
			initData.SysMemSlicePitch = 0;
			pInitData = &initData;

			context->UpdateSubresource(Texture[i].pTexture, 0, nullptr, cTex->imgData, cTex->tdesc.Width * 4, 0);

			if (cTex->mipMaps) {
				context->GenerateMips(Texture[i].TextureResView);
			}
		}

	}

	void LoadTexture(int i, D3D11_TEXTURE2D_DESC& tdesc, const char* fileName)
	{
		auto cTex = &Texture[i];
		D3D11_SUBRESOURCE_DATA initData{};
		D3D11_SUBRESOURCE_DATA* pInitData = nullptr;
		unsigned char* imgData = nullptr;

		if (fileName && cTex->type != tType::cube) {

			char path[MAX_PATH];
			strcpy_s(path, MAX_PATH, "textures/");
			strcat_s(path, MAX_PATH, fileName);

			int width = 0, height = 0, channels = 0;

			stbi_set_flip_vertically_on_load(true);
			imgData = stbi_load(path, &width, &height, &channels, 4);

			if (imgData) {
				cTex->size.x = width;
				cTex->size.y = height;
				tdesc.Width = width;
				tdesc.Height = height;
				tdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 

				tdesc.MipLevels = cTex->mipMaps ? (UINT)0 : 1;

				initData.pSysMem = imgData;
				initData.SysMemPitch = width * 4;
				initData.SysMemSlicePitch = 0;
				pInitData = &initData;

				context->UpdateSubresource(Texture[i].pTexture, 0, nullptr, imgData, tdesc.Width * 4, 0);

				if (tdesc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) {
					context->GenerateMips(Texture[i].TextureResView);
				}

				stbi_image_free(imgData);
				//Log((std::string(fileName)+" loaded").c_str());
			}
			else {
				Log("Failed to load PNG file from disk");
			}
		}
	}

#endif

	void Create(int i, tType type, tFormat format, int width, int height, bool mipMaps, bool depth, const char* fileName = NULL)
	{
		auto cTex = &Texture[i];
		ZeroMemory(&cTex->tdesc, sizeof(cTex->tdesc));
		ZeroMemory(&svDesc, sizeof(svDesc));
		ZeroMemory(&renderTargetViewDesc, sizeof(renderTargetViewDesc));
		ZeroMemory(&descDSV, sizeof(descDSV));

		Texture[i].type = type;
		Texture[i].format = format;
		Texture[i].size = XMFLOAT2{ (float)width, (float)height };
		Texture[i].mipMaps = mipMaps;
		Texture[i].depth = depth;

		if (i > 0)// main rt skipped, init in device init
		{
		#if !NoExternalResources
			LoadTexture(i, fileName);
		#endif
			CreateTex(i);
			CreateShaderRes(i);
		#if !NoExternalResources
			TextureToGPU(i);
		#endif


			CreateRTView(i);
		}

		if (depth)
		{
			CreateDepth(i);
			CreateShaderResDepth(i);
		}
	}

	void UnbindAll()
	{
		ID3D11ShaderResourceView* const null[128] = { NULL };
		context->VSSetShaderResources(0, 128, null);
		context->PSSetShaderResources(0, 128, null);
	}



	void CopyColor(int dst, int src)
	{
		context->CopyResource(Texture[(int)dst].pTexture, Texture[(int)src].pTexture);
	}

	void CopyDepth(int dst, int src)
	{
		context->CopyResource(Texture[(int)dst].pDepth, Texture[(int)src].pDepth);
	}

	void TextureToShader(auto tex, unsigned int slot, targetshader tA = targetshader::both)
	{

		if (tA == targetshader::both || tA == targetshader::vertex)
		{
			context->VSSetShaderResources(slot, 1, &Texture[(int)tex].TextureResView);
		}

		if (tA == targetshader::both || tA == targetshader::pixel)
		{
			context->PSSetShaderResources(slot, 1, &Texture[(int)tex].TextureResView);
		}
	}

	void CreateMipMap()
	{
		context->GenerateMips(Texture[currentRT].TextureResView);
	}


	

} 

