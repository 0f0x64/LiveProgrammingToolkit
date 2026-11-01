#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")

namespace Audio
{
#define MAXCHANNELS 32
#define max_audio 256

	struct RIFF_HEADER {
		char chunkId[4];
		unsigned long chunkSize;
		char format[4];
	};

	struct WAVE_FORMAT {
		char subChunkId[4];
		unsigned long subChunkSize;
		unsigned short audioFormat;
		unsigned short numChannels;
		unsigned long sampleRate;
		unsigned long byteRate;
		unsigned short blockAlign;
		unsigned short bitsPerSample;
	};

	struct WAVE_DATA {
		char subChunkId[4];
		unsigned long subChunkSize;
	};

	struct soundDesc {
		std::vector<BYTE> data;
		WAVEFORMATEX format;
	};

	struct VoiceController {
		IXAudio2SourceVoice* voice;
		int soundIndex;
		bool isLooping;
		float volume;

		VoiceController() : voice(nullptr), soundIndex(-1), isLooping(false), volume(1.0f) {}
		VoiceController(IXAudio2SourceVoice* v, int idx, bool loop, float vol)
			: voice(v), soundIndex(idx), isLooping(loop), volume(vol) {
		}
	};

	IXAudio2* pXAudio2;
	IXAudio2MasteringVoice* pMasteringVoice;
	XAUDIO2_BUFFER buffer;
	BYTE* channel[MAXCHANNELS];

	int len;
	int channelLen;

	soundDesc Sounds[max_audio];

	int soundsCount;

	void Init() {
		IXAudio2SourceVoice* pSourceVoice;

		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(hr)) {
			Log("Failed to coinitialize ex");
			return;
		}

		hr = XAudio2Create(&pXAudio2, 0);
		if (FAILED(hr)) {
			Log("Failed to create XAudio2");
			return;
		}

		hr = pXAudio2->CreateMasteringVoice(&pMasteringVoice);
		if (FAILED(hr)) {
			Log("Failed to create mastering voice");
			return;
		}

		for (int x = 0; x < MAXCHANNELS - 1; x++)
		{
			channel[x] = new BYTE[channelLen * 2 * 2];
			ZeroMemory(channel[x], channelLen * 2 * 2);
		}

		channel[MAXCHANNELS - 1] = new BYTE[len * 2 * 2];
		ZeroMemory(channel[MAXCHANNELS - 1], len * 2 * 2);

		WAVEFORMATEX waveformat;
		waveformat.wFormatTag = WAVE_FORMAT_PCM;
		waveformat.nChannels = 2;
		waveformat.nSamplesPerSec = 44100;
		waveformat.nAvgBytesPerSec = 44100 * 2 * 2;
		waveformat.nBlockAlign = 2 * 16 / 8;
		waveformat.wBitsPerSample = 16;
		waveformat.cbSize = 0;

		hr = pXAudio2->CreateSourceVoice(&pSourceVoice, &waveformat);

		ZeroMemory(&buffer, sizeof(buffer));
		buffer.AudioBytes = 2 * 2 * len;
		buffer.pAudioData = channel[MAXCHANNELS - 1];
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		buffer.PlayBegin = 0;
		buffer.PlayLength = 0;

		hr = pSourceVoice->SubmitSourceBuffer(&buffer);
		pSourceVoice->Stop(0, 0);
	};

	IXAudio2SourceVoice* Play(int soundIndex, bool loop = false, float volume = 1.0f) {
		if (soundIndex < 0 || soundIndex >= soundsCount) return nullptr;

		soundDesc& sound = Sounds[soundIndex];
		if (sound.data.empty()) return nullptr;

		IXAudio2SourceVoice* pVoice = nullptr;
		HRESULT hr = pXAudio2->CreateSourceVoice(&pVoice, &sound.format);
		if (FAILED(hr)) return nullptr;

		XAUDIO2_BUFFER voiceBuffer = {};
		voiceBuffer.pAudioData = sound.data.data();
		voiceBuffer.AudioBytes = (UINT32)sound.data.size();
		voiceBuffer.Flags = XAUDIO2_END_OF_STREAM;

		// Если зациклено, устанавливаем флаг
		if (loop) {
			voiceBuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
			voiceBuffer.LoopBegin = 0;
			voiceBuffer.LoopLength = 0;
		}

		hr = pVoice->SubmitSourceBuffer(&voiceBuffer);
		if (FAILED(hr)) {
			pVoice->DestroyVoice();
			return nullptr;
		}

		// Устанавливаем громкость
		if (volume < 0.0f) volume = 0.0f;
		if (volume > 1.0f) volume = 1.0f;
		pVoice->SetVolume(volume);

		pVoice->Start(0);

		// Сохраняем контроллер
		activeVoices.push_back(VoiceController(pVoice, soundIndex, loop, volume));

		return pVoice;
	}

	IXAudio2SourceVoice* Play(const std::string& name, bool loop = false, float volume = 1.0f) {
		auto it = SoundName.find(name);
		if (it != SoundName.end()) {
			return Play(it->second, loop, volume);
		}
		return nullptr;
	}

	void Pause(IXAudio2SourceVoice* pVoice) {
		if (pVoice) {
			pVoice->Stop(0);
		}
	}

	void Resume(IXAudio2SourceVoice* pVoice) {
		if (pVoice) {
			pVoice->Start(0);
		}
	}

	void Stop(IXAudio2SourceVoice* pVoice) {
		if (pVoice) {
			pVoice->Stop(0);
		}
	}

	void Stop(const std::string& name) {
		auto it = SoundName.find(name);
		if (it == SoundName.end()) return;

		int soundIndex = it->second;
		for (auto& controller : activeVoices) {
			if (controller.voice) {
				controller.voice->Stop(0);
			}
		}
	}

	void StopAll() {
		for (auto& controller : activeVoices) {
			if (controller.voice) {
				controller.voice->Stop(0);
				controller.voice->DestroyVoice();
				controller.voice = nullptr;
			}
		}
		activeVoices.clear();
	}

	void SetVolume(IXAudio2SourceVoice* pVoice, float volume) {
		if (pVoice) {
			if (volume < 0.0f) volume = 0.0f;
			if (volume > 1.0f) volume = 1.0f;

			pVoice->SetVolume(volume);
		}
	}

	void SetVolume(const std::string& name, float volume) {
		auto it = SoundName.find(name);
		if (it == SoundName.end()) return;

		int soundIndex = it->second;
		for (auto& controller : activeVoices) {
			if (controller.voice) {
				SetVolume(controller.voice, volume);
			}
		}
	}

	float GetVolume(IXAudio2SourceVoice* pVoice) {
		if (!pVoice) return 0.0f;
		float volume;
		pVoice->GetVolume(&volume);
		return volume;
	}

	void DeleteVoice(IXAudio2SourceVoice* pVoice) {
		pVoice->DestroyVoice();
	};

	bool IsPlaying(IXAudio2SourceVoice* pVoice) {
		if (!pVoice) return false;
		XAUDIO2_VOICE_STATE state;
		pVoice->GetState(&state);
		return state.BuffersQueued > 0;
	}

	void UpdateVoices() {
		for (auto it = activeVoices.begin(); it != activeVoices.end(); ) {
			if (!it->voice || !IsPlaying(it->voice)) {
				if (it->voice) {
					it->voice->DestroyVoice();
				}
				it = activeVoices.erase(it);
			}
			else {
				++it;
			}
		}
	}
