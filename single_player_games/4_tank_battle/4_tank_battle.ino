
#include <TFT_eSPI.h>

#define JOYSTICK_X_PIN    1
#define JOYSTICK_Y_PIN    2
#define BUTTON_PIN        11
#define LED_PIN           47

#define SCREEN_WIDTH      480
#define SCREEN_HEIGHT     320

#define TILE_SIZE         20
#define MAP_WIDTH         24
#define MAP_HEIGHT        15

#define TANK_SIZE         18
#define TANK_SPEED        3
#define BULLET_SIZE       4
#define BULLET_SPEED      6

#define MAX_BULLETS       10
#define MAX_ENEMIES       5
#define ENEMY_SHOOT_INTERVAL 2000
#define ENEMY_MOVE_INTERVAL  100

#define JOYSTICK_CENTER   2048
#define JOYSTICK_THRESHOLD 1000

#define COLOR_BACKGROUND  TFT_BLACK
#define COLOR_PLAYER      TFT_GREEN
#define COLOR_PLAYER_DARK 0x03E0
#define COLOR_ENEMY       TFT_RED
#define COLOR_ENEMY_DARK  0x7800
#define COLOR_BULLET      TFT_YELLOW
#define COLOR_WALL        0x8410
#define COLOR_STEEL       0xC618
#define COLOR_WATER       0x001F
#define COLOR_GRASS       0x07E0
#define COLOR_BASE        0xFFE0
#define COLOR_TEXT        TFT_WHITE
#define COLOR_HUD         TFT_CYAN
#define COLOR_HUD_BG      TFT_NAVY

enum Direction {
  DIR_UP,
  DIR_DOWN,
  DIR_LEFT,
  DIR_RIGHT
};

enum TileType {
  TILE_EMPTY = 0,
  TILE_BRICK = 1,
  TILE_STEEL = 2,
  TILE_WATER = 3,
  TILE_GRASS = 4,
  TILE_BASE = 5
};

enum GameState {
  STATE_MENU,
  STATE_PLAYING,
  STATE_GAME_OVER,
  STATE_VICTORY
};

struct Tank {
  float x, y;
  Direction dir;
  bool alive;
  unsigned long lastShot;
  unsigned long lastMove;
};

struct Bullet {
  float x, y;
  Direction dir;
  bool active;
  bool isPlayer;
};

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite sprite = TFT_eSprite(&tft);

uint8_t gameMap[MAP_HEIGHT][MAP_WIDTH];

Tank player;

Tank enemies[MAX_ENEMIES];
int enemyCount = 0;
int enemiesKilled = 0;
int totalEnemies = 10;

Bullet bullets[MAX_BULLETS];

GameState gameState = STATE_MENU;
int playerLives = 3;
int level = 1;
int score = 0;

bool baseDestroyed = false;

bool lastButtonState = HIGH;
bool shootPressed = false;

unsigned long lastUpdateTime = 0;
unsigned long lastEnemySpawn = 0;
unsigned long lastBlinkTime = 0;
bool ledState = false;

const uint8_t mapTemplate[MAP_HEIGHT][MAP_WIDTH] PROGMEM = {
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,1,1,0,1,1,0,1,1,0,0,0,0,0,1,1,0,1,1,0,1,1,0,0},
  {0,1,1,0,1,1,0,1,1,0,2,2,2,2,0,1,1,0,1,1,0,1,1,0},
  {0,1,1,0,1,1,0,1,1,0,0,0,0,0,0,1,1,0,1,1,0,1,1,0},
  {0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0},
  {1,1,0,1,1,0,0,1,1,0,1,1,1,1,0,1,1,0,0,1,1,0,1,1},
  {2,2,0,1,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,1,0,2,2},
  {0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0},
  {0,1,1,0,1,1,0,0,0,3,3,4,4,3,3,0,0,0,1,1,0,1,1,0},
  {0,1,1,0,1,1,0,0,0,3,3,0,0,3,3,0,0,0,1,1,0,1,1,0},
  {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0},
  {1,1,0,1,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,1,0,1,1},
  {0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0},
  {0,1,1,1,0,0,0,0,0,1,1,5,5,1,1,0,0,0,0,0,1,1,1,0},
  {0,1,1,1,0,0,0,0,0,1,1,5,5,1,1,0,0,0,0,0,1,1,1,0}
};

