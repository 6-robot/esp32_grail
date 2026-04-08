#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI(320,480);
U8g2_for_TFT_eSPI u8g2;

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_RED);

  u8g2.begin(tft);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(TFT_WHITE);
  u8g2.setBackgroundColor(TFT_RED);
}

void loop() {
  u8g2.setCursor(0, 16);
  u8g2.print("世界你好！");
}