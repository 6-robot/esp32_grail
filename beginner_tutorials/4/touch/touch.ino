#include <TAMC_GT911.h>
#include <TFT_eSPI.h>

#define IIC_SDA  9
#define IIC_SCL 10
#define TP_INT  39
#define TP_RST  38

TAMC_GT911 tp(IIC_SDA, IIC_SCL, TP_INT, TP_RST, 480, 320);
TFT_eSPI tft = TFT_eSPI(320, 480);

void setup() {
  tp.begin();
  tp.setRotation(ROTATION_RIGHT);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.fillScreen(TFT_RED);
}

void loop() {
  tp.read();
  if (tp.isTouched)
  {
    tft.setCursor(0, 0);
    for (int i=0; i<tp.touches; i++)
    {
      tft.printf("Touch[%d]:(%d,%d) Size:%d\n", 
              i+1, tp.points[i].x, tp.points[i].y, tp.points[i].size);

      int tp_r = tp.points[i].size/2;
      tft.fillCircle(tp.points[i].x, tp.points[i].y, tp_r, TFT_GREEN);
    }
  }
  delay(30);
}