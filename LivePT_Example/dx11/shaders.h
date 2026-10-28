namespace Shaders {

	#define shadersPath "../LivePT_Example/shaders"

	#include "../shaders/shadersList.h"

//#if EditMode
//	#include "generated\uiShaderMargin.h"
//#endif

	#include "shadersReflection.h"

	const char VertexShaderModel[] = "vs_4_1";
	const char PixelShaderModel[] = "ps_4_1";




	//#include "shaderStructWrapMacro.h"
	//#include "../shaders/vs/sprite.shader"

	#if !EditMode

//	#include "..\generated\processedShaders.h"
//	#include "..\generated\libList.h"


	#endif

	int vsCount = 0;
	int psCount = 0;

	typedef struct {
		ID3D11VertexShader* pShader;
		ID3DBlob* pBlob;
		ID3D11Buffer* params;
	#if EditMode
		HRESULT compileStatus;
	#endif
	} VertexShader;

	typedef struct {
		ID3D11PixelShader* pShader;
		ID3DBlob* pBlob;
		ID3D11Buffer* params;
	#if EditMode
		HRESULT compileStatus;
	#endif

	} PixelShader;

	VertexShader VS[255];
	PixelShader PS[255];

	ID3DBlob* pErrorBlob;

	#if EditMode

		bool firstRun = true;
		const char* shaderExtension = ".shader";

		void CompilerLog(LPCWSTR source, HRESULT hr, const char* message, bool logOnlyifErrors = false)
		{
		#if DebugMode
			if (FAILED(hr))
			{
				Log((char*)pErrorBlob->GetBufferPointer(),"",false);
				if (firstRun) ExitProcess(0);
			}
			else
			{
				if (logOnlyifErrors) return;

				char shaderName[1024];
				WideCharToMultiByte(CP_ACP, NULL, source, -1, shaderName, sizeof(shaderName), NULL, NULL);

				char* shortName = strrchr(shaderName, '/');
				if (!shortName) shortName = strrchr(shaderName, '\\');
				shortName = shortName ? shortName + 1 : shaderName;

				char msg[2048];
				strcpy_s(msg, sizeof(msg), message);
				strcat_s(msg, sizeof(msg), shortName);

				Log(msg, "");
			}
		#endif
		}

		wchar_t shaderPathW[MAX_PATH];

		enum class stype {vertex, pixel};

		LPCWSTR nameToPatchLPCWSTR(const char* name, int i, stype type)
		{
			char path[MAX_PATH];
			strcpy_s(path, sizeof(path), shadersPath);
			strcat_s(path, sizeof(path), name);

			int len = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
			MultiByteToWideChar(CP_ACP, 0, path, -1, shaderPathW, len);

			return shaderPathW;
		}

		void CreateVS(int i, const char* name, bool logOnlyifErrors =  false)
		{
			LPCWSTR source = nameToPatchLPCWSTR(name,i,stype::vertex);

			HRESULT hr = D3DCompileFromFile(source, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VS", VertexShaderModel, NULL, NULL, &VS[i].pBlob, &pErrorBlob);
			CompilerLog(source, hr, "vertex shader compiled: ", hr == VS[i].compileStatus ? logOnlyifErrors : false);
			VS[i].compileStatus = hr;

			if (hr == S_OK)
			{
				hr = device->CreateVertexShader(VS[i].pBlob->GetBufferPointer(), VS[i].pBlob->GetBufferSize(), NULL, &VS[i].pShader);
				LogIfError("vs creation fail\n");
			}

		}

		void CreatePS(int i, const char* name, bool logOnlyifErrors = false)
		{
			LPCWSTR source = nameToPatchLPCWSTR(name, i, stype::pixel);

			HRESULT hr = D3DCompileFromFile(source, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PS", PixelShaderModel, NULL, NULL, &PS[i].pBlob, &pErrorBlob);
			CompilerLog(source, hr, "pixel shader compiled: ", hr == PS[i].compileStatus ? logOnlyifErrors : false);
			PS[i].compileStatus = hr;

			if (hr == S_OK)
			{
				hr = device->CreatePixelShader(PS[i].pBlob->GetBufferPointer(), PS[i].pBlob->GetBufferSize(), NULL, &PS[i].pShader);
				LogIfError("vs creation fail\n");
			}

		}

		void Init()
		{
			vsCount = sizeof(vsList) / sizeof(const char*);
			psCount = sizeof(psList) / sizeof(const char*);


		#if shaderCompileOnDemand
			firstRun = false;
			return;
		#endif	

			int i = 0;
			while (i < vsCount)
			{
				char fileName[255];
				strcpy_s(fileName,255, "/vs/");
				strcat_s(fileName,255, vsList[i]);
				strcat_s(fileName,255, shaderExtension);
				CreateVS(i, fileName);
				i++;

			}

			i = 0;
			while (i < psCount)
			{
				char fileName[255];
				strcpy_s(fileName,255, "/ps/");
				strcat_s(fileName,255, psList[i]);
				strcat_s(fileName,255, shaderExtension);
				CreatePS(i, fileName);
				i++;
			}

			firstRun = false;
		}

#else



/*		int getPtrIndex(const char* shaderName)
		{
			int j = 0; while (strcmp(libName[j], shaderName)) j++;
			return j;
		}
		*/

		char* outText;

		char* processIncludes(const char* shaderText)
		{
			if (!outText) outText = new char[640000];
			strcpy(outText, shaderText);
			char* inc = outText;

			while (true)
			{
				inc = strstr(inc, "#include");
				if (inc == NULL) break; 

				char* b1 = strstr(inc, "lib/") + 4;
				char* b2 = strstr(b1, ".");
				char* incEnd = strstr(b2, ">");
				char name[255];
				int sz = b2 - b1;
				memcpy(name, b1, sz);
				name[sz] = 0;
				int aa = strlen(name);

				int index = getPtrIndex(name);
				const char* includePtr = libPtr[index];
				int includeSize = strlen(libPtr[index]);

				int textSize = strlen(outText);
				memmove(inc + includeSize-1, incEnd + 1, textSize - (incEnd - outText)+1);
				memcpy(inc, includePtr, includeSize);

			}

			return outText;
		}

		void CreateVS(int n, const char* shaderText)
		{
			HRESULT hr;

			VS[n].pBlob = NULL;
			auto ptr = processIncludes(shaderText);
			hr = D3DCompile(ptr, strlen(ptr), NULL, NULL, NULL, "VS", VertexShaderModel, NULL, NULL, &VS[n].pBlob, &pErrorBlob);

			LogBlobIfError

			if (hr == S_OK)
			{
				if (VS[n].pShader) VS[n].pShader->Release();
				hr = device->CreateVertexShader(VS[n].pBlob->GetBufferPointer(), VS[n].pBlob->GetBufferSize(), NULL, &VS[n].pShader);
				LogIfError("vs fail\n"); 
			}
			else
			{
				int a = 0;
			}

		}


		void CreatePS(int n, const char* shaderText)
		{
			HRESULT hr = S_OK;

			PS[n].pBlob = NULL;
			auto ptr = processIncludes(shaderText);
			hr = D3DCompile(ptr, strlen(ptr), NULL, NULL, NULL, "PS", PixelShaderModel, NULL, NULL, &PS[n].pBlob, &pErrorBlob);

			LogBlobIfError;

			if (hr == S_OK)
			{
				if (PS[n].pShader) PS[n].pShader->Release();
				hr = device->CreatePixelShader(PS[n].pBlob->GetBufferPointer(), PS[n].pBlob->GetBufferSize(), NULL, &PS[n].pShader);
				LogIfError("ps fail\n");
			}
			else
			{
				auto a = ptr;
			}
		}

		//#include "..\generated\processedShadersCompile.h"

	#endif

	void vShader(vertex n)
	{
	#if EditMode
		if (!VS[(int)n].pShader)
		{
			char fileName[255];
			strcpy_s(fileName,255, "/vs/");
			strcat_s(fileName,255, vsList[(int)n]);
			strcat_s(fileName,255, shaderExtension);
			CreateVS((int)n, fileName);
		}
	#endif	
		context->VSSetShader(VS[(int)n].pShader, NULL, 0);
	}

	void pShader(pixel n)
	{
	#if EditMode
			if (!PS[(int)n].pShader)
			{
				char fileName[255];
				strcpy_s(fileName,255, "/ps/");
				strcat_s(fileName,255, psList[(int)n]);
				strcat_s(fileName,255, shaderExtension);
				CreatePS((int)n, fileName);
			}
	#endif
		context->PSSetShader(PS[(int)n].pShader, NULL, 0);
	}

	void resetShader(unsigned int n)
	{
		context->PSSetShader(NULL, NULL, 0);
	}

}


//#include "generated\constBufReflect.h"


void paramConstBufInit()
{
	#undef shader
	int i = 0;
	#define shader(name) dx11::ConstBuf::Create(dx11::Shaders::VS[i].params, sizeof(dx11::Shaders::vs::name::params));i++;
	#include "../shaders/vsList.h"

	#undef shader
	i = 0;
	#define shader(name) dx11::ConstBuf::Create(Shaders::PS[i].params, sizeof(Shaders::ps::name::params));i++;
	#include "../shaders/psList.h"
}

#undef Shader
