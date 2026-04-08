#include <TFT_eSPI.h>
#include <Wire.h>
#include <MPU6050_tockn.h>

// 创建TFT显示对象，设置分辨率为320x480
TFT_eSPI tft = TFT_eSPI(320, 480);

// 定义I2C的SDA和SCL引脚
#define I2C_SDA 9
#define I2C_SCL 10

// 创建I2C对象
TwoWire I2C_0 = TwoWire(0);
// 创建MPU6050对象，使用I2C_0
MPU6050 mpu6050(I2C_0);

void setup() {
  // 初始化TFT显示屏
  tft.init();
  // 设置显示屏的旋转方向
  tft.setRotation(1);
  // 反转显示屏颜色
  tft.invertDisplay(1);
  // 设置文本大小
  tft.setTextSize(2);
  // 填充屏幕为黑色
  tft.fillScreen(TFT_BLACK);

  // 设置光标位置并打印初始化信息
  tft.setCursor(0, 0); 
  tft.println("MPU6050 Initializing...");
  // 开始I2C通信
  I2C_0.begin(I2C_SDA, I2C_SCL);
  // 初始化MPU6050传感器
  mpu6050.begin();
  
  // 打印偏移量计算提示
  tft.println("Calculating offsets...");
  tft.println("DO NOT MOVE MPU6050...");
  // 计算陀螺仪偏移量
  mpu6050.calcGyroOffsets(true);
  // 打印完成信息
  tft.println("Done!");
  delay(100);
  // 清空屏幕
  tft.fillScreen(TFT_BLACK);
}

void loop() {
  // 更新传感器数据
  mpu6050.update();
  // 清除之前显示的姿态角度信息
  tft.fillRect(80, 20, 80, 60, TFT_BLACK);
  // 设置光标位置并打印姿态角度信息
  tft.setCursor(0, 0);
  tft.println("Angle (deg):");
  tft.setCursor(0, 20);
  tft.print("Roll  : "); 
  tft.println(mpu6050.getAngleX(), 1); // 打印横滚角度
  tft.print("Pitch : "); 
  tft.println(mpu6050.getAngleY(), 1); // 打印俯仰角度
  tft.print("Yaw   : "); 
  tft.println(mpu6050.getAngleZ(), 1); // 打印航向角度

  // 设置光标位置并打印加速度计数据
  tft.fillRect(20, 120, 100, 60, TFT_BLACK);
  tft.setCursor(0, 100);
  tft.println("Accel (g):"); // 打印加速度计标题
  tft.setCursor(0, 120);
  tft.print("X: "); 
  tft.println(mpu6050.getAccX(), 2); // 打印X轴加速度
  tft.print("Y: "); 
  tft.println(mpu6050.getAccY(), 2); // 打印Y轴加速度
  tft.print("Z: "); 
  tft.println(mpu6050.getAccZ(), 2); // 打印Z轴加速度
  
  // 设置光标位置并打印陀螺仪数据
  tft.fillRect(20, 220, 100, 60, TFT_BLACK);
  tft.setCursor(0, 200);
  tft.println("Gyro (deg/s):"); // 打印陀螺仪标题
  tft.setCursor(0, 220);
  tft.print("X: "); 
  tft.println(mpu6050.getGyroX(), 1); // 打印X轴角速度
  tft.print("Y: "); 
  tft.println(mpu6050.getGyroY(), 1); // 打印Y轴角速度
  tft.print("Z: "); 
  tft.println(mpu6050.getGyroZ(), 1); // 打印Z轴角速度
  
  // 设置光标位置并打印温度数据
  tft.setCursor(0, 290);
  tft.print("Temperature: "); 
  tft.print(mpu6050.getTemp(), 1); // 打印温度
  tft.println(" C"); // 添加单位

  // 延迟30毫秒用于更新数据
  delay(30);
}
