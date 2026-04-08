
#include <TFT_eSPI.h>

#define JOYSTICK_X_PIN    1
#define JOYSTICK_Y_PIN    2
#define BUTTON_PIN        11
#define LED_PIN           47

#define SCREEN_WIDTH      480
#define SCREEN_HEIGHT     320

#define BIRD_WIDTH        24
#define BIRD_HEIGHT       20
#define BIRD_X            80

#define PIPE_WIDTH        50
#define PIPE_GAP          100
#define PIPE_SPEED        6
#define PIPE_COUNT        3
#define PIPE_SPACING      180

#define GRAVITY           0.6f
#define FLAP_STRENGTH     -9.0f
#define MAX_FALL_SPEED    12.0f

#define GROUND_HEIGHT     40

#define JOYSTICK_CENTER   512
#define JOYSTICK_THRESHOLD 200

#define COLOR_SKY         0x7ECE
#define COLOR_GROUND      0xC582
#define COLOR_GRASS       0x2E05
#define COLOR_BIRD_BODY   0xFDE0
#define COLOR_BIRD_WING   0xFC00
#define COLOR_BIRD_BEAK   0xFD00
#define COLOR_BIRD_EYE    TFT_WHITE
#define COLOR_PIPE        0x2E05
#define COLOR_PIPE_DARK   0x1C02
#define COLOR_TEXT        TFT_WHITE
#define COLOR_SCORE       TFT_YELLOW

enum GameState {
  STATE_MENU,
  STATE_READY,
  STATE_PLAYING,
  STATE_GAME_OVER
};

struct Pipe {
  int x;
  int gapY;
  bool passed;
  bool active;
};

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

float birdY;
float birdVelocity;
int birdFrame = 0;

Pipe pipes[PIPE_COUNT];

GameState gameState = STATE_MENU;
int score = 0;
int highScore = 0;

bool lastButtonState = HIGH;
bool lastFlapState = false;
bool flapPressed = false;

unsigned long lastUpdateTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastAnimTime = 0;
bool ledState = false;

float floatOffset = 0;
float floatDir = 1;

void initGame();
void handleInput();
void updateGame();
void drawGame();

void drawBird();
void drawPipes();
void drawGround();
void drawScore();
void drawMenu();
void drawReady();
void drawGameOver();
void flap();
void resetPipes();
bool checkCollision();
void blinkLED(int interval);
void updateFloating();

void setup() {
  Serial.begin(115200);

  pinMode(JOYSTICK_X_PIN, INPUT);
  pinMode(JOYSTICK_Y_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(COLOR_SKY);

  if (spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT) == nullptr) {
    Serial.println("Sprite创建失败！内存不足！尝试减小尺寸或色深。");
  } else {
    Serial.println("Sprite创建成功！");
    spr.setSwapBytes(true);
  }

  randomSeed(analogRead(0));

  drawMenu();
  Serial.println("ESP32 Flappy Bird 优化版启动");
}

void loop() {
  handleInput();

  unsigned long currentTime = millis();

  switch (gameState) {
    case STATE_MENU:
      blinkLED(500);
      updateFloating();
      if (currentTime - lastUpdateTime >= 30) {
        lastUpdateTime = currentTime;
        drawMenu();
      }
      if (flapPressed) {
        flapPressed = false;
        gameState = STATE_READY;
        birdY = SCREEN_HEIGHT / 2;
      }
      break;

    case STATE_READY:
      blinkLED(300);
      updateFloating();
      if (currentTime - lastUpdateTime >= 30) {
        lastUpdateTime = currentTime;
        drawGame();
        drawReady();
      }
      if (flapPressed) {
        flapPressed = false;
        initGame();
        gameState = STATE_PLAYING;
        flap();
      }
      break;

    case STATE_PLAYING:
      digitalWrite(LED_PIN, HIGH);
      if (currentTime - lastUpdateTime >= 20) {
        lastUpdateTime = currentTime;
        updateGame();
        drawGame();
      }
      break;

    case STATE_GAME_OVER:
      blinkLED(100);

      if (flapPressed) {
        flapPressed = false;
        gameState = STATE_READY;
        birdY = SCREEN_HEIGHT / 2;

      }
      break;
  }
}

void updateFloating() {
  floatOffset += floatDir * 0.5f;
  if (floatOffset > 8) floatDir = -1;
  if (floatOffset < -8) floatDir = 1;
}

void blinkLED(int interval) {
  unsigned long currentTime = millis();
  if (currentTime - lastBlinkTime >= interval) {
    lastBlinkTime = currentTime;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}

void initGame() {
  birdY = SCREEN_HEIGHT / 2;
  birdVelocity = 0;
  birdFrame = 0;
  score = 0;
  resetPipes();
  lastUpdateTime = millis();
  Serial.println("游戏开始！");
}

void resetPipes() {
  for (int i = 0; i < PIPE_COUNT; i++) {
    pipes[i].x = SCREEN_WIDTH + i * PIPE_SPACING;
    pipes[i].gapY = random(PIPE_GAP / 2 + 30, SCREEN_HEIGHT - GROUND_HEIGHT - PIPE_GAP / 2 - 30);
    pipes[i].passed = false;
    pipes[i].active = true;
  }
}

void handleInput() {
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    flapPressed = true;
  }
  lastButtonState = currentButtonState;

  int y_adc = analogRead(JOYSTICK_Y_PIN);
  bool currentFlapState = (y_adc < JOYSTICK_CENTER - JOYSTICK_THRESHOLD);
  if (!lastFlapState && currentFlapState) {
    flapPressed = true;
  }
  lastFlapState = currentFlapState;

  if (gameState == STATE_PLAYING && flapPressed) {
    flapPressed = false;
    flap();
  }
}

