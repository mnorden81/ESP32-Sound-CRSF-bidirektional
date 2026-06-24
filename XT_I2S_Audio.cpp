// XTronical I2S Audio Library, angepasst: Ringpuffer für I2S (ESP32)
// Basis: XT_I2S_Audio - Kopie
// Änderungen: Ringbuffer + Consumer Task + Stabilitäts- und Performance-Fixes
// (c) XTronical 2018-2021, Anpassungen 2025

#include "XT_I2S_Audio.h"
#include <math.h>
#include <cstring>
#include <hardwareSerial.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "SD.h"

WavHeader_Struct WavHeader; // Used as a place to store the header data from the wav data

static portMUX_TYPE s_ringInitMux = portMUX_INITIALIZER_UNLOCKED;


/**********************************************************************************************************************/
/* Ringbuffer configuration                                                                                           */
/**********************************************************************************************************************/
// SLOT_BYTES muss Vielfaches von 4 sein. Wähle passend zur i2s_config.dma_buf_len (z.B. 256 oder 512).
#define SLOT_BYTES       512
#define SLOT_WORDS       (SLOT_BYTES / 4)
#define RING_SLOTS       16 // erhöht auf 16 für besseren Puffer
static_assert((RING_SLOTS & (RING_SLOTS - 1)) == 0, "RING_SLOTS must be power of two");

static uint32_t ringBuffer[RING_SLOTS][SLOT_WORDS];
// use larger index type to avoid surprises if RING_SLOTS grows
static volatile uint32_t ringHead = 0;
static volatile uint32_t ringTail = 0;
static SemaphoreHandle_t ringMutex = nullptr;
static SemaphoreHandle_t slotCountSem = nullptr;  // gefüllte Slots
static SemaphoreHandle_t freeSlotSem = nullptr;   // freie Slots
static TaskHandle_t i2sConsumerTaskHandle = nullptr;
static TaskHandle_t i2sProducerTaskHandle = nullptr;
static volatile XT_I2S_Class *s_pI2SAudioInstance = nullptr;  // Pointer zur Instanz für Producer Task

/**********************************************************************************************************************/
/* global functions                                                                                                   */
/**********************************************************************************************************************/

// Fixed-point volume: Volume percent 0..100 mapped to Q8.8 (scale = (Volume*256)/100)
void SetVolume(int16_t *Left, int16_t *Right, int16_t Volume)
{
    if (Volume < 0) return;
    if (Volume == 100) return; // no-op fast path
    uint32_t scale = (uint32_t(Volume) * 256u) / 100u; // Q8.8
    int32_t l = (int32_t)(*Left) * (int32_t)scale;
    int32_t r = (int32_t)(*Right) * (int32_t)scale;
    *Left = int16_t((l + 128) >> 8);
    *Right = int16_t((r + 128) >> 8);
}

/**********************************************************************************************************************/
/* XT_PlayListItem_Class                                                                                              */
/**********************************************************************************************************************/

XT_PlayListItem_Class::XT_PlayListItem_Class()
{
    RepeatForever = false;
    Repeat = 0;
    RepeatIdx = 0;
}

void XT_PlayListItem_Class::NextSample(int16_t *Left, int16_t *Right)
{
}
void XT_PlayListItem_Class::Init()
{
}

/**********************************************************************************************************************/
/* Wave class                                                                                                         */
/**********************************************************************************************************************/
void XT_Wave_Class::NextSample(int16_t *Left, int16_t *Right)
{
}
void XT_Wave_Class::Init(int8_t Note)
{
}

/**********************************************************************************************************************/
/* XT_WAV_Class                                                                                                       */
/**********************************************************************************************************************/

#define DATA_CHUNK_ID 0x61746164
#define FMT_CHUNK_ID 0x20746d66
#define longword(bfr, ofs) (bfr[ofs + 3] << 24 | bfr[ofs + 2] << 16 | bfr[ofs + 1] << 8 | bfr[ofs + 0])

