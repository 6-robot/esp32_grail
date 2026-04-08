const int motor_pin = 45;

const int frequency = 15000;
const int resolution = 8;
const int motor_ch = 0;

void setup() 
{ 
  ledcSetup(motor_ch, frequency, resolution);
  ledcAttachPin(motor_pin, motor_ch);
}

void loop() 
{
    ledcWrite(motor_ch, 150);
    delay(3000);
    ledcWrite(motor_ch, 0);
    delay(2000);
}
