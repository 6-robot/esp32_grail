void setup() {
  pinMode(12, INPUT);
  pinMode(48, OUTPUT);
  pinMode(11, INPUT);
  pinMode(47, OUTPUT);
}

void loop() {
  int state = digitalRead(12);
  digitalWrite(48, state);
  
  int state2 = digitalRead(11);
  digitalWrite(47, state2);
}
