namespace sound {

	struct soundpoly {
		int index;
		double cooldownMS;
	};

	LPT_LIVE_FUNCTION(void, PlayPoly, soundpoly, in) {
		if (in.index >= Audio::MAX_SOUNDS) return;

		static double s_ClientSoundTimers[Audio::MAX_SOUNDS]{};
		static bool s_IsInitialized[Audio::MAX_SOUNDS]{};

		double currentTimeMS = timer::GetCounter();

		if (!s_IsInitialized[in.index] || (currentTimeMS - s_ClientSoundTimers[in.index] >= in.cooldownMS)) {

			Audio::PlaySoundX(in.index);

			s_ClientSoundTimers[in.index] = currentTimeMS;
			s_IsInitialized[in.index] = true;
		}
	}

	struct sound {
		int index;
	};

	LPT_LIVE_FUNCTION(void, Play, sound, in) {
		if (in.index >= Audio::MAX_SOUNDS) return;

		Audio::PlaySoundX(in.index);
	}

	void preLoad() {

		Audio::LoadSound(0, "punch.ogg");
		Audio::LoadSound(1, "lose_scissors_stone.ogg");
		Audio::LoadSound(2, "win_paper_stone.ogg");
		Audio::LoadSound(3, "win_scissors_paper.ogg");
		Audio::LoadSound(4, "gong.ogg");
	}


}