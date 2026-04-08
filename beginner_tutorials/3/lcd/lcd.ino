#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI(320,480);

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_RED);
}

void loop() {
  tft.setCursor(0, 0, 2);
  tft.setTextSize(2);
	tft.println("Hello World!");
}