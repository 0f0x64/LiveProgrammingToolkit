namespace App {
	
	void init()
	{
        timer::StartCounter();
	}

	void Loop()
	{
        gApi::Start();

        gApi::SetCamera2d({
            .eye = pos2{20.18f, 11.00f},
            .angle = 109
            });

        gApi::Clear(color3{59, 44, 79});

        gApi::ShowSprite({
                .pos = pos2{-0.18f, 0.00f},
                .size = size2{1.0f,1.0f},
                .rotation = angle(0.0f),
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = TexList::sprites::spr1
            });

        gApi::ShowSprite({
                .pos = pos2{ 0.637f,0.0f},
                .size = size2{0.68f, 0.66f},
                .rotation = angle(3.1f),
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = TexList::sprites::spr2
            });

        gApi::End();

	}

}