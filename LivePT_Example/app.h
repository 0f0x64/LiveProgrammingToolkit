namespace App {
	
	void init()
	{
        timer::StartCounter();
	}

	void Loop()
	{
        gApi::Start();

        gApi::Clear(color3{59, 44, 79});

        gApi::ShowSprite({
                .pos = pos2{-0.19f, 0.07f},
                .size = size2{1.0f,1.0f},
                .rotation = angle(0.0f),
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = TexList::sprites::spr1
            });

        gApi::ShowSprite({
                .pos = pos2{ 0.535f,0.0f},
                .size = size2{0.68f, 0.68f},
                .rotation = angle(3.1f),
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = TexList::sprites::spr2
            });

        gApi::End();

	}

}