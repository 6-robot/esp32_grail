// 电机PWM信号引脚
const int motor_pin = 45;
// 频率15kHz，8位分辨率
const int frequency = 15000;
const int resolution = 8;
const int motor_ch = 0;
// 摇杆X轴的ADC输入引脚
const int x_pin = 1;

void setup() 
{
  // 配置电机控制PWM通道：频率15kHz，8位分辨率
  ledcSetup(motor_ch, frequency, resolution);
  ledcAttachPin(motor_pin, motor_ch);
  // 设置模拟输入引脚
  pinMode(x_pin, INPUT);
}

void loop() 
{
  // 读取模拟输入值(范围0-4095)
  int x_value = analogRead(x_pin);
  
  // 将模拟输入值映射到PWM范围(0-255)
  int pos = x_value*255/4095;
  
  // 计算PWM占空比：
  // pos*2得到范围(0-510)
  // 减去255得到范围(-255到+255)
  int duty = pos*2 - 255;

  if( duty > 0 )  // 正值开始转动
  {
    ledcWrite(motor_ch, duty);    // 电机通道输出对应占空比
  }
  else  // 非正值表示停止
  {
    ledcWrite(motor_ch, 0);       // 电机通道停止
  }
  delay(100);
}