void initGame();
void initMap();
void handleInput();
void updateGame();
void drawGame();
void drawMap();
void drawTank(Tank* tank, bool isPlayer);
void drawBullets();
void drawHUD();
void drawMenu();
void drawGameOver();
void drawVictory();
void playerShoot();
void enemyShoot(int index);
void updateBullets();
void updateEnemies();
void spawnEnemy();
bool checkTankCollision(float x, float y, Tank* exclude);
bool checkWallCollision(float x, float y);
void destroyTile(int tx, int ty);
Direction readJoystick();
void blinkLED(int interval);

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
  tft.fillScreen(COLOR_BACKGROUND);

  if (psramFound()) {
    sprite.setColorDepth(16);
    Serial.println("PSRAM detected, using 16-bit color.");
  } else {

    sprite.setColorDepth(16);
    Serial.println("PSRAM NOT detected.");
  }

  void* ptr = sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
  if (ptr == NULL) {
    Serial.println("警告: Sprite 内存分配失败！尝试降低色深...");
    sprite.setColorDepth(8);
    ptr = sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (ptr == NULL) {
      Serial.println("错误: Sprite 创建完全失败，无法运行游戏。");
      while(1) delay(100);
    }
  }
  sprite.setSwapBytes(true);

  randomSeed(analogRead(0));

  drawMenu();

  Serial.println("ESP32坦克大战游戏初始化完成");
}

