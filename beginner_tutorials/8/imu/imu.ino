#include <TFT_eSPI.h>
#include <Wire.h>
#include <MPU6050_tockn.h>

TFT_eSPI tft = TFT_eSPI(320,480);

#define I2C_SDA 9
#define I2C_SCL 10

TwoWire I2C_0 = TwoWire(0);
MPU6050 mpu6050(I2C_0);

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);

  tft.setCursor(0, 0); 
  tft.println("MPU6050 Initializing...");
  I2C_0.begin(I2C_SDA, I2C_SCL);
  mpu6050.begin();
  
  tft.println("Calculating offsets...");
  tft.println("DO NOT MOVE MPU6050...");
  mpu6050.calcGyroOffsets(true);
  
  tft.println("Done!");
  tft.fillScreen(TFT_BLACK);
}

void loop() {
  mpu6050.update();
  
  tft.fillRect(80, 20, 80, 60, TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println("Angle (deg):");
  tft.setCursor(0, 20);
  tft.print("Roll  : "); 
  tft.println(mpu6050.getAngleX(), 1);
  tft.print("Pitch : "); 
  tft.println(mpu6050.getAngleY(), 1);
  tft.print("Yaw   : "); 
  tft.println(mpu6050.getAngleZ(), 1);
  
  delay(100);
}