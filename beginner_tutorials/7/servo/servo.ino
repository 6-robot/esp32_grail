const int servo_pin = 21;
const int servo_ch = 0;

void setup() 
{
  ledcSetup(servo_ch, 50, 12);
  ledcAttachPin(servo_pin, servo_ch);
}

void loop() 
{
  int min_duty = (0.5 * 4096) / 20;
  int mid_duty = (1.5 * 4096) / 20;
  int max_duty = (2.5 * 4096) / 20;
  ledcWrite(servo_ch, min_duty);
  delay(3000);
  ledcWrite(servo_ch, mid_duty);
  delay(3000);
  ledcWrite(servo_ch, max_duty);
  delay(3000);
}