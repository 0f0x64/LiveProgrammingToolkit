typedef struct {
	XMVECTOR eye;
	XMVECTOR at;
	XMVECTOR up;
	float angle;
} camData;

namespace Camera
{
	using namespace dx11;

	#ifndef M_PI
	#define M_PI 3.14159265358979323846
	#endif

	float DegreesToRadians(float degrees)
	{
		return degrees * (float)M_PI / 180.f;
	}

	float RadiansToDegrees(float radians)
	{
		return radians * 180.f / (float)M_PI;
	}

	#if EditMode

	struct {

		XMMATRIX world;
		XMMATRIX view;
		XMMATRIX proj;

		bool overRide = true;
		float angle = 100.f;

		void Init()
		{
			XMVECTOR At = XMVECTOR{ 0,0,0,1 };
			XMVECTOR Eye = XMVECTOR{ 0,0,13,1 };
			XMVECTOR Up = XMVECTOR{ 0,1,0,1 };

			world = XMMatrixIdentity();
			view = XMMatrixLookAtLH(Eye, At, Up);
			proj = XMMatrixPerspectiveFovLH(DegreesToRadians(angle), screen.aspect, 0.01f, 100.0f);
		}

		void pInit()
		{
			proj = XMMatrixPerspectiveFovLH(DegreesToRadians(angle), screen.aspect, 0.01f, 100.0f);
		}


	} viewCam;

	#endif

	void Set(float3 eye, float3 at, float3 up, float angle)
	{

		XMVECTOR Eye = XMVECTOR{eye.x,eye.y,eye.z, 0.0f};
		XMVECTOR At =  XMVECTOR{at.x, at.y, at.z, 0.0f};
		XMVECTOR Up =  XMVECTOR{up.x, up.y, up.z, 0.0f};

		ConstBuf::camera[0] = {
			.world = XMMatrixIdentity(),
			.view = XMMatrixTranspose(XMMatrixLookAtLH(Eye, At, Up)),
			.proj = XMMatrixTranspose(XMMatrixPerspectiveFovLH(DegreesToRadians(angle), dx11::screen.iaspect, 0.01f, 100.0f))
		};

		ConstBuf::Update(ConstBuf::cBuffer::camera);
		ConstBuf::Set(ConstBuf::cBuffer::camera, targetshader::both);
	}
}