void loop() {
  handleInput();

  unsigned long currentTime = millis();

  switch (gameState) {
    case STATE_MENU:
      blinkLED(500);
      if (shootPressed) {
        shootPressed = false;
        initGame();
        gameState = STATE_PLAYING;

      }
      break;

    case STATE_PLAYING:
      digitalWrite(LED_PIN, HIGH);

      if (currentTime - lastUpdateTime >= 16) {
        lastUpdateTime = currentTime;
        updateGame();
        drawGame();
      }
      break;

    case STATE_GAME_OVER:
      blinkLED(100);
      if (shootPressed) {
        shootPressed = false;
        gameState = STATE_MENU;
        drawMenu();
      }
      break;

    case STATE_VICTORY:
      blinkLED(200);
      if (shootPressed) {
        shootPressed = false;
        level++;
        initGame();
        gameState = STATE_PLAYING;
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

  initMap();

  player.x = (MAP_WIDTH / 2 - 2) * TILE_SIZE;
  player.y = (MAP_HEIGHT - 2) * TILE_SIZE;
  player.dir = DIR_UP;
  player.alive = true;
  player.lastShot = 0;

  for (int i = 0; i < MAX_ENEMIES; i++) {
    enemies[i].alive = false;
  }
  enemyCount = 0;
  enemiesKilled = 0;
  totalEnemies = 5 + level * 2;

  for (int i = 0; i < MAX_BULLETS; i++) {
    bullets[i].active = false;
  }

  baseDestroyed = false;

  lastEnemySpawn = millis();

  Serial.printf("第 %d 关开始！需要消灭 %d 个敌人\n", level, totalEnemies);
}

void initMap() {
  for (int y = 0; y < MAP_HEIGHT; y++) {
    for (int x = 0; x < MAP_WIDTH; x++) {
      gameMap[y][x] = pgm_read_byte(&mapTemplate[y][x]);
    }
  }
}

void handleInput() {

  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    shootPressed = true;
    if (gameState == STATE_PLAYING) {
      playerShoot();
      shootPressed = false;
    }
  }
  lastButtonState = currentButtonState;

  if (gameState == STATE_PLAYING && player.alive) {
    Direction newDir = readJoystick();

    int x_adc = analogRead(JOYSTICK_X_PIN);
    int y_adc = analogRead(JOYSTICK_Y_PIN);
    int x_offset = abs(x_adc - JOYSTICK_CENTER);
    int y_offset = abs(y_adc - JOYSTICK_CENTER);

    if (x_offset > JOYSTICK_THRESHOLD || y_offset > JOYSTICK_THRESHOLD) {
      player.dir = newDir;

      float newX = player.x;
      float newY = player.y;

      switch (newDir) {
        case DIR_UP:    newY -= TANK_SPEED; break;
        case DIR_DOWN:  newY += TANK_SPEED; break;
        case DIR_LEFT:  newX -= TANK_SPEED; break;
        case DIR_RIGHT: newX += TANK_SPEED; break;
      }

      if (newX >= 0 && newX <= SCREEN_WIDTH - TANK_SIZE &&
          newY >= 20 && newY <= SCREEN_HEIGHT - TANK_SIZE) {

        if (!checkWallCollision(newX, newY) && !checkTankCollision(newX, newY, &player)) {
          player.x = newX;
          player.y = newY;
        }
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
    if (x_offset > JOYSTICK_THRESHOLD) return DIR_RIGHT;
  } else {
    if (y_offset < -JOYSTICK_THRESHOLD) return DIR_UP;
    if (y_offset > JOYSTICK_THRESHOLD) return DIR_DOWN;
  }

  return player.dir;
}

void playerShoot() {
  unsigned long currentTime = millis();
  if (currentTime - player.lastShot < 300) return;

  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      bullets[i].x = player.x + TANK_SIZE / 2 - BULLET_SIZE / 2;
      bullets[i].y = player.y + TANK_SIZE / 2 - BULLET_SIZE / 2;
      bullets[i].dir = player.dir;
      bullets[i].active = true;
      bullets[i].isPlayer = true;
      player.lastShot = currentTime;
      Serial.println("玩家射击！");
      break;
    }
  }
}

void enemyShoot(int index) {
  Tank* enemy = &enemies[index];
  unsigned long currentTime = millis();
  if (currentTime - enemy->lastShot < ENEMY_SHOOT_INTERVAL) return;

  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      bullets[i].x = enemy->x + TANK_SIZE / 2 - BULLET_SIZE / 2;
      bullets[i].y = enemy->y + TANK_SIZE / 2 - BULLET_SIZE / 2;
      bullets[i].dir = enemy->dir;
      bullets[i].active = true;
      bullets[i].isPlayer = false;
      enemy->lastShot = currentTime;
      break;
    }
  }
}

void updateGame() {

  updateBullets();

  updateEnemies();

  unsigned long currentTime = millis();
  if (enemyCount < MAX_ENEMIES && enemiesKilled + enemyCount < totalEnemies) {
    if (currentTime - lastEnemySpawn >= 3000) {
      spawnEnemy();
      lastEnemySpawn = currentTime;
    }
  }

  if (enemiesKilled >= totalEnemies) {
    gameState = STATE_VICTORY;
    drawVictory();
  }

  if (!player.alive || baseDestroyed) {
    playerLives--;
    if (playerLives <= 0) {
      gameState = STATE_GAME_OVER;
      drawGameOver();
    } else {

      player.x = (MAP_WIDTH / 2 - 2) * TILE_SIZE;
      player.y = (MAP_HEIGHT - 2) * TILE_SIZE;
      player.dir = DIR_UP;
      player.alive = true;
    }
  }
}

