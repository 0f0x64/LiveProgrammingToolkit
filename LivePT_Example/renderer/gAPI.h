namespace gApi {

	using namespace dx11;

	struct spriteDesc {
		float4 pos;
		float4 size;
		float4 color;
		TexList::sprites texture;
	};

	LPT_LIVE_FUNCTION(void, ShowSprite, spriteDesc, in)
	{
		Shaders::vs::sprite::params = { in.pos };
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
}