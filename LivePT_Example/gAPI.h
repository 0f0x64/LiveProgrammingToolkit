
struct in {

};

LPT_LIVE_FUNCTION(void, ShowSprite, in)
Shaders::vs::sprite::params = { eval(float4{ -0.43f,0.0f,0.0f,0.0f }) };
ConstBuf::Update(dx11::Shaders::VS[(int)Shaders::vertex::sprite].params, Shaders::vs::sprite::params);
Shaders::SetVSParamCB(Shaders::vertex::sprite);
Shaders::vShader(Shaders::vertex::sprite);

Shaders::ps::sprite::params = { eval(float4{ 1.0f,0.0f,0.0f,1.0f }) };
ConstBuf::Update(dx11::Shaders::PS[(int)Shaders::pixel::sprite].params, Shaders::ps::sprite::params);
Shaders::SetPSParamCB(Shaders::pixel::sprite);
Textures::TextureToShader(TexList::sprites::spr1, 0, targetshader::pixel);
Sampler::Sampler(targetshader::pixel, 0, filter::linear, addr::wrap, addr::wrap);
Shaders::pShader(Shaders::pixel::sprite);

Draw::NullDrawer(1, 1);