XT_Wav_Class::XT_Wav_Class(const String &name) // Load data from file on SD Card
    : FileName{name}
{
    SamplesBufferIsEmpty = true;
}

/****** WAV helper methods (improved checks) ****************************************/

bool XT_Wav_Class::ValidWavData(WavHeader_Struct *Wav)
{
    if (memcmp(Wav->RIFFSectionID, "RIFF", 4) != 0)
    {
        Serial.print("Invalid data - Not RIFF format");
        return false;
    }
    if (memcmp(Wav->RiffFormat, "WAVE", 4) != 0)
    {
        Serial.print("Invalid data - Not Wave file");
        return false;
    }
    if (memcmp(Wav->FormatSectionID, "fmt", 3) != 0)
    {
        Serial.print("Invalid data - No format section found");
        return false;
    }
    if (memcmp(Wav->DataSectionID, "data", 4) != 0)
    {
        Serial.print("Invalid data - data section not found");
        return false;
    }
    if (Wav->FormatID != 1)
    {
        Serial.print("Invalid data - format Id must be 1");
        return false;
    }
    if (Wav->FormatSize != 16)
    {
        Serial.print("Invalid data - format section size must be 16.");
        return false;
    }
    if ((Wav->NumChannels != 1) && (Wav->NumChannels != 2))
    {
        Serial.print("Invalid data - only mono or stereo permitted.");
        return false;
    }
    if (Wav->SampleRate > 48000)
    {
        Serial.print("Invalid data - Sample rate cannot be greater than 48000");
        return false;
    }
    if ((Wav->BitsPerSample != 8) && (Wav->BitsPerSample != 16))
    {
        Serial.print("Invalid data - Only 8 or 16 bits per sample permitted.");
        return false;
    }
    // additional consistency checks
    uint32_t expectedByteRate = Wav->SampleRate * Wav->NumChannels * (Wav->BitsPerSample / 8);
    if (Wav->ByteRate != expectedByteRate)
    {
        Serial.print("Warning: ByteRate inconsistent with SampleRate/Channels/BitsPerSample");
    }
    return true;
}

void XT_Wav_Class::PrintData(const char *Data, uint8_t NumBytes)
{
    for (uint8_t i = 0; i < NumBytes; i++)
        Serial.print(Data[i]);
    Serial.println();
}

void XT_Wav_Class::DumpWAVHeader(WavHeader_Struct *Wav)
{
    if (memcmp(Wav->RIFFSectionID, "RIFF", 4) != 0)
    {
        Serial.print("Not a RIFF format file - ");
        PrintData(Wav->RIFFSectionID, 4);
        return;
    }
    if (memcmp(Wav->RiffFormat, "WAVE", 4) != 0)
    {
        Serial.print("Not a WAVE file - ");
        PrintData(Wav->RiffFormat, 4);
        return;
    }
    if (memcmp(Wav->FormatSectionID, "fmt", 3) != 0)
    {
        Serial.print("fmt ID not present - ");
        PrintData(Wav->FormatSectionID, 3);
        return;
    }
    if (memcmp(Wav->DataSectionID, "data", 4) != 0)
    {
        Serial.print("data ID not present - ");
        PrintData(Wav->DataSectionID, 4);
        return;
    }
    Serial.println();
    Serial.print("Total size :");
    Serial.println(Wav->Size);
    Serial.print("Format section size :");
    Serial.println(Wav->FormatSize);
    Serial.print("Wave format :");
    Serial.println(Wav->FormatID);
    Serial.print("Channels :");
    Serial.println(Wav->NumChannels);
    Serial.print("Sample Rate :");
    Serial.println(Wav->SampleRate);
    Serial.print("Byte Rate :");
    Serial.println(Wav->ByteRate);
    Serial.print("Block Align :");
    Serial.println(Wav->BlockAlign);
    Serial.print("Bits Per Sample :");
    Serial.println(Wav->BitsPerSample);
    Serial.print("Data Size :");
    Serial.println(Wav->DataSize);
}

