namespace App {
	
	void init()
	{
        timer::StartCounter();
	}

	void Loop()
	{
        gApi::Start();

        gApi::Clear(color3{42, 38, 52});

        gApi::ShowSprite({
                .pos = pos2{ -0.492f,0.0f},
                .size = size2{1.0f,1.0f},
                .rotation = angle(0.0f),
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = (int)TexList::sprites::spr1
            });

        gApi::ShowSprite({
                .pos = pos2{ -0.492f,0.0f},
                .size = size2{1.0f,1.0f},
                .rotation = angle(0.0f),
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = (int)TexList::sprites::spr1
            });

        gApi::End();

	}

}