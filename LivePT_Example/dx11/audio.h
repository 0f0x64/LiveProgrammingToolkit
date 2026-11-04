#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")

// Компилируем реализацию методов stb_vorbis непосредственно в этом файле
#define int32  STB_VORBIS_INT32_TMP
#define uint32 STB_VORBIS_UINT32_TMP
#define int16  STB_VORBIS_INT16_TMP
#define uint16 STB_VORBIS_UINT16_TMP
#include "../thirdParty/stb_vorbis.c"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Audio
{
    // Значения по умолчанию для процедурного синтезатора (для файлов заменятся реальными данными)
    constexpr DWORD SAMPLE_RATE = 44100;
    constexpr WORD CHANNELS = 1;
    constexpr WORD BITS_PER_SAMPLE = 16;

    constexpr size_t MAX_SOUNDS = 10;      // Максимум уникальных звуков в системе
    constexpr size_t VOICES_PER_SOUND = 4; // Пул полифонии (одновременных голосов) на каждый звук

    // Структура для хранения данных одного звука
    struct Sound {
        IXAudio2SourceVoice* sourceVoices[VOICES_PER_SOUND]{};
        BYTE* audioData = nullptr;
        DWORD dataSize = 0;
        double durationSeconds = 0.0;
        bool isUsed = false;
        bool isLoadedFromFile = false; // Флаг для разделения способов аллокации памяти
        bool isLooped = false;         // Флаг режима бесконечного повторения
        size_t nextVoiceIndex = 0;
    };

    // Структура аудио-движка
    struct AudioEngine {
        IXAudio2* pXAudio2 = nullptr;
        IXAudio2MasteringVoice* pMasterVoice = nullptr;
        Sound sounds[MAX_SOUNDS]{};
        HRESULT coInitResult = E_FAIL;
    };

    // Глобальный объект движка
    AudioEngine g_Engine{};

    using WaveGenerator = double(*)(double angle, double time, double duration);

    // --- Функция огибающей (Линейный Decay) ---
    double ApplyDecay(double time, double duration) {
        if (time >= duration) return 0.0;
        constexpr double lambda = 4.0;
        double exponentialFactor = exp(-lambda * (time / duration));
        double endValue = exp(-lambda);
        return (exponentialFactor - endValue) / (1.0 - endValue);
    }

    // --- Колбэки генерации аудиоданных для синтезатора ---
    double GenerateSawtooth(double angle, double time, double duration) {
        double wave = 2.0 * fmod(time * 440.0, 1.0) - 1.0;
        return wave * ApplyDecay(time, duration);
    }

    // --- Вспомогательная функция безопасной очистки аудиослота ---
    void FreeSoundSlot(Sound& sound) {
        if (!sound.isUsed) return;

        for (size_t v = 0; v < VOICES_PER_SOUND; ++v) {
            if (sound.sourceVoices[v]) {
                sound.sourceVoices[v]->DestroyVoice();
            }
        }
        if (sound.audioData) {
            if (sound.isLoadedFromFile) {
                free(sound.audioData); // stb_vorbis использует стандартный malloc
            }
            else {
                HeapFree(GetProcessHeap(), 0, sound.audioData); // Синтезатор работает с кучей Windows
            }
        }
        sound = {};
    }

    // Декларации функций управления, которые будут во второй части
    bool CreateSound(size_t index, double durationSeconds, WaveGenerator generator);
    bool LoadSound(size_t index, const char* filepath);
    void PlaySoundX(size_t index, bool loop);
    void PauseSoundX(size_t index);
    void StopSoundX(size_t index);
    void ReleaseAudioEngine();

    // --- Инициализация аудиосистемы ---
    bool Init() {
        g_Engine.coInitResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(g_Engine.coInitResult) && g_Engine.coInitResult != RPC_E_CHANGED_MODE) {
            return false;
        }

        if (FAILED(XAudio2Create(&g_Engine.pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
            if (g_Engine.coInitResult == S_OK) CoUninitialize();
            return false;
        }

        if (FAILED(g_Engine.pXAudio2->CreateMasteringVoice(&g_Engine.pMasterVoice))) {
            g_Engine.pXAudio2->Release();
            g_Engine.pXAudio2 = nullptr;
            if (g_Engine.coInitResult == S_OK) CoUninitialize();
            return false;
        }

        LoadSound(0, "punch.ogg");          
        LoadSound(1, "lose_scissors_stone.ogg");
        LoadSound(2, "win_paper_stone.ogg");
        LoadSound(3, "win_scissors_paper.ogg");
        LoadSound(4, "gong.ogg");

        return true;
    }

    // --- Конец Части 1. Продолжение функций генерации и загрузки ниже ---
    // --- Продолжение пространства имен Audio ---

    // --- Загрузка .ogg файла через stb_vorbis ---
    bool LoadSound(size_t index, const char* filepath) {
        if (index >= MAX_SOUNDS || !filepath) return false;

        char pName[MAX_PATH];
        strcpy_s(pName, MAX_PATH, "sound/");
        strcat_s(pName, MAX_PATH, filepath);

        Sound& sound = g_Engine.sounds[index];
        FreeSoundSlot(sound); // Сброс предыдущего контента из выбранного слота

        int channels = 0;
        int sampleRate = 0;
        short* outputBuffer = nullptr;

        // Полное декодирование файла из диска силами stb_vorbis
        int totalSamplesPerChannel = stb_vorbis_decode_filename(
            pName,
            &channels,
            &sampleRate,
            &outputBuffer
        );

        // Если возвращен отрицательный код — файл не найден или поврежден
        if (totalSamplesPerChannel < 0) {
            return false;
        }

        sound.isLoadedFromFile = true;
        sound.audioData = reinterpret_cast<BYTE*>(outputBuffer);

        WORD bitsPerSample = 16; // stb_vorbis гарантированно выдает 16-битный PCM
        WORD blockAlign = (channels * bitsPerSample) / 8;

        sound.dataSize = totalSamplesPerChannel * blockAlign;
        sound.durationSeconds = static_cast<double>(totalSamplesPerChannel) / sampleRate;
        sound.isLooped = false;

        // Конфигурируем структуру формата под реальные метаданные OGG файла
        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = static_cast<WORD>(channels);
        wfx.nSamplesPerSec = static_cast<DWORD>(sampleRate);
        wfx.wBitsPerSample = bitsPerSample;
        wfx.nBlockAlign = blockAlign;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;

        // Создаем пул голосов под формат данного файла
        for (size_t v = 0; v < VOICES_PER_SOUND; ++v) {
            if (FAILED(g_Engine.pXAudio2->CreateSourceVoice(&sound.sourceVoices[v], &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr))) {
                FreeSoundSlot(sound);
                return false;
            }
        }

        sound.isUsed = true;
        return true;
    }

    // --- Генерация процедурного звука ---
    bool CreateSound(size_t index, double durationSeconds, WaveGenerator generator) {
        if (index >= MAX_SOUNDS || !generator || durationSeconds <= 0.0) return false;

        Sound& sound = g_Engine.sounds[index];
        FreeSoundSlot(sound);

        sound.durationSeconds = durationSeconds;
        sound.isLoadedFromFile = false;
        sound.isLooped = false;

        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = CHANNELS;
        wfx.nSamplesPerSec = SAMPLE_RATE;
        wfx.wBitsPerSample = BITS_PER_SAMPLE;
        wfx.nBlockAlign = (CHANNELS * BITS_PER_SAMPLE) / 8;
        wfx.nAvgBytesPerSec = SAMPLE_RATE * wfx.nBlockAlign;
        wfx.cbSize = 0;

        for (size_t v = 0; v < VOICES_PER_SOUND; ++v) {
            if (FAILED(g_Engine.pXAudio2->CreateSourceVoice(&sound.sourceVoices[v], &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr))) {
                return false;
            }
        }

        const DWORD totalSamples = static_cast<DWORD>(SAMPLE_RATE * sound.durationSeconds);
        sound.dataSize = totalSamples * wfx.nBlockAlign;

        sound.audioData = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sound.dataSize));
        if (!sound.audioData) {
            for (size_t v = 0; v < VOICES_PER_SOUND; ++v) sound.sourceVoices[v]->DestroyVoice();
            return false;
        }

        short* samples = reinterpret_cast<short*>(sound.audioData);
        const double frequency = 440.0;
        for (DWORD i = 0; i < totalSamples; ++i) {
            double time = static_cast<double>(i) / SAMPLE_RATE;
            double angle = 2.0 * M_PI * frequency * time;
            samples[i] = static_cast<short>(16384.0 * generator(angle, time, sound.durationSeconds));
        }

        for (size_t v = 0; v < VOICES_PER_SOUND; ++v) {
            XAUDIO2_BUFFER buffer{};
            buffer.AudioBytes = sound.dataSize;
            buffer.pAudioData = sound.audioData;
            buffer.Flags = XAUDIO2_END_OF_STREAM;
            buffer.LoopCount = 0;

            if (FAILED(sound.sourceVoices[v]->SubmitSourceBuffer(&buffer))) {
                FreeSoundSlot(sound);
                return false;
            }
        }

        sound.isUsed = true;
        return true;
    }

    // --- Управление воспроизведением (Поддерживает полифонию и режим Loop) ---
    void PlaySoundX(size_t index, bool loop = false) {
        if (index >= MAX_SOUNDS || !g_Engine.sounds[index].isUsed) return;

        Sound& sound = g_Engine.sounds[index];
        sound.isLooped = loop;

        // Выбираем следующий по очереди голос из пула
        size_t chosenIndex = sound.nextVoiceIndex;
        IXAudio2SourceVoice* voiceToPlay = sound.sourceVoices[chosenIndex];

        // Проверяем, играет ли он сейчас
        XAUDIO2_VOICE_STATE state;
        voiceToPlay->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

        // Если голос занят (например, звук длинный, а мы быстро кликаем по кругу)
        if (state.BuffersQueued > 0) {
            voiceToPlay->Stop(0);
            voiceToPlay->FlushSourceBuffers();
        }

        // Заправляем буфер непосредственно перед стартом
        XAUDIO2_BUFFER buffer = { 0 };
        buffer.AudioBytes = sound.dataSize;
        buffer.pAudioData = sound.audioData;
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.LoopCount = sound.isLooped ? XAUDIO2_LOOP_INFINITE : 0;

        voiceToPlay->SubmitSourceBuffer(&buffer);
        voiceToPlay->Start(0);

        // Сдвигаем индекс для следующего вызова (0 -> 1 -> 2 -> 3 -> 0)
        sound.nextVoiceIndex = (chosenIndex + 1) % VOICES_PER_SOUND;
    }


    // --- Поставить на паузу все голоса звука ---
    void PauseSoundX(size_t index) {
        if (index < MAX_SOUNDS && g_Engine.sounds[index].isUsed) {
            for (size_t v = 0; v < VOICES_PER_SOUND; ++v) {
                g_Engine.sounds[index].sourceVoices[v]->Stop(0);
            }
        }
    }

    // --- Мгновенная остановка с очисткой бесконечного цикла ---
    void StopSoundX(size_t index) {
        if (index >= MAX_SOUNDS || !g_Engine.sounds[index].isUsed) return;

        Sound& sound = g_Engine.sounds[index];
        sound.isLooped = false;

        for (size_t v = 0; v < VOICES_PER_SOUND; ++v) {
            sound.sourceVoices[v]->Stop(0);
            sound.sourceVoices[v]->FlushSourceBuffers(); // Просто очищаем, ничего заново не шлем!
        }
    }

    // --- Корректное освобождение ресурсов при выходе из приложения ---
    void ReleaseAudioEngine() {
        for (size_t i = 0; i < MAX_SOUNDS; ++i) {
            FreeSoundSlot(g_Engine.sounds[i]);
        }

        if (g_Engine.pMasterVoice) g_Engine.pMasterVoice->DestroyVoice();
        if (g_Engine.pXAudio2) g_Engine.pXAudio2->Release();

        if (g_Engine.coInitResult == S_OK) CoUninitialize();
    }
} // namespace Audio

#undef int32  
#undef uint32 
#undef int16  
#undef uint16 