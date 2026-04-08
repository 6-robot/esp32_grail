// 定义舵机控制引脚和通道
const int servo_pin = 21;  // 舵机PWM信号输出引脚
const int servo_ch = 0;    // 使用ESP32的PWM通道0

// 定义摇杆X轴输入引脚
const int x_pin = 1;       

void setup() 
{
  // 配置PWM通道
  // 参数: 通道号, 频率50Hz, 分辨率12位(0-4095)
  ledcSetup(servo_ch, 50, 12);
  
  // 将PWM通道绑定到输出引脚 GPIO21
  ledcAttachPin(servo_pin, servo_ch);
  
  // 设置摇杆X轴引脚为输入模式
  pinMode(x_pin, INPUT);
}

void loop() 
{
  // 读取摇杆X轴的模拟数值(0-4095)
  int x_adc = analogRead(x_pin);
  
  // 计算舵机PWM信号的最小宽度(0.5ms对应的计数值)
  int min_width = (0.5 * 4096) / 20;
  
  // 计算舵机PWM信号的最大宽度(2.5ms对应的计数值)
  int max_width = (2.5 * 4096) / 20;
  
  // 计算角度到PWM计数值的转换系数
  double k = (max_width - min_width)/180.0;
  
  // 将摇杆的ADC值映射到舵机角度范围，并转换为对应的PWM计数值
  int duty = x_adc * 180 * k / 4095 + min_width;
  
  // 输出PWM信号控制舵机
  ledcWrite(servo_ch, duty);
  
  // 延时100ms，控制更新频率
  delay(100);
}