void flap() {
  birdVelocity = FLAP_STRENGTH;
}

void updateGame() {
  if (millis() - lastAnimTime >= 100) {
    lastAnimTime = millis();
    birdFrame = (birdFrame + 1) % 3;
  }

  birdVelocity += GRAVITY;
  if (birdVelocity > MAX_FALL_SPEED) birdVelocity = MAX_FALL_SPEED;

  birdY += birdVelocity;
  if (birdY < 0) { birdY = 0; birdVelocity = 0; }

  for (int i = 0; i < PIPE_COUNT; i++) {
    if (pipes[i].active) {
      pipes[i].x -= PIPE_SPEED;

      if (!pipes[i].passed && pipes[i].x + PIPE_WIDTH < BIRD_X) {
        pipes[i].passed = true;
        score++;
      }

      if (pipes[i].x + PIPE_WIDTH < 0) {
        int maxX = 0;
        for (int j = 0; j < PIPE_COUNT; j++) {
          if (pipes[j].x > maxX) maxX = pipes[j].x;
        }
        pipes[i].x = maxX + PIPE_SPACING;
        pipes[i].gapY = random(PIPE_GAP / 2 + 30, SCREEN_HEIGHT - GROUND_HEIGHT - PIPE_GAP / 2 - 30);
        pipes[i].passed = false;
      }
    }
  }

  if (checkCollision()) {
    gameState = STATE_GAME_OVER;
    if (score > highScore) highScore = score;
    drawGameOver();
  }
}

bool checkCollision() {
  if (birdY + BIRD_HEIGHT > SCREEN_HEIGHT - GROUND_HEIGHT) return true;

  for (int i = 0; i < PIPE_COUNT; i++) {
    if (pipes[i].active) {
      int birdLeft = BIRD_X;
      int birdRight = BIRD_X + BIRD_WIDTH;
      int birdTop = (int)birdY + 2;
      int birdBottom = (int)birdY + BIRD_HEIGHT - 2;

      int pipeLeft = pipes[i].x;
      int pipeRight = pipes[i].x + PIPE_WIDTH;
      int gapTop = pipes[i].gapY - PIPE_GAP / 2;
      int gapBottom = pipes[i].gapY + PIPE_GAP / 2;

      if (birdRight > pipeLeft && birdLeft < pipeRight) {
        if (birdTop < gapTop || birdBottom > gapBottom) return true;
      }
    }
  }
  return false;
}

void drawGame() {

  spr.fillSprite(COLOR_SKY);

  drawPipes();
  drawBird();
  drawGround();
  drawScore();

  spr.pushSprite(0, 0);
}

void drawBird() {
  int x = BIRD_X;
  int y;

  if (gameState == STATE_MENU || gameState == STATE_READY) {
    y = SCREEN_HEIGHT / 2 + (int)floatOffset;
  } else {
    y = (int)birdY;
  }

  spr.fillEllipse(x + BIRD_WIDTH/2, y + BIRD_HEIGHT/2, BIRD_WIDTH/2, BIRD_HEIGHT/2, COLOR_BIRD_BODY);

  int wingY = y + BIRD_HEIGHT/2;
  if (birdFrame == 0) wingY -= 2;
  else if (birdFrame == 2) wingY += 2;
  spr.fillEllipse(x + BIRD_WIDTH/3, wingY, 8, 6, COLOR_BIRD_WING);

  spr.fillCircle(x + BIRD_WIDTH - 8, y + 6, 5, COLOR_BIRD_EYE);
  spr.fillCircle(x + BIRD_WIDTH - 7, y + 6, 2, TFT_BLACK);

  int beakX = x + BIRD_WIDTH - 2;
  int beakY = y + BIRD_HEIGHT/2;
  spr.fillTriangle(beakX, beakY - 3, beakX, beakY + 3, beakX + 10, beakY, COLOR_BIRD_BEAK);
}