void updateBullets() {
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;

    switch (bullets[i].dir) {
      case DIR_UP:    bullets[i].y -= BULLET_SPEED; break;
      case DIR_DOWN:  bullets[i].y += BULLET_SPEED; break;
      case DIR_LEFT:  bullets[i].x -= BULLET_SPEED; break;
      case DIR_RIGHT: bullets[i].x += BULLET_SPEED; break;
    }

    if (bullets[i].x < 0 || bullets[i].x > SCREEN_WIDTH ||
        bullets[i].y < 20 || bullets[i].y > SCREEN_HEIGHT) {
      bullets[i].active = false;
      continue;
    }

    int tx = (int)bullets[i].x / TILE_SIZE;
    int ty = ((int)bullets[i].y - 20) / TILE_SIZE;

    if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT) {
      uint8_t tile = gameMap[ty][tx];
      if (tile == TILE_BRICK) {
        destroyTile(tx, ty);
        bullets[i].active = false;
        continue;
      } else if (tile == TILE_STEEL) {
        bullets[i].active = false;
        continue;
      } else if (tile == TILE_BASE) {
        baseDestroyed = true;
        gameMap[ty][tx] = TILE_EMPTY;
        bullets[i].active = false;
        Serial.println("基地被摧毁！");
        continue;
      }
    }

    if (bullets[i].isPlayer) {

      for (int j = 0; j < MAX_ENEMIES; j++) {
        if (enemies[j].alive) {
          if (bullets[i].x >= enemies[j].x && bullets[i].x <= enemies[j].x + TANK_SIZE &&
              bullets[i].y >= enemies[j].y && bullets[i].y <= enemies[j].y + TANK_SIZE) {
            enemies[j].alive = false;
            enemyCount--;
            enemiesKilled++;
            score += 100;
            bullets[i].active = false;
            Serial.printf("消灭敌人！得分: %d\n", score);
            break;
          }
        }
      }
    } else {

      if (player.alive) {
        if (bullets[i].x >= player.x && bullets[i].x <= player.x + TANK_SIZE &&
            bullets[i].y >= player.y && bullets[i].y <= player.y + TANK_SIZE) {
          player.alive = false;
          bullets[i].active = false;
          Serial.println("玩家被击中！");
        }
      }
    }
  }
}

void updateEnemies() {
  unsigned long currentTime = millis();

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].alive) continue;

    if (currentTime - enemies[i].lastMove >= ENEMY_MOVE_INTERVAL) {
      enemies[i].lastMove = currentTime;

      if (random(100) < 30) {
        enemies[i].dir = (Direction)random(4);
      }

      float newX = enemies[i].x;
      float newY = enemies[i].y;

      switch (enemies[i].dir) {
        case DIR_UP:    newY -= TANK_SPEED; break;
        case DIR_DOWN:  newY += TANK_SPEED; break;
        case DIR_LEFT:  newX -= TANK_SPEED; break;
        case DIR_RIGHT: newX += TANK_SPEED; break;
      }

      if (newX >= 0 && newX <= SCREEN_WIDTH - TANK_SIZE &&
          newY >= 20 && newY <= SCREEN_HEIGHT - TANK_SIZE) {
        if (!checkWallCollision(newX, newY) && !checkTankCollision(newX, newY, &enemies[i])) {
          enemies[i].x = newX;
          enemies[i].y = newY;
        } else {

          enemies[i].dir = (Direction)random(4);
        }
      } else {
        enemies[i].dir = (Direction)random(4);
      }
    }

    if (random(100) < 5) {
      enemyShoot(i);
    }
  }
}

void spawnEnemy() {

  int index = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].alive) {
      index = i;
      break;
    }
  }
  if (index < 0) return;

  int spawnPoints[] = {0, MAP_WIDTH / 2, MAP_WIDTH - 1};
  int spawnX = spawnPoints[random(3)] * TILE_SIZE;

  enemies[index].x = spawnX;
  enemies[index].y = 20;
  enemies[index].dir = DIR_DOWN;
  enemies[index].alive = true;
  enemies[index].lastShot = millis();
  enemies[index].lastMove = millis();
  enemyCount++;

  Serial.printf("敌人生成于 X=%d\n", spawnX);
}

bool checkTankCollision(float x, float y, Tank* exclude) {

  if (&player != exclude && player.alive) {
    if (x < player.x + TANK_SIZE && x + TANK_SIZE > player.x &&
        y < player.y + TANK_SIZE && y + TANK_SIZE > player.y) {
      return true;
    }
  }

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (&enemies[i] != exclude && enemies[i].alive) {
      if (x < enemies[i].x + TANK_SIZE && x + TANK_SIZE > enemies[i].x &&
          y < enemies[i].y + TANK_SIZE && y + TANK_SIZE > enemies[i].y) {
        return true;
      }
    }
  }

  return false;
}

