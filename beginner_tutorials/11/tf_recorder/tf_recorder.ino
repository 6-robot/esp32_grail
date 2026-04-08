#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <driver/i2s.h>

static int SD_CS_Pin =    16;
static int SD_MOSI_Pin =  42;
static int SD_MISO_Pin =  40;
static int SD_SCK_Pin =   41;
SPIClass SD_SPI(HSPI);

static int INMP441_WS_Pin =     18;
static int INMP441_SCK_Pin =    17;
static int INMP441_SD_Pin =     8;
#define SAMPLE_RATE 44100

static int Green_Btn_Pin = 11;
static int Green_LED_Pin = 47;

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

File recordFile;
wavStruct wavHeader;
bool isRecording = false;

i2s_config_t i2sIn_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 1024
};

const i2s_pin_config_t i2sIn_pin_config = {
  .bck_io_num = INMP441_SCK_Pin,
  .ws_io_num = INMP441_WS_Pin,
  .data_out_num = -1,
  .data_in_num = INMP441_SD_Pin
};

void setup() {
  pinMode(Green_Btn_Pin, INPUT_PULLUP);
  pinMode(Green_LED_Pin, OUTPUT);
  digitalWrite(Green_LED_Pin, HIGH);

  SD_SPI.begin(SD_SCK_Pin, SD_MISO_Pin, SD_MOSI_Pin, SD_CS_Pin);
  if(SD.begin(SD_CS_Pin, SD_SPI) == false) {
      digitalWrite(Green_LED_Pin, LOW);
      return;
  }

  i2s_driver_install(I2S_NUM_0, &i2sIn_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &i2sIn_pin_config);
}

void loop() {
  if (digitalRead(Green_Btn_Pin) == LOW) 
  {
    digitalWrite(Green_LED_Pin, LOW);

    if(isRecording == false) {
      recordFile = SD.open("/record.wav", FILE_WRITE);
      if(!recordFile) {
          return;
      }
      recordFile.write((uint8_t*)&wavHeader, sizeof(wavHeader));
      isRecording = true;
    }
    esp_err_t result;
    size_t bytes_read = 0;
    int16_t data[1024];
    result = i2s_read(I2S_NUM_0, &data, sizeof(data), &bytes_read, portMAX_DELAY);
    if(result == ESP_OK && bytes_read > 0) {
      recordFile.write((uint8_t*)data, bytes_read);
      wavHeader.chunkSize += bytes_read;
      wavHeader.subchunk2Size += bytes_read;
    }
  }
  else
  {
    digitalWrite(Green_LED_Pin, HIGH);
    if(recordFile) {
        recordFile.seek(0);
        recordFile.write((uint8_t*)&wavHeader, sizeof(wavHeader));
        recordFile.close();
        wavHeader.chunkSize = 36;
        wavHeader.subchunk2Size = 0;
    }
    isRecording = false;
    delay(300);
  }
}