void drawPipes() {
  for (int i = 0; i < PIPE_COUNT; i++) {
    if (pipes[i].active && pipes[i].x < SCREEN_WIDTH && pipes[i].x + PIPE_WIDTH > 0) {
      int gapTop = pipes[i].gapY - PIPE_GAP / 2;
      int gapBottom = pipes[i].gapY + PIPE_GAP / 2;

      if (gapTop > 0) {
        spr.fillRect(pipes[i].x, 0, PIPE_WIDTH, gapTop, COLOR_PIPE);
        spr.fillRect(pipes[i].x, 0, 4, gapTop, COLOR_PIPE_DARK);
        spr.fillRect(pipes[i].x + PIPE_WIDTH - 4, 0, 4, gapTop, COLOR_PIPE_DARK);
        spr.fillRect(pipes[i].x - 4, gapTop - 20, PIPE_WIDTH + 8, 20, COLOR_PIPE);
        spr.drawRect(pipes[i].x - 4, gapTop - 20, PIPE_WIDTH + 8, 20, COLOR_PIPE_DARK);
      }

      int bottomPipeHeight = SCREEN_HEIGHT - GROUND_HEIGHT - gapBottom;
      if (bottomPipeHeight > 0) {
        spr.fillRect(pipes[i].x, gapBottom, PIPE_WIDTH, bottomPipeHeight, COLOR_PIPE);
        spr.fillRect(pipes[i].x, gapBottom, 4, bottomPipeHeight, COLOR_PIPE_DARK);
        spr.fillRect(pipes[i].x + PIPE_WIDTH - 4, gapBottom, 4, bottomPipeHeight, COLOR_PIPE_DARK);
        spr.fillRect(pipes[i].x - 4, gapBottom, PIPE_WIDTH + 8, 20, COLOR_PIPE);
        spr.drawRect(pipes[i].x - 4, gapBottom, PIPE_WIDTH + 8, 20, COLOR_PIPE_DARK);
      }
    }
  }
}

void drawGround() {
  int groundY = SCREEN_HEIGHT - GROUND_HEIGHT;
  spr.fillRect(0, groundY, SCREEN_WIDTH, 8, COLOR_GRASS);
  spr.fillRect(0, groundY + 8, SCREEN_WIDTH, GROUND_HEIGHT - 8, COLOR_GROUND);
  for (int x = 0; x < SCREEN_WIDTH; x += 20) {
    spr.drawLine(x, groundY + 8, x + 10, groundY + GROUND_HEIGHT, 0xA461);
  }
}

void drawScore() {
  spr.setTextColor(COLOR_TEXT);
  spr.setTextSize(4);
  String scoreStr = String(score);
  int textWidth = scoreStr.length() * 24;
  int x = (SCREEN_WIDTH - textWidth) / 2;

  spr.setTextColor(TFT_BLACK);
  spr.setCursor(x - 2, 22); spr.print(score);
  spr.setCursor(x + 2, 22); spr.print(score);
  spr.setCursor(x, 20);     spr.print(score);
  spr.setCursor(x, 24);     spr.print(score);

  spr.setTextColor(COLOR_TEXT);
  spr.setCursor(x, 22);
  spr.print(score);
}

void drawMenu() {
  spr.fillSprite(COLOR_SKY);

  drawGround();
  drawBird();

  spr.setTextColor(COLOR_TEXT);
  spr.setTextSize(4);

  spr.setTextColor(TFT_BLACK);
  spr.setCursor(98, 52); spr.println("FLAPPY BIRD");
  spr.setCursor(102, 52); spr.println("FLAPPY BIRD");
  spr.setTextColor(COLOR_SCORE);
  spr.setCursor(100, 50); spr.println("FLAPPY BIRD");

  spr.setTextColor(COLOR_TEXT);
  spr.setTextSize(2);
  spr.setCursor(100, 200); spr.println("Push Joystick UP or");
  spr.setCursor(100, 225); spr.println("Press Button to FLAP");

  if (highScore > 0) {
    spr.setTextColor(TFT_CYAN);
    spr.setCursor(160, 260);
    spr.printf("Best: %d", highScore);
  }

  spr.pushSprite(0, 0);
}

void drawReady() {

  spr.setTextColor(COLOR_TEXT);
  spr.setTextSize(3);

  spr.setTextColor(TFT_BLACK);
  spr.setCursor(148, 102); spr.println("GET READY!");
  spr.setCursor(152, 102); spr.println("GET READY!");
  spr.setTextColor(COLOR_SCORE);
  spr.setCursor(150, 100); spr.println("GET READY!");

  spr.setTextColor(COLOR_TEXT);
  spr.setTextSize(2);
  spr.setCursor(150, 150); spr.println("Tap to start");

  spr.pushSprite(0, 0);
}

void drawGameOver() {

  spr.fillRect(90, 80, 300, 160, TFT_NAVY);
  spr.drawRect(90, 80, 300, 160, TFT_WHITE);

  spr.setTextColor(TFT_RED);
  spr.setTextSize(3);
  spr.setCursor(145, 95); spr.println("GAME OVER");

  spr.setTextColor(COLOR_TEXT);
  spr.setTextSize(2);
  spr.setCursor(180, 140); spr.printf("Score: %d", score);
  spr.setCursor(180, 170); spr.printf("Best: %d", highScore);

  if (score >= highScore && score > 0) {
    spr.setTextColor(TFT_MAGENTA);
    spr.setCursor(175, 200); spr.println("NEW RECORD!");
  } else {
    spr.setTextColor(TFT_WHITE);
    spr.setCursor(145, 200); spr.println("Tap to restart");
  }

  spr.pushSprite(0, 0);
}
