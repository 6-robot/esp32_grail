void setup() {
  pinMode(11, INPUT);
  pinMode(47, OUTPUT);
}

void loop() {
  int state = digitalRead(11); 
  digitalWrite(47, state);
}