bool XT_Wav_Class::OpenWavFile()
{
    WavFile = SD.open(FileName); // Open the wav file
    if (WavFile == false)
    {
        Serial.print("Could not open :");
        Serial.println(FileName);
        FileOK = false;
        return false;
    }
    else
    {
        int bytes = WavFile.read((byte *)&WavHeader, 44); // Read in the WAV header
        if (bytes != 44)
        {
            Serial.print("Failed to read WAV header, got bytes: ");
            Serial.println(bytes);
            WavFile.close();
            FileOK = false;
            return false;
        }
        FileOK = true;
        return true;
    }
}

void XT_Wav_Class::UnLoadWavFile()
{
    if (WavFile) WavFile.close();
}

void XT_Wav_Class::LoadWavFile()
{
    if (OpenWavFile())
    {
        if (ValidWavData(&WavHeader))
        {
            DumpWAVHeader(&WavHeader);
            Serial.println();
        }
        else
        {
            Serial.print("Ivalid Wave file header! Filename: ");
            Serial.println(FileName);
            // Mark file as invalid so playback won't attempt to use it
            FileOK = false;
            WavFile.close();
            return;
        }

        SampleRate = WavHeader.SampleRate;
        BytesPerSample = (WavHeader.BitsPerSample / 8) * WavHeader.NumChannels;
        NumChannels = WavHeader.NumChannels;
        DataSize = WavHeader.DataSize;
        IncreaseBy = float(SampleRate) / SAMPLES_PER_SEC;
        PlayingTime = (1000u * DataSize) / (uint32_t)(SampleRate * BytesPerSample);
        WavFile.seek(44); // Start of wav data
        TotalBytesRead = 0; // Clear to no bytes read in so far
        SamplesBufferIsEmpty = true;

        Speed = 1.0;
        Volume = 100;
    }
}

void XT_Wav_Class::ReadFile()
{
    if (TotalBytesRead >= DataSize)
    {
        LastNumBytesRead = 0;
        SamplesBufferIsEmpty = true;
        return;
    }

    if (TotalBytesRead + NUM_BYTES_TO_READ_FROM_FILE > DataSize)
        LastNumBytesRead = DataSize - TotalBytesRead;
    else
        LastNumBytesRead = NUM_BYTES_TO_READ_FROM_FILE;

    int bytes = WavFile.read(Samples, LastNumBytesRead);
    if (bytes <= 0)
    {
        Serial.print("ReadFile: read failed or EOF, bytes=");
        Serial.println(bytes);
        LastNumBytesRead = 0;
        SamplesBufferIsEmpty = true;
    }
    else
    {
        LastNumBytesRead = bytes;
        SamplesDataIdx = 0;
        TotalBytesRead += LastNumBytesRead;
        SamplesBufferIsEmpty = false;
    }
}

void XT_Wav_Class::Init()
{
    Serial.print("XT_Wav_Class::Init() called for: ");
    Serial.println(FileName);
    LastIntCount = 0;
    if (Speed >= 0 && FileOK)
    {
        TotalBytesRead = 0;
        WavFile.seek(44);
        ReadFile();
    }
    Count = 0;
    SpeedUpCount = 0;
    TimeElapsed = 0;
    TimeLeft = PlayingTime;
}

