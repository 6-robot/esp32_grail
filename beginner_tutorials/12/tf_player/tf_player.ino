#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <driver/i2s.h>

static int SD_CS_Pin =    16;
static int SD_MOSI_Pin =  42;
static int SD_MISO_Pin =  40;
static int SD_SCK_Pin =   41;
SPIClass SD_SPI(HSPI);

static int MAX98357_LRC_Pin =   13;
static int MAX98357_BCLK_PIn =  14;
static int MAX98357_DIN_Pin =   4;
#define SAMPLE_RATE 44100

static int Blue_Btn_Pin = 12;
static int Blue_LED_Pin = 48;

struct wavStruct {
    const char chunkID[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunkSize = 36;
    const char format[4] = {'W', 'A', 'V', 'E'};
    const char subchunk1ID[4] = {'f', 'm', 't', ' '};
    const uint32_t subchunk1Size = 16;
    const uint16_t audioFormat = 1;
    const uint16_t numChannels = 1;
    const uint32_t sampleRate = SAMPLE_RATE;
    const uint32_t byteRate = 32000;
    const uint16_t blockAlign = 2;
    const uint16_t bitsPerSample = 16;
    const char subchunk2ID[4] = {'d', 'a', 't', 'a'};
    uint32_t subchunk2Size = 0;
};

File playFile;
wavStruct wavHeader;

i2s_config_t i2sOut_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 1024
};

const i2s_pin_config_t i2sOut_pin_config = {
  .bck_io_num = MAX98357_BCLK_PIn,
  .ws_io_num = MAX98357_LRC_Pin,
  .data_out_num = MAX98357_DIN_Pin,
  .data_in_num = -1
};

void setup() {
  pinMode(Blue_Btn_Pin, INPUT_PULLUP);
  pinMode(Blue_LED_Pin, OUTPUT);
  digitalWrite(Blue_LED_Pin, HIGH);

  SD_SPI.begin(SD_SCK_Pin, SD_MISO_Pin, SD_MOSI_Pin, SD_CS_Pin);
  if(SD.begin(SD_CS_Pin, SD_SPI) == false) {
      digitalWrite(Blue_LED_Pin, LOW);
      return;
  }

  i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);
}

void loop() {
  size_t bytes_read;
  if (digitalRead(Blue_Btn_Pin) == LOW)
  {
    playFile = SD.open("/record.wav");
    if (playFile) { 
      digitalWrite(Blue_LED_Pin, LOW);
      playFile.readBytes((char*)&wavHeader, sizeof(wavHeader));
      size_t bytesToRead = wavHeader.subchunk2Size;
      int16_t data[1024];
      while (bytesToRead > 0) {
        size_t bytesToPlay = 1024;
        if(bytesToRead < 1024)
          bytesToPlay = bytesToRead;
        size_t bytesRead = playFile.read((uint8_t*)data, bytesToPlay);
        for (size_t i = 0; i < bytesRead / sizeof(int16_t); i++) {
            data[i] *= 10;
        }
        i2s_write(I2S_NUM_1, data, bytesToPlay, &bytes_read, portMAX_DELAY);
        bytesToRead -= bytesRead;
      }
      i2s_zero_dma_buffer(I2S_NUM_1);
      playFile.close();
    }
    digitalWrite(Blue_LED_Pin, HIGH);
  }
  
  delay(10);
}