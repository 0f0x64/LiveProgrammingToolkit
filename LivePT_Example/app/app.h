namespace App {
	
    enum class phase {
        start,
        fight,
        end,
        restart
    };

    phase gameState = phase::start;

    enum class select { stone, scissors, paper };

    enum { win, lose, draft };
    int result = 0;
    
    //struct player

    int LScore = 0;
    select LSelect = select::stone;
    float lAmp = 0;
    float rAmp = 0;
    bool enemyPressed = false;
    float Lofs = -1.05f;
    static bool p1ready = false;
    static int frameL = 0;
      
    int RScore = 0;
    select RSelect = select::stone;
    float rtL = 0;
    float rtR = 0;
    bool keyPressed = false;
    float Rofs = +1.05f;
    static bool p2ready = false;
    static int frameR = 0;

    double prevFrameTime;
    double frameTime;
    double gongTime;
    double fightStartTime;
    double pTimer;
    double endTimer;
    double restartTime;
    double pressedTime = 0;

    bool endTrigger =  false;
    bool restartTrigger = false;

    float gongAmp = 0;

    int base_sc;
    int base_pap;
    float leftX;
    float rightX;




	void init()
	{
        sound::preLoad();
        timer::StartCounter();
        prevFrameTime = timer::GetCounter();
	}

    void ShowBackground()
    {
        gfx::ShowSprite({
         .pos = pos2{0.0f, -0.0f},
         .size = size2{2.9f,2.9f},
         .rotation = float4{0.0f,0.0f,0.0f,0.0f},
         .rotCentre = float4{0.0f,0.0f,0.0f,0.0f},
         .color = float4{0.0f,0.0f,0.0f,0.0f},
         .texture = 1
            });

        gfx::ShowSprite({
                .pos = pos2{0.0f, -0.20f},
                .size = size2{1.32f, 1.32f},
                .rotation = float4{0.0f,0.0f,0.0f,0.0f},
                .rotCentre = float4{0.0f,0.0f,0.0f,0.0f},
                .color = float4{0.0f,0.0f,0.0f,0.0f},
                .texture = 2
            });

        float gongMovement = gongAmp*(eval(1.f)*sin(frameTime/eval(350.)));

        gfx::ShowSprite({
                .pos = pos2{0.0f, -.2f},
                .size = size2{1.32f, 1.32f},
                .rotation = float4{gongMovement,0.0f,0.0f,0.0f},
                .rotCentre = float4{0.0f,.250f,0.0f,0.0f},
                .color = float4{0.0f,0.0f,0.0f,0.0f},
                .texture = 3
            });

        int tlogo = (int)TexList::back::logo;
        gfx::ShowSprite({
                .pos = pos2{ 0.f,.74f},
                .size = size2{1.38f, 1.38f},
                .rotation = float4{0.f,0.f,0.f,0.f },
                .rotCentre = float4 {1.f,.0f,.0f,.0f },
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = tlogo
            });
    }

    void ShowHands()
    {
        int tTable[] = { base_sc ,base_sc,base_pap };
        int tLeft = tTable[(int)LSelect];

        static int tRight = base_sc;
        if (keyPressed && gameState == phase::fight)
        {
            tRight = tTable[(int)RSelect];
        }

        float Ljitter = 0;
        float Rjitter = 0;

        if (gameState == phase::end)
        {
            if (frameTime - endTimer < 1000)
            {
                if (result == win)
                {
                    Ljitter = ((rand() % 100) / 100.) * .05;
                }
                if (result == lose)
                {
                    Rjitter = ((rand() % 100) / 100.) * .05;
                }
            }

        }

        float LyPos = Ljitter - 0.4;
        float RyPos = Rjitter - 0.4;

        gfx::ShowSpriteFX({
                .pos = pos2{leftX, LyPos},
                .size = size2{-1.45f, 1.45f},
                .rotation = float4{0.f,0.f,rtL,0.f },
                .rotCentre = float4 {1.f,.0f,.0f,.0f },
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = tLeft + frameL
            });

        gfx::ShowSpriteFX({
                .pos = pos2{ rightX,RyPos},
                .size = size2{1.45f, 1.45f},
                .rotation = float4{0.f,0.f,rtR,0.f },
                .rotCentre = float4 {1.f,.0f,.0f,.0f },
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = tRight + frameR
            });
    }

    void ShowScore()
    {
        float step = eval(.11);

        for (int i = 0; i < LScore; i++)
        {
            int tex = (int)TexList::back::finger;
            gfx::ShowSprite({
                    .pos = pos2{-1.20f + step * (float)i, 1.23f},
                    .size = size2{0.14f, 0.19f},
                    .rotation = float4{0.f,0.f,0.f,0.f },
                    .rotCentre = float4 {1.f,.0f,.0f,.0f },
                    .color = float4{1.0f,1.0f,1.0f,1.0f},
                    .texture = tex
                });
        }
        for (int i = 0; i < RScore; i++)
        {
            int tex = (int)TexList::back::finger;
            gfx::ShowSprite({
                    .pos = pos2{1.20f - step * (float)i, 1.23f},
                    .size = size2{0.14f, 0.19f},
                    .rotation = float4{0.f,0.f,0.f,0.f },
                    .rotCentre = float4 {1.f,.0f,.0f,.0f },
                    .color = float4{1.0f,1.0f,1.0f,1.0f},
                    .texture = tex
                });
        }
    }

	void Loop()
	{
        frameTime = timer::GetCounter();
        if (frameTime < prevFrameTime + 16) return;
        prevFrameTime = frameTime;

        gfx::Start();

        gfx::SetCamera2d({
            .eye = pos2{0.0f, 0.0f},
            .angle = 109
            });

        gfx::Clear(color3{12, 8, 35});

        ShowBackground();

        base_sc = (int)TexList::sciss::sc1;
        base_pap = (int)TexList::paper::pap1;

        float basepos = 0.65f;

        if (p1ready) {
            Lofs *= eval(.9);
        }
        if (p2ready) {
            Rofs *= eval(.9);
        }

        leftX = -basepos + Lofs;
        rightX = basepos + Rofs;

        if (gameState == phase::start)
        {
            int t1 = (int)TexList::back::ready1;
            int t2 = (int)TexList::back::ready2;

            float amp1 = sin(frameTime / eval(200.)) * eval(.020);
            float amp2 = sin(frameTime / eval(200.)) * eval(.020);

            if (GetAsyncKeyState('S')) {
                p1ready = true;
                p2ready = true;
            }
            
            if (p1ready) amp1 = 0;
            if (p2ready) amp2 = 0;

            float presspos = eval(-1.1);
            amp1 += presspos;
            amp2 += presspos;

            if (!(p1ready && p2ready))
            {
                gfx::ShowSprite({
                    .pos = pos2{ -0.f,amp1},
                    .size = size2{1.f, .2f},
                    .rotation = float4{0.f,0.f,0.f,0.f },
                    .rotCentre = float4 {1.f,.0f,.0f,.0f },
                    .color = float4{1.0f,1.0f,1.0f,1.0f},
                    .texture = t1
                    });
            }
            static float NRtime = 0;

            //if (p1ready && p2ready) {
            if (p1ready) {

                int bframe = (int)TexList::back::t1;
                int btime = floor((frameTime - NRtime) / (16 * 60));
                if (btime > 2) btime = 2;
                bframe += btime;

                 pTimer = NRtime;
                float delta = frameTime - pTimer;
                if (delta > 16 * 60 * 3)
                {
                    sound::Play({ 4 });
                    gameState = phase::fight;
                    gongTime = frameTime;
                    gongAmp = eval(10);
                }
                else {
                    gfx::ShowSprite({
                             .pos = pos2{ 0.01f,-0.32f},
                             .size = size2{.2f, .2f},
                             .rotation = float4{0.f,0.f,0.f,0.f },
                             .rotCentre = float4 {1.f,.0f,.0f,.0f },
                             .color = float4{1.0f,1.0f,1.0f,1.0f},
                             .texture = bframe
                        });
                }
            }
            else
            {
                NRtime = frameTime;
            }

            rtL = 0;
            rtR = 0;
            lAmp = 3;
            rAmp = 3;
            fightStartTime = frameTime;
            keyPressed = false;
            enemyPressed = false;
        }

        if (gameState == phase::fight) {         

            if (!keyPressed)
            {
                if (GetAsyncKeyState('1'))
                {
                    keyPressed = true;
                    RSelect = select::stone;
                    //Audio::PlaySoundX(0);
                }

                if (GetAsyncKeyState('2'))
                {
                    keyPressed = true;
                    RSelect = select::scissors;
                    sound::Play({ 0 });
                }

                if (GetAsyncKeyState('3'))
                {
                    keyPressed = true;
                    RSelect = select::paper;
                    sound::Play({ 0 });
                }
            }

            float margin = 2500;
            float delta = frameTime - fightStartTime;

            if (delta > margin)
            {
                if (!enemyPressed)
                {
                    LSelect = (select)(rand() % 3);
                    enemyPressed = true;
                    if (LSelect != select::stone) {
                        sound::Play({ 0 });
                    }
                }

                if (LSelect != select::stone)
                {
                    frameL++;
                    if (frameL > 7) frameL = 7;
                }
            }
            else {
                rtL = sin((frameTime - fightStartTime) * (6. * M_PI / 3000.)) * lAmp;
            }

            if (keyPressed)
            {
                if (RSelect != select::stone)
                {
                    frameR++;
                    if (frameR > 7) frameR = 7;
                }
            }
            else
            {
                rtR = sin((frameTime - fightStartTime) * (6. * M_PI / 3000.)) * rAmp;
            }

            auto time = frameTime - fightStartTime;
            if (time > 3000-1000/4.)
            {
                gameState = phase::end;
                if (!keyPressed)
                {
                    keyPressed = true;
                    RSelect = select::stone;
                }
            }

            gongAmp *= .99;

            endTimer = frameTime;
            endTrigger = false;
        }

        if (gameState == phase::end) {

            if (!endTrigger)
            {
                int gameTable[3][3] = {
                  //user: stone   scissors  paper     //comp
                        { draft,  lose,     win },    //stone
                        { win,    draft,    lose },   //scissors
                        { lose,   win,      draft }   //paper
                };

                result = gameTable[(int)LSelect][(int)RSelect];

                if (result == win) RScore++;
                if (result == lose) LScore++;

                endTrigger =true;
            }

            if (frameTime - endTimer > 2000)
            {
                frameL--;
                frameR--;
                frameL = std::clamp(frameL, 0, 7);
                frameR = std::clamp(frameR, 0, 7);
                rtL *= .9;
                rtR *= .9;

            }

            if (frameTime - endTimer > 3000)
            {
                gameState = phase::start;
                lAmp = 3;
                rAmp = 3;

                if (LScore >= 5 || RScore >=5) {
                    gameState = phase::restart;
                }

            }

            restartTime = frameTime;
            restartTrigger = false;
        }


        //hands

        ShowHands();

        ShowScore();

        if (gameState == phase::restart) {
            
            int tex = 0;
            if (LScore > RScore) tex = (int)TexList::back::lose; else tex = (int)TexList::back::win;

            if (!restartTrigger)
            {
                restartTrigger = true;
                if (LScore > RScore) {
                    sound::Play({ 1 });
                }
                else {
                    sound::Play({ 2 });
                }
            }

            gfx::ShowSprite({
                    .pos = pos2{ 0.f,0.f},
                    .size = size2{1.f, 1.f},
                    .rotation = float4{0.f,0.f,0.f,0.f },
                    .rotCentre = float4 {1.f,.0f,.0f,.0f },
                    .color = float4{1.0f,1.0f,1.0f,1.0f},
                    .texture = tex
                });

            if (frameTime - restartTime > 3000)
            {
                gameState = phase::start;
                p1ready = false;
                p2ready = false;
                RScore = 0;
                LScore = 0;
            }
        }

        /*int tex = (int)TexList::sciss::sc1;
        gfx::ShowSpriteFX({
                .pos = pos2{0, 0.},
                .size = size2{-1.45f, 1.45f},
                .rotation = float4{0.f,0.f,0,0.f },
                .rotCentre = float4 {1.f,.0f,.0f,.0f },
                .color = float4{1.0f,1.0f,1.0f,1.0f},
                .texture = tex
            });*/


        gfx::End();

	}


    /*
    если игрок рано выбрал - противник палит выбор
    если противник тянет с выбором - возможность успеть рассмотреть уменьшается

    */
}