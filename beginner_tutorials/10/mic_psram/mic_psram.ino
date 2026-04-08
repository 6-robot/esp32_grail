#include <driver/i2s.h>
#include <esp_system.h>

static int INMP441_WS_Pin =     18;
static int INMP441_SCK_Pin =    17;
static int INMP441_SD_Pin =     8;
static int MAX98357_LRC_Pin =   13;
static int MAX98357_BCLK_PIn =  14;
static int MAX98357_DIN_Pin =   4;
#define SAMPLE_RATE 44100

static int Green_Btn_Pin = 11;
static int Green_LED_Pin = 47;
static int Blue_Btn_Pin = 12;
static int Blue_LED_Pin = 48;

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

int16_t* data;
int data_length = SAMPLE_RATE * 60; // 录制60秒音频
void setup() {
  i2s_driver_install(I2S_NUM_0, &i2sIn_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &i2sIn_pin_config);
  i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);

  pinMode(Blue_Btn_Pin, INPUT_PULLUP);
  pinMode(Blue_LED_Pin, OUTPUT);
  digitalWrite(Blue_LED_Pin, HIGH);
  pinMode(Green_Btn_Pin, INPUT_PULLUP);
  pinMode(Green_LED_Pin, OUTPUT);
  digitalWrite(Green_LED_Pin, HIGH);

  Serial.begin(115200);
  size_t psramSize = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  Serial.printf("可用PSRAM大小: %d bytes\n", psramSize);

  // 从PSRAM中申请内存建立数组
  data = (int16_t*)(ps_malloc(data_length * sizeof(int16_t)));
  if (data == NULL) {
      digitalWrite(Blue_LED_Pin, LOW); // 内存申请失败的话就亮蓝灯
  }
}

void loop() {
  size_t bytes_read;
  if (digitalRead(Green_Btn_Pin) == LOW) 
  {
    digitalWrite(Green_LED_Pin, LOW);
    i2s_read(I2S_NUM_0, data, data_length, &bytes_read, portMAX_DELAY);
    for (int i = 0; i < data_length; i++) {
      data[i] *= 10;
    }
    digitalWrite(Green_LED_Pin, HIGH);
  }

  if (digitalRead(Blue_Btn_Pin) == LOW)  
  {
    digitalWrite(Blue_LED_Pin, LOW);
    i2s_write(I2S_NUM_1, data, data_length, &bytes_read, portMAX_DELAY);
    i2s_zero_dma_buffer(I2S_NUM_1);
    digitalWrite(Blue_LED_Pin, HIGH);
  }
  
  delay(10);
}