bool checkWallCollision(float x, float y) {

  int corners[4][2] = {
    {(int)x / TILE_SIZE, ((int)y - 20) / TILE_SIZE},
    {(int)(x + TANK_SIZE - 1) / TILE_SIZE, ((int)y - 20) / TILE_SIZE},
    {(int)x / TILE_SIZE, ((int)(y + TANK_SIZE - 1) - 20) / TILE_SIZE},
    {(int)(x + TANK_SIZE - 1) / TILE_SIZE, ((int)(y + TANK_SIZE - 1) - 20) / TILE_SIZE}
  };

  for (int i = 0; i < 4; i++) {
    int tx = corners[i][0];
    int ty = corners[i][1];

    if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT) {
      uint8_t tile = gameMap[ty][tx];
      if (tile == TILE_BRICK || tile == TILE_STEEL || tile == TILE_WATER || tile == TILE_BASE) {
        return true;
      }
    }
  }

  return false;
}

void destroyTile(int tx, int ty) {
  if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT) {
    if (gameMap[ty][tx] == TILE_BRICK) {
      gameMap[ty][tx] = TILE_EMPTY;
    }
  }
}

void drawGame() {

  sprite.fillSprite(COLOR_BACKGROUND);

  drawMap();

  if (player.alive) {
    drawTank(&player, true);
  }

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].alive) {
      drawTank(&enemies[i], false);
    }
  }

  drawBullets();

  drawHUD();

  sprite.pushSprite(0, 0);
}

void drawMap() {
  for (int y = 0; y < MAP_HEIGHT; y++) {
    for (int x = 0; x < MAP_WIDTH; x++) {
      uint8_t tile = gameMap[y][x];
      if (tile == TILE_EMPTY) continue;

      int px = x * TILE_SIZE;
      int py = y * TILE_SIZE + 20;

      switch (tile) {
        case TILE_BRICK:
          sprite.fillRect(px, py, TILE_SIZE, TILE_SIZE, COLOR_WALL);
          sprite.drawLine(px, py + TILE_SIZE/2, px + TILE_SIZE, py + TILE_SIZE/2, 0x4208);
          sprite.drawLine(px + TILE_SIZE/2, py, px + TILE_SIZE/2, py + TILE_SIZE, 0x4208);
          break;

        case TILE_STEEL:
          sprite.fillRect(px, py, TILE_SIZE, TILE_SIZE, COLOR_STEEL);
          sprite.drawRect(px, py, TILE_SIZE, TILE_SIZE, TFT_WHITE);
          break;

        case TILE_WATER:
          sprite.fillRect(px, py, TILE_SIZE, TILE_SIZE, COLOR_WATER);
          for (int i = 0; i < TILE_SIZE; i += 4) {
            sprite.drawLine(px, py + i, px + TILE_SIZE, py + i, 0x033F);
          }
          break;

        case TILE_GRASS:
          sprite.fillRect(px, py, TILE_SIZE, TILE_SIZE, COLOR_GRASS);
          break;

        case TILE_BASE:
          sprite.fillRect(px, py, TILE_SIZE, TILE_SIZE, COLOR_BASE);
          sprite.fillTriangle(px + TILE_SIZE/2, py + 2,
                          px + 2, py + TILE_SIZE - 2,
                          px + TILE_SIZE - 2, py + TILE_SIZE - 2, TFT_RED);
          break;
      }
    }
  }
}