void XT_Wav_Class::NextSample(int16_t *Left, int16_t *Right)
{
    if (!FileOK || SamplesBufferIsEmpty)
    {
        *Left = 0;
        *Right = 0;
        return;
    }

    float step = IncreaseBy * Speed;
    Count += step;
    uint32_t advanceSamples = uint32_t(Count);
    if (advanceSamples > 0)
    {
        Count -= advanceSamples;
        SamplesDataIdx += size_t(advanceSamples) * BytesPerSample;
    }

    if (SamplesDataIdx >= LastNumBytesRead)
    {
        ReadFile();
        if (SamplesBufferIsEmpty)
        {
            *Left = 0;
            *Right = 0;
            Playing = false;
            TimeLeft = 0;
            return;
        }
    }

    if (NumChannels == 2)
    {
        *Left = (int16_t)((uint8_t)Samples[SamplesDataIdx] | ((int16_t)Samples[SamplesDataIdx + 1] << 8));
        *Right = (int16_t)((uint8_t)Samples[SamplesDataIdx + 2] | ((int16_t)Samples[SamplesDataIdx + 3] << 8));
    }
    else if (NumChannels == 1)
    {
        *Left = (int16_t)((uint8_t)Samples[SamplesDataIdx] | ((int16_t)Samples[SamplesDataIdx + 1] << 8));
        *Right = *Left;
    }

    SetVolume(Left, Right, Volume);

    if (TotalBytesRead >= DataSize)
    {
        Count = 0;
        SamplesDataIdx = 0;
        Playing = false;
        TimeLeft = 0;
        return;
    }

    uint32_t playedBytes = (TotalBytesRead - LastNumBytesRead) + SamplesDataIdx;
    TimeElapsed = (playedBytes * 1000u) / (SampleRate * BytesPerSample);
    TimeLeft = (PlayingTime > TimeElapsed) ? (PlayingTime - TimeElapsed) : 0;

    if (SamplesDataIdx + BytesPerSample >= LastNumBytesRead)
    {
        ReadFile();
    }
}

/**********************************************************************************************************************/
/* XT_I2S_Class                                                                                                       */
/**********************************************************************************************************************/

