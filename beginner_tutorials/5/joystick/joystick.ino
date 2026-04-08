#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI(320,480);

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.setTextSize(3);
  tft.fillScreen(TFT_RED);

  pinMode(1, INPUT);
  pinMode(2, INPUT);
}

char text[20];
void loop() {
  int x_adc = analogRead(1);
  int y_adc = analogRead(2);

  sprintf(text, "x_adc = %04d", x_adc);
  tft.setCursor(0, 0);
  tft.print(text);
  
  sprintf(text, "y_adc = %04d", y_adc);
  tft.setCursor(0, 30);
  tft.print(text);
}