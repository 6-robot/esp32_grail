#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI(320,480);

int circle_x = 240;
int circle_y = 160;
int prev_x = 240;    // 上一个位置的坐标
int prev_y = 160;

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_RED);

  pinMode(1, INPUT);
  pinMode(2, INPUT);
}

void loop() {
  // 保存当前位置为上一个位置
  prev_x = circle_x;
  prev_y = circle_y;
  
  int x_adc = analogRead(1);
  int y_adc = analogRead(2);

  // 根据摇杆值移动圆点
  if(x_adc > 3000) circle_x+=10;
  if(x_adc < 1000) circle_x-=10;
  if(y_adc > 3000) circle_y+=10;
  if(y_adc < 1000) circle_y-=10;
  
  // 限制圆点在屏幕范围内
  circle_x = constrain(circle_x, 0, 480);
  circle_y = constrain(circle_y, 0, 320);
  
  // 只清除上一个圆点区域
  tft.fillCircle(prev_x, prev_y, 10, TFT_RED);
  
  // 绘制新的黑色圆点
  tft.fillCircle(circle_x, circle_y, 10, TFT_BLACK);

  delay(30);
}