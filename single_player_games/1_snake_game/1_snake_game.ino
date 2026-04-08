
#include <TFT_eSPI.h>

#define JOYSTICK_X_PIN    1
#define JOYSTICK_Y_PIN    2
#define BUTTON_PIN        11
#define LED_PIN           47

#define SCREEN_WIDTH      480
#define SCREEN_HEIGHT     320
#define GRID_SIZE         16
#define GAME_WIDTH        (SCREEN_WIDTH / GRID_SIZE)
#define GAME_HEIGHT       ((SCREEN_HEIGHT - 40) / GRID_SIZE)
#define MAX_SNAKE_LENGTH  100

#define JOYSTICK_CENTER   2048
#define JOYSTICK_THRESHOLD 1000

#define GAME_SPEED_INIT   200
#define GAME_SPEED_MIN    80

#define COLOR_BACKGROUND  TFT_BLACK
#define COLOR_SNAKE_HEAD  TFT_GREEN
#define COLOR_SNAKE_BODY  TFT_DARKGREEN
#define COLOR_FOOD        TFT_RED
#define COLOR_BORDER      TFT_WHITE
#define COLOR_TEXT        TFT_YELLOW
#define COLOR_GAME_OVER   TFT_RED

enum Direction { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
enum GameState { STATE_MENU, STATE_PLAYING, STATE_PAUSED, STATE_GAME_OVER };

struct Point {
  int x;
  int y;
};

TFT_eSPI tft = TFT_eSPI(320, 480);

Point snake[MAX_SNAKE_LENGTH];
int snakeLength = 3;
Direction snakeDir = DIR_RIGHT;
Direction nextDir = DIR_RIGHT;

Point food;
GameState gameState = STATE_MENU;

int score = 0;
int lastScore = -1;
int highScore = 0;
int gameSpeed = GAME_SPEED_INIT;

bool lastButtonState = HIGH;
bool buttonPressed = false;

unsigned long lastMoveTime = 0;
unsigned long lastBlinkTime = 0;
bool ledState = false;

void initGame();
void handleInput();
void updateGame();
void drawFullGame();
void moveSnake();
void generateFood();
bool checkCollision();
bool checkFoodCollision();
void drawSnakeSegment(int index);
void eraseGrid(int x, int y);
void drawFood();
void drawScore();
void drawMenu();
void drawGameOver();
void drawPaused();
Direction readJoystick();
void blinkLED(int interval);

void setup() {
  Serial.begin(115200);
  pinMode(JOYSTICK_X_PIN, INPUT);
  pinMode(JOYSTICK_Y_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(COLOR_BACKGROUND);

  drawMenu();
  randomSeed(analogRead(0));
}

void loop() {
  handleInput();

  switch (gameState) {
    case STATE_MENU:
      blinkLED(500);
      if (buttonPressed) {
        buttonPressed = false;
        initGame();
        gameState = STATE_PLAYING;
        tft.fillScreen(COLOR_BACKGROUND);
        drawFullGame();
      }
      break;

    case STATE_PLAYING:
      digitalWrite(LED_PIN, HIGH);
      updateGame();
      break;

    case STATE_PAUSED:
      blinkLED(200);
      if (buttonPressed) {
        buttonPressed = false;
        gameState = STATE_PLAYING;

        tft.fillScreen(COLOR_BACKGROUND);
        drawFullGame();
      }
      break;

    case STATE_GAME_OVER:
      blinkLED(100);
      if (buttonPressed) {
        buttonPressed = false;
        gameState = STATE_MENU;
        drawMenu();
      }
      break;
  }
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
  snakeLength = 3;
  snakeDir = DIR_RIGHT;
  nextDir = DIR_RIGHT;

  int startX = GAME_WIDTH / 2;
  int startY = GAME_HEIGHT / 2;

  for (int i = 0; i < snakeLength; i++) {
    snake[i].x = startX - i;
    snake[i].y = startY;
  }

  score = 0;
  lastScore = -1;
  gameSpeed = GAME_SPEED_INIT;
  generateFood();
  lastMoveTime = millis();
}

void handleInput() {
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    buttonPressed = true;
    if (gameState == STATE_PLAYING) {
      gameState = STATE_PAUSED;
      drawPaused();
      buttonPressed = false;
    }
  }
  lastButtonState = currentButtonState;

  if (gameState == STATE_PLAYING) {
    Direction newDir = readJoystick();
    if (newDir != snakeDir) {
      if (!((snakeDir == DIR_UP && newDir == DIR_DOWN) ||
            (snakeDir == DIR_DOWN && newDir == DIR_UP) ||
            (snakeDir == DIR_LEFT && newDir == DIR_RIGHT) ||
            (snakeDir == DIR_RIGHT && newDir == DIR_LEFT))) {
        nextDir = newDir;
      }
    }
  }
}

Direction readJoystick() {
  int x_adc = analogRead(JOYSTICK_X_PIN);
  int y_adc = analogRead(JOYSTICK_Y_PIN);
  int x_offset = x_adc - JOYSTICK_CENTER;
  int y_offset = y_adc - JOYSTICK_CENTER;

  if (abs(x_offset) > abs(y_offset)) {
    if (x_offset < -JOYSTICK_THRESHOLD) return DIR_LEFT;
    else if (x_offset > JOYSTICK_THRESHOLD) return DIR_RIGHT;
  } else {
    if (y_offset < -JOYSTICK_THRESHOLD) return DIR_UP;
    else if (y_offset > JOYSTICK_THRESHOLD) return DIR_DOWN;
  }
  return snakeDir;
}

void updateGame() {
  unsigned long currentTime = millis();

  if (currentTime - lastMoveTime >= gameSpeed) {
    lastMoveTime = currentTime;

    Point oldTail = snake[snakeLength - 1];

    snakeDir = nextDir;

    Point newHead = snake[0];
    switch (snakeDir) {
      case DIR_UP:    newHead.y--; break;
      case DIR_DOWN:  newHead.y++; break;
      case DIR_LEFT:  newHead.x--; break;
      case DIR_RIGHT: newHead.x++; break;
    }

    for (int i = snakeLength - 1; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
    snake[0] = newHead;

    if (checkCollision()) {
      gameState = STATE_GAME_OVER;
      if (score > highScore) highScore = score;
      drawGameOver();
      return;
    }

    drawSnakeSegment(1);

    drawSnakeSegment(0);

    if (checkFoodCollision()) {
      score += 10;
      snakeLength++;
      snake[snakeLength - 1] = oldTail;

      if (gameSpeed > GAME_SPEED_MIN) gameSpeed -= 5;

      generateFood();
      drawFood();
      drawScore();
    } else {

      eraseGrid(oldTail.x, oldTail.y);
    }
  }
}

void generateFood() {
  bool validPosition;
  do {
    validPosition = true;
    food.x = random(0, GAME_WIDTH);
    food.y = random(0, GAME_HEIGHT);
    for (int i = 0; i < snakeLength; i++) {
      if (food.x == snake[i].x && food.y == snake[i].y) {
        validPosition = false;
        break;
      }
    }
  } while (!validPosition);
}

bool checkCollision() {
  if (snake[0].x < 0 || snake[0].x >= GAME_WIDTH ||
      snake[0].y < 0 || snake[0].y >= GAME_HEIGHT) return true;
  for (int i = 3; i < snakeLength; i++) {
    if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) return true;
  }
  return false;
}

bool checkFoodCollision() {
  return (snake[0].x == food.x && snake[0].y == food.y);
}

void drawFullGame() {
  tft.drawRect(0, 40, SCREEN_WIDTH, SCREEN_HEIGHT - 40, COLOR_BORDER);

  for (int i = 0; i < snakeLength; i++) {
    drawSnakeSegment(i);
  }
  drawFood();
  lastScore = -1;
  drawScore();
}

void eraseGrid(int x, int y) {
  tft.fillRect(x * GRID_SIZE, y * GRID_SIZE + 40, GRID_SIZE, GRID_SIZE, COLOR_BACKGROUND);
}

void drawSnakeSegment(int i) {
  int x = snake[i].x * GRID_SIZE;
  int y = snake[i].y * GRID_SIZE + 40;

  if (i == 0) {

    tft.fillRect(x + 1, y + 1, GRID_SIZE - 2, GRID_SIZE - 2, COLOR_SNAKE_HEAD);

    int eyeSize = 3;
    int eyeOffset = 4;
    switch (snakeDir) {
      case DIR_UP:
        tft.fillCircle(x + eyeOffset, y + eyeOffset, eyeSize, TFT_WHITE);
        tft.fillCircle(x + GRID_SIZE - eyeOffset, y + eyeOffset, eyeSize, TFT_WHITE);
        break;
      case DIR_DOWN:
        tft.fillCircle(x + eyeOffset, y + GRID_SIZE - eyeOffset, eyeSize, TFT_WHITE);
        tft.fillCircle(x + GRID_SIZE - eyeOffset, y + GRID_SIZE - eyeOffset, eyeSize, TFT_WHITE);
        break;
      case DIR_LEFT:
        tft.fillCircle(x + eyeOffset, y + eyeOffset, eyeSize, TFT_WHITE);
        tft.fillCircle(x + eyeOffset, y + GRID_SIZE - eyeOffset, eyeSize, TFT_WHITE);
        break;
      case DIR_RIGHT:
        tft.fillCircle(x + GRID_SIZE - eyeOffset, y + eyeOffset, eyeSize, TFT_WHITE);
        tft.fillCircle(x + GRID_SIZE - eyeOffset, y + GRID_SIZE - eyeOffset, eyeSize, TFT_WHITE);
        break;
    }
  } else {

    tft.fillRect(x + 2, y + 2, GRID_SIZE - 4, GRID_SIZE - 4, COLOR_SNAKE_BODY);

    tft.drawRect(x + 2, y + 2, GRID_SIZE - 4, GRID_SIZE - 4, COLOR_SNAKE_BODY);
  }
}

void drawFood() {
  int x = food.x * GRID_SIZE + GRID_SIZE / 2;
  int y = food.y * GRID_SIZE + 40 + GRID_SIZE / 2;
  tft.fillCircle(x, y, GRID_SIZE / 2 - 2, COLOR_FOOD);
  tft.fillCircle(x - 2, y - 2, 2, TFT_WHITE);
}

void drawScore() {

  if (score == lastScore) return;
  lastScore = score;

  tft.drawLine(0, 38, SCREEN_WIDTH, 38, COLOR_BORDER);

  tft.setTextSize(2);

  tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
  tft.setCursor(10, 10);
  tft.printf("Score: %d  ", score);

  tft.setCursor(SCREEN_WIDTH - 180, 10);
  tft.printf("High: %d  ", highScore);

  tft.setCursor(SCREEN_WIDTH / 2 - 40, 10);
  tft.printf("Len: %d  ", snakeLength);
}

void drawMenu() {
  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextColor(COLOR_SNAKE_HEAD, COLOR_BACKGROUND);
  tft.setTextSize(4);
  tft.setCursor(100, 60);
  tft.println("SNAKE GAME");

  for (int i = 0; i < 8; i++) {
    int x = 80 + i * 40;
    int y = 130;
    uint16_t color = (i == 0) ? COLOR_SNAKE_HEAD : COLOR_SNAKE_BODY;
    tft.fillRect(x, y, 35, 35, color);
  }

  tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
  tft.setTextSize(2);
  tft.setCursor(80, 200);
  tft.println("Use Joystick to control");
  tft.setCursor(80, 230);
  tft.println("Press Button to Start");

  if (highScore > 0) {
    tft.setTextColor(TFT_CYAN, COLOR_BACKGROUND);
    tft.setCursor(140, 280);
    tft.printf("High Score: %d", highScore);
  }
}

void drawGameOver() {
  tft.fillRect(80, 100, 320, 120, TFT_NAVY);
  tft.drawRect(80, 100, 320, 120, COLOR_BORDER);

  tft.setTextColor(COLOR_GAME_OVER, TFT_NAVY);
  tft.setTextSize(3);
  tft.setCursor(140, 120);
  tft.println("GAME OVER");

  tft.setTextColor(COLOR_TEXT, TFT_NAVY);
  tft.setTextSize(2);
  tft.setCursor(150, 160);
  tft.printf("Final Score: %d", score);

  if (score >= highScore && score > 0) {
    tft.setTextColor(TFT_MAGENTA, TFT_NAVY);
    tft.setCursor(160, 190);
    tft.println("NEW RECORD!");
  } else {
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(120, 190);
    tft.println("Press to Restart");
  }
}

void drawPaused() {
  tft.fillRect(140, 120, 200, 80, TFT_NAVY);
  tft.drawRect(140, 120, 200, 80, COLOR_BORDER);

  tft.setTextColor(TFT_YELLOW, TFT_NAVY);
  tft.setTextSize(3);
  tft.setCursor(175, 140);
  tft.println("PAUSED");

  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(165, 175);
  tft.println("Press button to continue");
}
