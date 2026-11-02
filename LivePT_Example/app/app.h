namespace App {
	
    double prevFrameTime;

	void init()
	{
        timer::StartCounter();
        prevFrameTime = timer::GetCounter();
	}


    void TryPlayClientSound(size_t index, double cooldownMS) {
        if (index >= Audio::MAX_SOUNDS) return;

        // Статические массивы инициализируются нулями/false один раз при старте программы
        static double s_ClientSoundTimers[Audio::MAX_SOUNDS]{};
        static bool s_IsInitialized[Audio::MAX_SOUNDS]{}; // По умолчанию все false

        double currentTimeMS = timer::GetCounter();

        // Если звук воспроизводится первый раз в игре ИЛИ кулдаун честно прошёл
        if (!s_IsInitialized[index] || (currentTimeMS - s_ClientSoundTimers[index] >= cooldownMS)) {

            // Вызываем проигрывание в аудиомодуле
            Audio::PlaySoundX(index);

            // Фиксируем время кадра и взводим флаг инициализации
            s_ClientSoundTimers[index] = currentTimeMS;
            s_IsInitialized[index] = true;
        }
    }

	void Loop()
	{
        auto frameTime = timer::GetCounter();
        if (frameTime < prevFrameTime + 16) return;
        prevFrameTime = frameTime;

        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
          //  TryPlayClientSound(0, eval(0.1)* 16 * 60);
        }




        gApi::Start();

        gApi::SetCamera2d({
            .eye = pos2{0.0f, 0.0f},
            .angle = 109
            });

        gApi::Clear(color3{12, 8, 35});

        static int frame = 0;

        int base_sc = (int)TexList::sciss::sc1;
        int base_pap = (int)TexList::paper::pap1;
        float rt = sin(frameTime / 100.)*3;

        gApi::ShowSprite({
                .pos = pos2{0.0f, -0.0f},
                .size = size2{2.9f,2.9f},
                .rotation = 0.f,
                .rotCentre = pos2 {0.f,.0f},
                .color = float4{0.0f,0.0f,0.0f,0.0f},
                .texture = 1
            });

        gApi::ShowSprite({
                .pos = pos2{0.0f, -0.1f},
                .size = size2{1.32f, 1.32f},
                .rotation = 0.f,
                .rotCentre = pos2 {0.f,.0f},
                .color = float4{0.0f,0.0f,0.0f,0.0f},
                .texture = 2
            });

        gApi::ShowSprite({
                .pos = pos2{0.0f, -0.1f},
                .size = size2{1.32f, 1.32f},
                .rotation = 0.f,
                .rotCentre = pos2 {0.f,.0f},
                .color = float4{0.0f,0.0f,0.0f,0.0f},
                .texture = 3
            });

        gApi::ShowSprite({
                .pos = pos2{-0.65f, -0.4f},
                .size = size2{-1.45f,1.45f},
                .rotation = rt,
                .rotCentre = pos2 {1.f,.0f},
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = base_sc + frame
        });

        gApi::ShowSprite({
                .pos = pos2{ 0.65f,-0.4f},
                .size = size2{1.45f, 1.45f},
                .rotation = rt,
                .rotCentre = pos2 {1.f,0.f},
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = base_pap + frame
            });

        gApi::End();

        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
            frame++;
        }
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
            frame--;
        }
        //frame++;
        if (frame >7) frame-= 8;
        if (frame < 0) frame += 8;
	}

}