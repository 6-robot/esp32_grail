#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#include <driver/i2s.h>
#include "xfyun_helper.h"

const char* ssid = "xxxx";
const char* password = "xxxx";

const char* IAT_APPID = "xxxx";
const char* IAT_APISecret = "xxxx";
const char* IAT_APIKey = "xxxx";

TFT_eSPI tft = TFT_eSPI(320,480);
U8g2_for_TFT_eSPI u8g2;

static int INMP441_WS_Pin =     18;
static int INMP441_SCK_Pin =    17;
static int INMP441_SD_Pin =     8;
static int MAX98357_LRC_Pin =   13;
static int MAX98357_BCLK_PIn =  14;
static int MAX98357_DIN_Pin =   4;
#define SAMPLE_RATE 16000

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

int16_t* record_data;
int data_length = SAMPLE_RATE * 5;
void setup() {
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);

  u8g2.begin(tft);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(TFT_WHITE);
  u8g2.setBackgroundColor(TFT_BLACK);

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

  record_data = (int16_t*)(ps_malloc(data_length * sizeof(int16_t)));
  if (record_data == NULL) {
    digitalWrite(Blue_LED_Pin, LOW);
    return;
  }

  u8g2.setCursor(0, 16);
  u8g2.print("连接 WiFi ");
  u8g2.print(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    u8g2.print(".");
  }
  u8g2.print("\n成功连上 WiFi！\n请按绿色按钮开始录音。");
 
  xfyun_init();
}

void loop() {
  size_t bytes_read;
  if (digitalRead(Green_Btn_Pin) == LOW) 
  {
    digitalWrite(Green_LED_Pin, LOW);
    delay(300);
    tft.fillScreen(TFT_BLACK);
    u8g2.setCursor(0, 16);
    u8g2.print("开始录音...");
    i2s_read(I2S_NUM_0, record_data, data_length * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    u8g2.setCursor(0, 32);
    u8g2.print("录音完毕,开始识别...\n");
    digitalWrite(Green_LED_Pin, HIGH);
    
    xfyun_iat(record_data, data_length ,IAT_APPID, IAT_APISecret ,IAT_APIKey);
  }

  String result = xfyun_get_result();
  if (result.length() > 0) {
    u8g2.print(result);
  }

  if (digitalRead(Blue_Btn_Pin) == LOW)  
  {
    digitalWrite(Blue_LED_Pin, LOW);
    i2s_write(I2S_NUM_1, record_data, data_length * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    i2s_zero_dma_buffer(I2S_NUM_1);
    digitalWrite(Blue_LED_Pin, HIGH);
  }
  
  delay(10);
}