void drawTank(Tank* tank, bool isPlayer) {
  int x = (int)tank->x;
  int y = (int)tank->y;

  uint16_t bodyColor = isPlayer ? COLOR_PLAYER : COLOR_ENEMY;
  uint16_t darkColor = isPlayer ? COLOR_PLAYER_DARK : COLOR_ENEMY_DARK;

  sprite.fillRect(x + 2, y + 2, TANK_SIZE - 4, TANK_SIZE - 4, bodyColor);
  sprite.drawRect(x + 2, y + 2, TANK_SIZE - 4, TANK_SIZE - 4, darkColor);

  switch (tank->dir) {
    case DIR_UP:
    case DIR_DOWN:
      sprite.fillRect(x, y, 3, TANK_SIZE, darkColor);
      sprite.fillRect(x + TANK_SIZE - 3, y, 3, TANK_SIZE, darkColor);
      break;
    case DIR_LEFT:
    case DIR_RIGHT:
      sprite.fillRect(x, y, TANK_SIZE, 3, darkColor);
      sprite.fillRect(x, y + TANK_SIZE - 3, TANK_SIZE, 3, darkColor);
      break;
  }

  int cx = x + TANK_SIZE / 2;
  int cy = y + TANK_SIZE / 2;
  switch (tank->dir) {
    case DIR_UP:
      sprite.fillRect(cx - 2, y - 4, 4, TANK_SIZE / 2 + 4, darkColor);
      break;
    case DIR_DOWN:
      sprite.fillRect(cx - 2, cy, 4, TANK_SIZE / 2 + 4, darkColor);
      break;
    case DIR_LEFT:
      sprite.fillRect(x - 4, cy - 2, TANK_SIZE / 2 + 4, 4, darkColor);
      break;
    case DIR_RIGHT:
      sprite.fillRect(cx, cy - 2, TANK_SIZE / 2 + 4, 4, darkColor);
      break;
  }
}

void drawBullets() {
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (bullets[i].active) {
      sprite.fillRect((int)bullets[i].x, (int)bullets[i].y, BULLET_SIZE, BULLET_SIZE, COLOR_BULLET);
    }
  }
}

void drawHUD() {

  sprite.fillRect(0, 0, SCREEN_WIDTH, 20, COLOR_HUD_BG);

  sprite.setTextColor(COLOR_HUD, COLOR_HUD_BG);
  sprite.setTextSize(1);
  sprite.setCursor(5, 6);
  sprite.printf("SCORE: %d", score);

  sprite.setCursor(150, 6);
  sprite.printf("LIVES: %d", playerLives);

  sprite.setCursor(250, 6);
  sprite.printf("LEVEL: %d", level);

  sprite.setCursor(350, 6);
  sprite.printf("ENEMIES: %d/%d", enemiesKilled, totalEnemies);
}

void drawMenu() {
  tft.fillScreen(COLOR_BACKGROUND);

  tft.setTextColor(COLOR_PLAYER);
  tft.setTextSize(4);
  tft.setCursor(100, 60);
  tft.println("TANK BATTLE");

  tft.fillRect(SCREEN_WIDTH/2 - 10, 130, 20, 20, COLOR_PLAYER);
  tft.drawRect(SCREEN_WIDTH/2 - 10, 130, 20, 20, COLOR_PLAYER_DARK);
  tft.fillRect(SCREEN_WIDTH/2 - 2, 120, 4, 15, COLOR_PLAYER_DARK);

  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(100, 180);
  tft.println("Joystick: Move Tank");
  tft.setCursor(100, 210);
  tft.println("Button: Fire");
  tft.setCursor(100, 250);
  tft.println("Press Button to Start");
}

void drawGameOver() {
  tft.fillRect(100, 100, 280, 120, COLOR_HUD_BG);
  tft.drawRect(100, 100, 280, 120, COLOR_TEXT);

  tft.setTextColor(TFT_RED);
  tft.setTextSize(3);
  tft.setCursor(155, 115);
  tft.println("GAME OVER");

  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(160, 155);
  tft.printf("Score: %d", score);

  tft.setCursor(130, 185);
  tft.println("Press to restart");

  playerLives = 3;
  level = 1;
  score = 0;
}

void drawVictory() {
  tft.fillRect(100, 100, 280, 120, TFT_DARKGREEN);
  tft.drawRect(100, 100, 280, 120, COLOR_TEXT);

  tft.setTextColor(COLOR_PLAYER);
  tft.setTextSize(3);
  tft.setCursor(145, 115);
  tft.println("VICTORY!");

  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(130, 155);
  tft.printf("Level %d Complete", level);

  tft.setCursor(130, 185);
  tft.println("Press for next level");
}
