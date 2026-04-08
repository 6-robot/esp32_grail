void setup() {
  pinMode(47, OUTPUT);
}

void loop() {
  digitalWrite(47, LOW);    // LED 亮
  delay(1000);               // 延时1秒
  digitalWrite(47, HIGH);   // LED 熄灭
  delay(1000);               // 延时1秒
}