XT_I2S_Class::XT_I2S_Class(uint8_t LRCLKPin, uint8_t BCLKPin, uint8_t I2SOutPin, i2s_port_t PassedPortNum)
{
    // Set up I2S config structure
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = SAMPLES_PER_SEC;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 8;
    i2s_config.dma_buf_len = 256;
    i2s_config.use_apll = 0;
    i2s_config.tx_desc_auto_clear = true;
    i2s_config.fixed_mclk = -1;

    // Set up I2S pin config structure
    pin_config.bck_io_num = BCLKPin;
    pin_config.ws_io_num = LRCLKPin;
    pin_config.data_out_num = I2SOutPin;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;

    PortNum = PassedPortNum;

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(PortNum, &pin_config);

    // init ringbuffer synchronization primitives and start consumer task
		taskENTER_CRITICAL(&s_ringInitMux);
		if (ringMutex == nullptr) ringMutex = xSemaphoreCreateMutex();
		if (slotCountSem == nullptr) slotCountSem = xSemaphoreCreateCounting(RING_SLOTS, 0);
		if (freeSlotSem == nullptr) freeSlotSem = xSemaphoreCreateCounting(RING_SLOTS, RING_SLOTS);
		taskEXIT_CRITICAL(&s_ringInitMux);


    // Speichere Instanz-Pointer für Producer Task
    s_pI2SAudioInstance = this;

    if (i2sConsumerTaskHandle == nullptr)
    {
        // Stack size 4096 sollte angepasst werden falls nötig
        xTaskCreatePinnedToCore(
            [](void *param) {
                // Consumer task lambda wrapper
                size_t bytesWritten = 0;
                for (;;)
                {
                    // Warte auf belegten Slot
                    if (xSemaphoreTake(slotCountSem, portMAX_DELAY) == pdTRUE)
                    {
                        uint32_t idx;
                        // kurz kritischer Abschnitt um Tail zu holen und atomar zu inkrementieren
                        xSemaphoreTake(ringMutex, portMAX_DELAY);
                        idx = ringTail & (RING_SLOTS - 1);
                        // Note: Do NOT advance tail here; advance after successful write to I2S to avoid losing slot on failure
                        xSemaphoreGive(ringMutex);

                        // Schreib direkt aus ringBuffer[idx]
                        esp_err_t res = i2s_write(I2S_NUM_0, (const void*)ringBuffer[idx], SLOT_BYTES, &bytesWritten, pdMS_TO_TICKS(100));
                        if (res != ESP_OK || bytesWritten != SLOT_BYTES)
                        {
                            // Schreibfehler: logge und trotzdem geben wir Slot frei, damit Consumer kann fortfahren.
                            Serial.print("i2s_write failed or partial write, res=");
                            Serial.print((int)res);
                            Serial.print(" bytesWritten=");
                            Serial.println(bytesWritten);
                            // continue: free slot below
                        }

                        // Slot freigeben und Tail-Index jetzt atomar inkrementieren
                        xSemaphoreTake(ringMutex, portMAX_DELAY);
                        ringTail = (ringTail + 1) & (RING_SLOTS - 1);
                        xSemaphoreGive(ringMutex);

                        xSemaphoreGive(freeSlotSem);
                    }
                }
            },
            "I2SConsumer",
            4096,
            nullptr,
            2,
            &i2sConsumerTaskHandle,
            1);
    }

    // Producer Task: Füllt kontinuierlich den Ringbuffer
    if (i2sProducerTaskHandle == nullptr)
    {
        xTaskCreatePinnedToCore(
            [](void *param) {
                // Producer task - füllt kontinuierlich den Ringbuffer
                for (;;)
                {
                    if (s_pI2SAudioInstance != nullptr)
                    {
                        ((XT_I2S_Class*)s_pI2SAudioInstance)->FillBuffer();
                    }
                    // Yield kurz, damit Consumer Task auch ausführen kann
                    // 1ms delay für optimale Balance zwischen Producer und Consumer
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            },
            "I2SProducer",
            4096,
            nullptr,
            2,
            &i2sProducerTaskHandle,
            1);
    }
}

XT_I2S_Class::~XT_I2S_Class()
{
    // stop consumer task
    if (i2sConsumerTaskHandle != nullptr)
    {
        vTaskDelete(i2sConsumerTaskHandle);
        i2sConsumerTaskHandle = nullptr;
    }

    // stop producer task
    if (i2sProducerTaskHandle != nullptr)
    {
        vTaskDelete(i2sProducerTaskHandle);
        i2sProducerTaskHandle = nullptr;
    }

    if (ringMutex) { vSemaphoreDelete(ringMutex); ringMutex = nullptr; }
    if (slotCountSem) { vSemaphoreDelete(slotCountSem); slotCountSem = nullptr; }
    if (freeSlotSem) { vSemaphoreDelete(freeSlotSem); freeSlotSem = nullptr; }

    s_pI2SAudioInstance = nullptr;

    i2s_driver_uninstall(PortNum); // Free resources
}

/**********************************************************************************************************************/
/* Playlist / playback control (unmodified)                                                                            */
/**********************************************************************************************************************/

void XT_I2S_Class::Play(XT_PlayListItem_Class *Sound)
{
    Play(Sound, true, -1);
}

void XT_I2S_Class::Play(XT_PlayListItem_Class *Sound, bool Mix)
{
    Play(Sound, Mix, -1);
}

void XT_I2S_Class::Play(XT_PlayListItem_Class *Sound, bool Mix, int16_t TheVolume)
{
    Serial.print("Play() called for item: "); Serial.println((uintptr_t)Sound);
    // If this is a WAV item, print filename and FileOK
    XT_Wav_Class *wav = dynamic_cast<XT_Wav_Class *>(reinterpret_cast<XT_Wav_Class *>(Sound));
    if (wav != nullptr) {
        Serial.print("  WAV FileName: ");
        Serial.println(wav->FileName);
        Serial.print("  WAV FileOK: "); Serial.println(wav->FileOK);
    }

    if (AlreadyPlaying(Sound))
        RemoveFromPlayList(Sound);
    if (Mix == false)
        StopAllSounds();

    Sound->NewSound = true;
    Sound->RepeatIdx = Sound->Repeat;
    if (TheVolume >= 0)
        Sound->Volume = TheVolume;

    Sound->Init();

    if (FirstPlayListItem == nullptr)
    {
        FirstPlayListItem = Sound;
        LastPlayListItem = Sound;
    }
    else
    {
        LastPlayListItem->NextItem = Sound;
        Sound->PreviousItem = LastPlayListItem;
        LastPlayListItem = Sound;
    }
    Sound->Playing = true;
}

void XT_I2S_Class::RemoveFromPlayList(XT_PlayListItem_Class *ItemToRemove)
{
    if (ItemToRemove->PreviousItem != nullptr)
        ItemToRemove->PreviousItem->NextItem = ItemToRemove->NextItem;
    else
        FirstPlayListItem = ItemToRemove->NextItem;
    if (ItemToRemove->NextItem != nullptr)
        ItemToRemove->NextItem->PreviousItem = ItemToRemove->PreviousItem;
    else
        LastPlayListItem = ItemToRemove->PreviousItem;

    ItemToRemove->PreviousItem = nullptr;
    ItemToRemove->NextItem = nullptr;
}

bool XT_I2S_Class::AlreadyPlaying(XT_PlayListItem_Class *Item)
{
    XT_PlayListItem_Class *PlayItem;
    PlayItem = FirstPlayListItem;
    while (PlayItem != nullptr)
    {
        if (PlayItem == Item)
            return true;
        PlayItem = PlayItem->NextItem;
    }
    return false;
}

void XT_I2S_Class::StopAllSounds()
{
    XT_PlayListItem_Class *PlayItem;
    PlayItem = FirstPlayListItem;
    while (PlayItem != nullptr)
    {
        PlayItem->Playing = false;
        RemoveFromPlayList(PlayItem);
        PlayItem = FirstPlayListItem;
    }
    FirstPlayListItem = nullptr;
}

void XT_I2S_Class::Stop(XT_PlayListItem_Class *Sound)
{
    Sound->Playing = false;
    RemoveFromPlayList(Sound);
}

int16_t CheckTopBottomedOut(int32_t Sample)
{
    if (Sample > 32767)
        return 32767;
    if (Sample < -32768)
        return -32768;
    return Sample;
}

int16_t ApplyCompressor(int32_t sample, int threshold = 12000, float ratio = 4.0f) {
    int sign = (sample >= 0) ? 1 : -1;
    int absSample = std::abs(sample);

    if (absSample > threshold) {
        int excess = absSample - threshold;
        absSample = threshold + int(excess / ratio);
    }

    return int16_t(std::clamp(sign * absSample, -32768, 32767));
}

uint32_t XT_I2S_Class::MixSamples()
{
    XT_PlayListItem_Class *PlayItem, *NextPlayItem;
    uint32_t MixedSample;
    int16_t LeftSample, RightSample;
    int32_t LeftMix, RightMix;
		int activeSources = 0;

    PlayItem = FirstPlayListItem;
    LeftMix = 0;
    RightMix = 0;
    while (PlayItem != nullptr)
    {
        if (PlayItem->Playing)
        {
            PlayItem->NextSample(&LeftSample, &RightSample);
            if (PlayItem->Filter != 0)
                PlayItem->Filter->FilterWave(&LeftSample, &RightSample);
            SetVolume(&LeftSample, &RightSample, PlayItem->Volume);
            LeftMix += LeftSample;
            RightMix += RightSample;
						activeSources++;
        }

        NextPlayItem = PlayItem->NextItem;
        if (PlayItem->Playing == false)
        {
            if (PlayItem->RepeatForever)
            {
                PlayItem->Init();
                PlayItem->Playing = true;
            }
            else
            {
                if (PlayItem->RepeatIdx > 0)
                {
                    PlayItem->RepeatIdx--;
                    PlayItem->Init();
                    PlayItem->Playing = true;
                }
                else
                    RemoveFromPlayList(PlayItem);
            }
        }
        PlayItem = NextPlayItem;
    }

    // Skalierung bei mehreren Quellen
		//static float PrevMixFactor = 1.0f;
		//float TargetMixFactor = 1.0f / std::max(1, activeSources);
		//PrevMixFactor = PrevMixFactor * 0.9f + TargetMixFactor * 0.1f;

		//LeftMix *= PrevMixFactor;
		//RightMix *= PrevMixFactor;


    // Lautstärke auf Gesamtmix anwenden
    //int16_t Left = int16_t(LeftMix);
    //int16_t Right = int16_t(RightMix);
    //SetVolume(&Left, &Right, Volume);

    // Begrenzung
    //LeftMix = uint16_t(CheckTopBottomedOut(Left));
    //RightMix = uint16_t(CheckTopBottomedOut(Right));
		LeftMix = ApplyCompressor(LeftMix);
   	RightMix = ApplyCompressor(RightMix);


    MixedSample = (LeftMix << 16) | (RightMix);
    return MixedSample;
}


/**********************************************************************************************************************/
/* Ringbuffer helper functions                                                                                         */
/**********************************************************************************************************************/

// Schreibe kompletten Slot (bytes == SLOT_BYTES) in Ring.
// waitTicks == 0 -> non-blocking, sofort false wenn kein freier Slot
bool RingWriteSlot(const void *src, size_t bytes, TickType_t waitTicks = pdMS_TO_TICKS(50))
{
    if (bytes != SLOT_BYTES) return false;
    // try to take a free slot
    if (waitTicks == 0)
    {
        if (xSemaphoreTake(freeSlotSem, 0) != pdTRUE) return false;
    }
    else
    {
        if (xSemaphoreTake(freeSlotSem, waitTicks) != pdTRUE) return false;
    }

    // reserve index but DO NOT advance head until copy finished
    uint32_t idx;
    xSemaphoreTake(ringMutex, portMAX_DELAY);
    idx = ringHead & (RING_SLOTS - 1);
    // temporarily mark slot reserved by incrementing a local headReserve variable (we'll commit after memcpy)
    uint32_t nextHead = (ringHead + 1) & (RING_SLOTS - 1);
    // do not write ringHead yet, to avoid marking slot as available for consumer before data copied
    xSemaphoreGive(ringMutex);

    // copy data into reserved slot
    memcpy((void*)ringBuffer[idx], src, SLOT_BYTES);

    // commit the head index so consumer can see the slot
    xSemaphoreTake(ringMutex, portMAX_DELAY);
    ringHead = nextHead;
    xSemaphoreGive(ringMutex);

    xSemaphoreGive(slotCountSem);
    return true;
}

/**********************************************************************************************************************/
/* FillBuffer: Producer-seite (schreibt einen Slot in den Ring)                                                       */
/* Hinweis: Fülle häufiger oder starte einen Producer-Task, falls kontinuierliche Produktion nötig                     */
/**********************************************************************************************************************/
void XT_I2S_Class::FillBuffer()
{
    uint32_t tmpSlot[SLOT_WORDS];

    for (size_t i = 0; i < SLOT_WORDS; ++i)
    {
        tmpSlot[i] = MixSamples();
    }

    // Schreibe kompletten Slot (blockierend mit Timeout)
    bool ok = RingWriteSlot(tmpSlot, SLOT_BYTES, pdMS_TO_TICKS(100));
    if (!ok)
    {
        // Kein freier Slot -> Unterlauf/Verwerfen. Optional: Statistik/Logging.
        // Aufzeichnen eines Counters wäre hier hilfreich.
        Serial.println("FillBuffer: no free slot, dropping buffer");
    }
}