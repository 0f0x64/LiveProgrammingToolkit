namespace App {
	
	void init()
	{
        timer::StartCounter();
	}

	void Loop()
	{
        gApi::Start();

        gApi::Clear(gApi::color3{42, 38, 52});

        gApi::ShowSprite({
                .pos = gApi::pos2(-0.492f,0.0f),
                .size = gApi::size2(1.0f,1.0f),
                .rotation = gApi::angle(0.0f),
                .color = float4(1.0f,1.0f,1.0f,1.0f),
            });

        gApi::ShowSprite({
                .pos = gApi::pos2(-0.492f,0.0f),
                .size = gApi::size2(1.0f,1.0f),
                .rotation = gApi::angle(0.0f),
                .color = float4(1.0f,1.0f,1.0f,1.0f),
            });

        gApi::End();

	}

}