#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <math.h>

const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int FRAME_W = SCREEN_W / 2;

const int GREEN_BTN_PIN = 11;
const int BLUE_BTN_PIN = 12;
const int TOUCH_SDA = 9;
const int TOUCH_SCL = 10;
const int TOUCH_INT = 39;
const int TOUCH_RST = 38;

const int REVERSE_PIN = 45;
const int FORWARD_PIN = 46;
const int MOTOR_FREQUENCY = 15000;
const int MOTOR_RESOLUTION = 8;
const int REVERSE_CH = 0;
const int FORWARD_CH = 1;

const uint32_t FRAME_MS = 16;
const int LEADERBOARD_SIZE = 5;
const int MAX_FRUITS = 14;
const int MAX_PARTICLES = 72;
const int FRUIT_SPRITE_SIZE = 56;
const int FRUIT_SPRITE_HALF = FRUIT_SPRITE_SIZE / 2;
const int FRUIT_SPRITE_COUNT = 4;
const uint16_t SPRITE_TRANSPARENT = 0xF81F;
const uint32_t BOMB_EXPLOSION_MS = 2000;

const float GRAVITY = 0.18f;
const int START_LIVES = 3;

struct Fruit {
  bool active;
  bool sliced;
  bool bomb;
  float x;
  float y;
  float vx;
  float vy;
  float r;
  uint16_t bodyColor;
  uint16_t rindColor;
  uint8_t style;
};

struct Particle {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  uint8_t life;
  uint8_t maxLife;
  uint8_t size;
  uint16_t color;
};

enum GameState {
  STATE_TITLE,
  STATE_PLAYING,
  STATE_GAME_OVER,
  STATE_LEADERBOARD
};

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBufferFull = TFT_eSprite(&tft);
TFT_eSprite frameBufferLeft = TFT_eSprite(&tft);
TFT_eSprite frameBufferRight = TFT_eSprite(&tft);
TAMC_GT911 tp(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_W, SCREEN_H);

enum FrameBufferMode {
  FB_NONE,
  FB_FULL,
  FB_SPLIT
};

struct WindowCanvas {
  TFT_eSprite *sprite;
  int xOffset;

  void fillScreen(uint16_t color) { sprite->fillScreen(color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) { sprite->fillRect(x - xOffset, y, w, h, color); }
  void drawRect(int x, int y, int w, int h, uint16_t color) { sprite->drawRect(x - xOffset, y, w, h, color); }
  void drawFastHLine(int x, int y, int w, uint16_t color) { sprite->drawFastHLine(x - xOffset, y, w, color); }
  void drawFastVLine(int x, int y, int h, uint16_t color) { sprite->drawFastVLine(x - xOffset, y, h, color); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) { sprite->drawLine(x0 - xOffset, y0, x1 - xOffset, y1, color); }
  void fillCircle(int x, int y, int r, uint16_t color) { sprite->fillCircle(x - xOffset, y, r, color); }
  void drawCircle(int x, int y, int r, uint16_t color) { sprite->drawCircle(x - xOffset, y, r, color); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) { sprite->fillRoundRect(x - xOffset, y, w, h, r, color); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) { sprite->drawRoundRect(x - xOffset, y, w, h, r, color); }
  void drawPixel(int x, int y, uint16_t color) { sprite->drawPixel(x - xOffset, y, color); }
  void setTextColor(uint16_t fg, uint16_t bg) { sprite->setTextColor(fg, bg); }
  int drawString(const char *text, int x, int y, int font) { return sprite->drawString(text, x - xOffset, y, font); }
  int drawCentreString(const char *text, int x, int y, int font) { return sprite->drawCentreString(text, x - xOffset, y, font); }
  void pushImage(int x, int y, int w, int h, const uint16_t *data, uint16_t transparent) {
    int dx = x - xOffset;
    for (int j = 0; j < h; ++j) {
      int row = j * w;
      for (int i = 0; i < w; ++i) {
        uint16_t c = data[row + i];
        if (c == transparent) continue;
        sprite->drawPixel(dx + i, y + j, c);
      }
    }
  }
};

GameState gameState = STATE_TITLE;
Fruit fruits[MAX_FRUITS];
Particle particles[MAX_PARTICLES];

FrameBufferMode frameBufferMode = FB_NONE;

int score = 0;
int lives = START_LIVES;
int comboFlash = 0;
int comboValue = 0;
bool bombHit = false;
bool bombExploding = false;
float bombExplosionX = 0.0f;
float bombExplosionY = 0.0f;
uint32_t bombExplosionStartMs = 0;
uint32_t bombSparkMs = 0;

int leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int latestRank = -1;

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;

bool touchDown = false;
bool touchPressed = false;
bool touchReleased = false;
int touchX = 0;
int touchY = 0;
int prevTouchX = 0;
int prevTouchY = 0;

int trailX[10] = {0};
int trailY[10] = {0};
int trailLen = 0;

bool vibrating = false;
bool vibForward = true;
uint32_t vibEndMs = 0;
uint32_t vibLastToggleMs = 0;

uint32_t stateStartMs = 0;
uint32_t lastStepMs = 0;
uint32_t nextSpawnMs = 0;

uint16_t fruitSprites[FRUIT_SPRITE_COUNT][FRUIT_SPRITE_SIZE * FRUIT_SPRITE_SIZE];

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t lerp565(uint16_t c0, uint16_t c1, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  uint8_t r0 = (c0 >> 11) & 0x1F;
  uint8_t g0 = (c0 >> 5) & 0x3F;
  uint8_t b0 = c0 & 0x1F;
  uint8_t r1 = (c1 >> 11) & 0x1F;
  uint8_t g1 = (c1 >> 5) & 0x3F;
  uint8_t b1 = c1 & 0x1F;
  uint8_t r = (uint8_t)(r0 + (r1 - r0) * t);
  uint8_t g = (uint8_t)(g0 + (g1 - g0) * t);
  uint8_t b = (uint8_t)(b0 + (b1 - b0) * t);
  return (r << 11) | (g << 5) | b;
}

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void clearFruitSprite(int idx) {
  for (int i = 0; i < FRUIT_SPRITE_SIZE * FRUIT_SPRITE_SIZE; ++i) {
    fruitSprites[idx][i] = SPRITE_TRANSPARENT;
  }
}

void setFruitSpritePixel(int idx, int x, int y, uint16_t color) {
  if (idx < 0 || idx >= FRUIT_SPRITE_COUNT) return;
  if (x < 0 || x >= FRUIT_SPRITE_SIZE || y < 0 || y >= FRUIT_SPRITE_SIZE) return;
  fruitSprites[idx][y * FRUIT_SPRITE_SIZE + x] = color;
}

void buildFruitSprite(int idx, uint16_t baseColor, uint16_t rimColor, bool isBomb) {
  clearFruitSprite(idx);

  const float cx = (float)FRUIT_SPRITE_HALF;
  const float cy = (float)FRUIT_SPRITE_HALF;
  const float radius = 21.0f;

  for (int y = 0; y < FRUIT_SPRITE_SIZE; ++y) {
    for (int x = 0; x < FRUIT_SPRITE_SIZE; ++x) {
      float dx = (float)x - cx;
      float dy = (float)y - cy;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist > radius + 1.0f) continue;

      float edge = clampf(dist / radius, 0.0f, 1.0f);
      uint16_t color = lerp565(baseColor, rimColor, edge * edge);

      float shadowT = clampf((dx * 0.55f + dy * 0.95f + 11.0f) / 21.0f, 0.0f, 1.0f);
      if (shadowT > 0.0f) {
        color = lerp565(color, rgb565(8, 10, 16), shadowT * (isBomb ? 0.55f : 0.35f));
      }

      float hx = dx + 7.0f;
      float hy = dy + 8.0f;
      float hiDist = sqrtf(hx * hx + hy * hy);
      if (hiDist < 11.0f) {
        float gloss = (11.0f - hiDist) / 11.0f;
        color = lerp565(color, rgb565(255, 255, 255), gloss * (isBomb ? 0.22f : 0.55f));
      }

      if (dist > radius - 1.2f) {
        color = rimColor;
      }
      setFruitSpritePixel(idx, x, y, color);
    }
  }

  if (isBomb) {
    for (int y = -4; y <= 2; ++y) {
      for (int x = -4; x <= 4; ++x) {
        if (x * x + y * y > 18) continue;
        setFruitSpritePixel(idx, FRUIT_SPRITE_HALF + x, FRUIT_SPRITE_HALF + y, rgb565(182, 188, 198));
      }
    }
    for (int y = -2; y <= 6; ++y) {
      for (int x = -1; x <= 1; ++x) {
        setFruitSpritePixel(idx, FRUIT_SPRITE_HALF + x, FRUIT_SPRITE_HALF - 22 + y, rgb565(220, 182, 122));
      }
    }
    setFruitSpritePixel(idx, FRUIT_SPRITE_HALF + 3, FRUIT_SPRITE_HALF - 22, rgb565(255, 220, 126));
    setFruitSpritePixel(idx, FRUIT_SPRITE_HALF + 4, FRUIT_SPRITE_HALF - 23, rgb565(255, 164, 88));
    setFruitSpritePixel(idx, FRUIT_SPRITE_HALF + 5, FRUIT_SPRITE_HALF - 24, rgb565(255, 116, 72));
    return;
  }

  for (int y = -1; y <= 7; ++y) {
    for (int x = -1; x <= 1; ++x) {
      setFruitSpritePixel(idx, FRUIT_SPRITE_HALF + x, FRUIT_SPRITE_HALF - 23 + y, rgb565(122, 94, 56));
    }
  }

  int leafCx = FRUIT_SPRITE_HALF + 6;
  int leafCy = FRUIT_SPRITE_HALF - 23;
  for (int y = -4; y <= 4; ++y) {
    for (int x = -6; x <= 6; ++x) {
      float n = (float)(x * x) / 36.0f + (float)(y * y) / 12.0f;
      if (n > 1.0f) continue;
      uint16_t lc = lerp565(rgb565(68, 122, 54), rgb565(132, 214, 102), clampf((float)(x + 6) / 12.0f, 0.0f, 1.0f));
      setFruitSpritePixel(idx, leafCx + x, leafCy + y, lc);
    }
  }
}

void initFruitSprites() {
  buildFruitSprite(0, rgb565(242, 92, 84), rgb565(170, 36, 30), false);
  buildFruitSprite(1, rgb565(248, 212, 84), rgb565(190, 140, 38), false);
  buildFruitSprite(2, rgb565(126, 214, 98), rgb565(56, 126, 48), false);
  buildFruitSprite(3, rgb565(44, 48, 58), rgb565(130, 136, 148), true);
}

void stopMotor() {
  ledcWrite(FORWARD_CH, 0);
  ledcWrite(REVERSE_CH, 0);
}

void startVibration(uint16_t durationMs = 80) {
  vibrating = true;
  vibForward = true;
  vibEndMs = millis() + durationMs;
  vibLastToggleMs = 0;
}

void updateVibration() {
  if (!vibrating) {
    stopMotor();
    return;
  }

  uint32_t now = millis();
  if (now >= vibEndMs) {
    vibrating = false;
    stopMotor();
    return;
  }

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 20) {
    vibLastToggleMs = now;
    vibForward = !vibForward;
  }

  if (vibForward) {
    ledcWrite(FORWARD_CH, 165);
    ledcWrite(REVERSE_CH, 0);
  } else {
    ledcWrite(FORWARD_CH, 0);
    ledcWrite(REVERSE_CH, 165);
  }
}

void initFrameBuffer() {
  frameBufferMode = FB_NONE;
  frameBufferFull.deleteSprite();
  frameBufferLeft.deleteSprite();
  frameBufferRight.deleteSprite();

  frameBufferFull.setColorDepth(16);
  if (frameBufferFull.createSprite(SCREEN_W, SCREEN_H) != nullptr) {
    frameBufferMode = FB_FULL;
    return;
  }
  frameBufferFull.deleteSprite();

  frameBufferFull.setColorDepth(8);
  if (frameBufferFull.createSprite(SCREEN_W, SCREEN_H) != nullptr) {
    frameBufferMode = FB_FULL;
    return;
  }
  frameBufferFull.deleteSprite();

  frameBufferLeft.setColorDepth(16);
  frameBufferRight.setColorDepth(16);
  bool leftReady = (frameBufferLeft.createSprite(FRAME_W, SCREEN_H) != nullptr);
  bool rightReady = (frameBufferRight.createSprite(FRAME_W, SCREEN_H) != nullptr);
  if (leftReady && rightReady) {
    frameBufferMode = FB_SPLIT;
    return;
  }

  frameBufferLeft.deleteSprite();
  frameBufferRight.deleteSprite();
  frameBufferLeft.setColorDepth(8);
  frameBufferRight.setColorDepth(8);
  leftReady = (frameBufferLeft.createSprite(FRAME_W, SCREEN_H) != nullptr);
  rightReady = (frameBufferRight.createSprite(FRAME_W, SCREEN_H) != nullptr);
  if (leftReady && rightReady) {
    frameBufferMode = FB_SPLIT;
    return;
  }

  frameBufferLeft.deleteSprite();
  frameBufferRight.deleteSprite();
}

void clearObjects() {
  for (int i = 0; i < MAX_FRUITS; ++i) fruits[i].active = false;
  for (int i = 0; i < MAX_PARTICLES; ++i) particles[i].active = false;
}

void beginGame() {
  score = 0;
  lives = START_LIVES;
  comboFlash = 0;
  comboValue = 0;
  bombHit = false;
  bombExploding = false;
  bombExplosionX = 0.0f;
  bombExplosionY = 0.0f;
  bombExplosionStartMs = 0;
  bombSparkMs = 0;
  trailLen = 0;
  clearObjects();
  randomSeed((uint32_t)micros() ^ ((uint32_t)touchX << 9) ^ ((uint32_t)touchY << 3));
  nextSpawnMs = millis() + 350;
  gameState = STATE_PLAYING;
  stateStartMs = millis();
  lastStepMs = millis();
}

void submitScoreToLeaderboard() {
  int rank = LEADERBOARD_SIZE - 1;
  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    if (score >= leaderboard[i]) {
      rank = i;
      break;
    }
  }

  for (int i = LEADERBOARD_SIZE - 1; i > rank; --i) {
    leaderboard[i] = leaderboard[i - 1];
  }
  leaderboard[rank] = score;
  latestRank = rank;
}

void enterGameOver(bool causedByBomb) {
  bombHit = causedByBomb;
  submitScoreToLeaderboard();
  gameState = STATE_GAME_OVER;
  stateStartMs = millis();
  startVibration(causedByBomb ? 260 : 140);
}

void readInput() {
  bool newGreen = digitalRead(GREEN_BTN_PIN) == LOW;
  bool newBlue = digitalRead(BLUE_BTN_PIN) == LOW;
  greenPressed = newGreen && !greenDown;
  bluePressed = newBlue && !blueDown;
  greenDown = newGreen;
  blueDown = newBlue;

  tp.read();
  bool newTouchDown = tp.isTouched;
  touchPressed = newTouchDown && !touchDown;
  touchReleased = !newTouchDown && touchDown;

  if (touchDown && newTouchDown) {
    prevTouchX = touchX;
    prevTouchY = touchY;
  }

  touchDown = newTouchDown;
  if (touchDown) {
    touchX = tp.points[0].x;
    touchY = tp.points[0].y;
    if (touchPressed) {
      prevTouchX = touchX;
      prevTouchY = touchY;
      trailLen = 0;
    }
  }

  if (!touchDown && touchReleased) {
    trailLen = 0;
  }
}

void pushTrailPoint(int x, int y) {
  if (trailLen < 10) {
    trailX[trailLen] = x;
    trailY[trailLen] = y;
    trailLen++;
    return;
  }
  for (int i = 0; i < 9; ++i) {
    trailX[i] = trailX[i + 1];
    trailY[i] = trailY[i + 1];
  }
  trailX[9] = x;
  trailY[9] = y;
}

void spawnJuice(float x, float y, uint16_t baseColor, int count) {
  for (int i = 0; i < MAX_PARTICLES && count > 0; ++i) {
    if (particles[i].active) continue;
    particles[i].active = true;
    particles[i].x = x + (float)random(-14, 15);
    particles[i].y = y + (float)random(-12, 13);
    particles[i].vx = (float)random(-360, 361) * 0.01f;
    particles[i].vy = (float)random(-440, 20) * 0.01f;
    particles[i].life = (uint8_t)random(14, 28);
    particles[i].maxLife = particles[i].life;
    particles[i].size = (uint8_t)random(4, 9);
    particles[i].color = lerp565(baseColor, rgb565(255, 255, 255), (float)random(0, 40) / 100.0f);
    count--;
  }
}

bool hasActiveBomb() {
  for (int i = 0; i < MAX_FRUITS; ++i) {
    if (fruits[i].active && fruits[i].bomb) return true;
  }
  return false;
}

int activeFruitCount() {
  int count = 0;
  for (int i = 0; i < MAX_FRUITS; ++i) {
    if (fruits[i].active) count++;
  }
  return count;
}

void spawnFruitOne(bool allowBomb, bool forceBomb = false) {
  int slot = -1;
  for (int i = 0; i < MAX_FRUITS; ++i) {
    if (!fruits[i].active) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return;

  Fruit &f = fruits[slot];
  f.active = true;
  f.sliced = false;

  int bombChance = random(0, 100);
  f.bomb = forceBomb || (allowBomb && bombChance < 8);

  f.x = (float)random(48, SCREEN_W - 48);
  f.y = SCREEN_H + random(14, 28);
  f.r = 21.0f;

  float targetX = (float)random(80, SCREEN_W - 80);
  float flightFrames = (float)random(44, 62);
  f.vx = (targetX - f.x) / flightFrames;
  f.vy = -(float)random(86, 106) * 0.1f;

  if (f.bomb) {
    f.bodyColor = rgb565(36, 38, 44);
    f.rindColor = rgb565(120, 126, 136);
    f.style = 3;
  } else {
    f.style = (uint8_t)random(0, 3);
    if (f.style == 0) {
      f.bodyColor = rgb565(240, 82, 72);
      f.rindColor = rgb565(170, 36, 30);
    } else if (f.style == 1) {
      f.bodyColor = rgb565(246, 210, 72);
      f.rindColor = rgb565(186, 138, 36);
    } else {
      f.bodyColor = rgb565(120, 212, 92);
      f.rindColor = rgb565(52, 126, 46);
    }
  }
}

void scheduleNextSpawn() {
  uint32_t now = millis();
  uint32_t elapsed = now - stateStartMs;
  if (elapsed < 10000) {
    nextSpawnMs = now + (uint32_t)random(420, 760);
  } else {
    nextSpawnMs = now + (uint32_t)random(240, 520);
  }
}

void spawnWave() {
  uint32_t elapsed = millis() - stateStartMs;
  int maxOnScreen = (elapsed < 10000) ? 2 : 3;
  int active = activeFruitCount();
  int room = maxOnScreen - active;
  if (room <= 0) {
    scheduleNextSpawn();
    return;
  }

  int wave = random(1, 4);  // 1~3 fruits per spawn attempt
  if (wave > room) wave = room;

  bool spawnSingleBomb = false;
  int bombIndex = -1;

  if (elapsed >= 10000 && !hasActiveBomb() && random(0, 100) < 7) {
    spawnSingleBomb = true;
    bombIndex = random(0, wave);
  }

  for (int i = 0; i < wave; ++i) {
    bool forceBomb = spawnSingleBomb && (i == bombIndex);
    spawnFruitOne(false, forceBomb);
  }

  scheduleNextSpawn();
}

bool segmentHitsCircle(float x1, float y1, float x2, float y2, float cx, float cy, float r) {
  float vx = x2 - x1;
  float vy = y2 - y1;
  float wx = cx - x1;
  float wy = cy - y1;

  float len2 = vx * vx + vy * vy;
  if (len2 < 0.001f) {
    float dx = cx - x1;
    float dy = cy - y1;
    return (dx * dx + dy * dy) <= r * r;
  }

  float t = (wx * vx + wy * vy) / len2;
  t = clampf(t, 0.0f, 1.0f);

  float px = x1 + t * vx;
  float py = y1 + t * vy;
  float dx = cx - px;
  float dy = cy - py;
  return (dx * dx + dy * dy) <= r * r;
}

void updateParticles() {
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    if (!particles[i].active) continue;
    particles[i].x += particles[i].vx;
    particles[i].y += particles[i].vy;
    particles[i].vy += 0.11f;
    particles[i].vx *= 0.985f;

    if (particles[i].life > 0) particles[i].life--;
    if (particles[i].life == 0) particles[i].active = false;
  }
}

void onFruitMissed() {
  lives--;
  // Miss feedback should be obvious and independent from hit vibrations.
  startVibration(180);
}

void updateFruitsPhysics() {
  for (int i = 0; i < MAX_FRUITS; ++i) {
    if (!fruits[i].active) continue;

    Fruit &f = fruits[i];
    f.vy += GRAVITY;
    f.x += f.vx;
    f.y += f.vy;

    if (f.x < -40.0f || f.x > SCREEN_W + 40.0f || f.y > SCREEN_H + 42.0f) {
      if (!f.sliced && !f.bomb) {
        onFruitMissed();
        if (lives <= 0) {
          f.active = false;
          enterGameOver(false);
          return;
        }
      }
      f.active = false;
    }
  }
}

void handleSliceHit(Fruit &f) {
  if (f.sliced) return;
  f.sliced = true;

  if (f.bomb) {
    bombExploding = true;
    bombExplosionStartMs = millis();
    bombSparkMs = bombExplosionStartMs;
    bombExplosionX = f.x;
    bombExplosionY = f.y;
    f.active = false;
    for (int i = 0; i < MAX_FRUITS; ++i) {
      fruits[i].active = false;
    }
    spawnJuice(bombExplosionX, bombExplosionY, rgb565(230, 188, 96), 32);
    spawnJuice(bombExplosionX, bombExplosionY, rgb565(216, 220, 234), 28);
    startVibration(320);
    return;
  }

  f.active = false;
  score += 10;
  comboValue += 1;
  startVibration(55);
  spawnJuice(f.x, f.y, f.bodyColor, 14);
}

void processSlice() {
  if (!touchDown) return;

  pushTrailPoint(touchX, touchY);
  float x1 = (float)prevTouchX;
  float y1 = (float)prevTouchY;
  float x2 = (float)touchX;
  float y2 = (float)touchY;

  float move = fabsf(x2 - x1) + fabsf(y2 - y1);
  if (move < 6.0f) return;

  int slicedThisSegment = 0;
  for (int i = 0; i < MAX_FRUITS; ++i) {
    if (!fruits[i].active || fruits[i].sliced) continue;
    if (!segmentHitsCircle(x1, y1, x2, y2, fruits[i].x, fruits[i].y, fruits[i].r + 3.0f)) continue;
    handleSliceHit(fruits[i]);
    if (gameState != STATE_PLAYING) return;
    if (!fruits[i].bomb) slicedThisSegment++;
  }

  if (slicedThisSegment >= 2) {
    int bonus = (slicedThisSegment - 1) * 5;
    score += bonus;
    comboFlash = 22;
    comboValue = slicedThisSegment;
    startVibration(78);
  }
}

void updatePlaying() {
  if (greenPressed) {
    gameState = STATE_TITLE;
    stateStartMs = millis();
    return;
  }

  if (bombExploding) {
    uint32_t now = millis();
    if (now - bombSparkMs >= 90) {
      bombSparkMs = now;
      spawnJuice(bombExplosionX + (float)random(-8, 9), bombExplosionY + (float)random(-8, 9), rgb565(255, 162, 92), 8);
    }
    updateParticles();
    if (now - bombExplosionStartMs >= BOMB_EXPLOSION_MS) {
      bombExploding = false;
      enterGameOver(true);
    }
    return;
  }

  if (millis() >= nextSpawnMs) {
    spawnWave();
  }

  processSlice();
  if (gameState != STATE_PLAYING) return;

  updateFruitsPhysics();
  if (gameState != STATE_PLAYING) return;

  updateParticles();

  if (comboFlash > 0) comboFlash--;
  if (touchReleased) comboValue = 0;
}

template <typename CanvasT>
void drawBackground(CanvasT &canvas) {
  canvas.fillRect(0, 0, SCREEN_W, 96, rgb565(16, 28, 54));
  canvas.fillRect(0, 96, SCREEN_W, 128, rgb565(24, 50, 86));
  canvas.fillRect(0, 224, SCREEN_W, SCREEN_H - 224, rgb565(22, 26, 38));
  for (int y = 24; y < SCREEN_H; y += 48) {
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(34, 66, 102));
  }
}

void drawFruit(WindowCanvas &canvas, const Fruit &f) {
  int spriteIdx = f.bomb ? 3 : (int)f.style;
  if (spriteIdx < 0 || spriteIdx >= FRUIT_SPRITE_COUNT) spriteIdx = 0;
  int x = (int)f.x - FRUIT_SPRITE_HALF;
  int y = (int)f.y - FRUIT_SPRITE_HALF;
  canvas.pushImage(x, y, FRUIT_SPRITE_SIZE, FRUIT_SPRITE_SIZE, fruitSprites[spriteIdx], SPRITE_TRANSPARENT);
}

template <typename CanvasT>
void drawFruit(CanvasT &canvas, const Fruit &f) {
  int x = (int)f.x;
  int y = (int)f.y;
  int r = (int)f.r;
  uint16_t base = f.bomb ? rgb565(48, 54, 62) : f.bodyColor;
  uint16_t rim = f.bomb ? rgb565(130, 136, 148) : f.rindColor;
  canvas.fillCircle(x, y, r, base);
  canvas.drawCircle(x, y, r, rim);
  canvas.fillCircle(x - r / 3, y - r / 3, r / 3, lerp565(base, rgb565(255, 255, 255), f.bomb ? 0.18f : 0.45f));
}

template <typename CanvasT>
void drawParticles(CanvasT &canvas) {
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    if (!particles[i].active) continue;
    float t = (float)particles[i].life / (float)particles[i].maxLife;
    uint16_t c = lerp565(rgb565(18, 20, 28), particles[i].color, t);
    canvas.fillRect((int)particles[i].x, (int)particles[i].y, particles[i].size, particles[i].size, c);
  }
}

template <typename CanvasT>
void drawTrail(CanvasT &canvas) {
  if (trailLen < 2) return;
  for (int i = 1; i < trailLen; ++i) {
    float t = (float)i / (float)(trailLen - 1);
    uint16_t c = lerp565(rgb565(72, 218, 242), rgb565(222, 255, 255), t);
    canvas.drawLine(trailX[i - 1], trailY[i - 1], trailX[i], trailY[i], c);
    if (i == trailLen - 1) {
      canvas.fillCircle(trailX[i], trailY[i], 3, rgb565(226, 252, 255));
    }
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.fillRoundRect(10, 8, 170, 34, 8, rgb565(22, 34, 58));
  canvas.setTextColor(rgb565(232, 248, 255), rgb565(22, 34, 58));
  char buf[40];
  snprintf(buf, sizeof(buf), "SCORE %d", score);
  canvas.drawString(buf, 20, 18, 2);

  uint16_t missBg = rgb565(44, 24, 42);
  canvas.fillRoundRect(190, 8, 126, 34, 8, missBg);
  canvas.setTextColor(rgb565(255, 220, 234), missBg);
  canvas.drawString("MISS", 202, 18, 2);

  int missCount = START_LIVES - lives;
  if (missCount < 0) missCount = 0;
  if (missCount > START_LIVES) missCount = START_LIVES;
  for (int i = 0; i < START_LIVES; ++i) {
    int cx = 258 + i * 16;
    int cy = 24;
    bool activeMiss = (i < missCount);
    uint16_t c = activeMiss
      ? rgb565(255, 78, 78)
      : rgb565(112, 78, 86);

    canvas.drawLine(cx - 5, cy - 5, cx + 5, cy + 5, c);
    canvas.drawLine(cx - 5, cy - 4, cx + 5, cy + 6, c);
    canvas.drawLine(cx - 5, cy - 6, cx + 5, cy + 4, c);
    canvas.drawLine(cx + 5, cy - 5, cx - 5, cy + 5, c);
    canvas.drawLine(cx + 5, cy - 4, cx - 5, cy + 6, c);
    canvas.drawLine(cx + 5, cy - 6, cx - 5, cy + 4, c);
  }

  canvas.fillRoundRect(326, 8, 144, 34, 8, rgb565(32, 58, 42));
  if (comboFlash > 0 && comboValue >= 2) {
    canvas.setTextColor(rgb565(255, 246, 166), rgb565(32, 58, 42));
    snprintf(buf, sizeof(buf), "COMBO x%d", comboValue);
  } else {
    canvas.setTextColor(rgb565(198, 236, 206), rgb565(32, 58, 42));
    snprintf(buf, sizeof(buf), "NINJA MODE");
  }
  canvas.drawString(buf, 338, 18, 2);
}

template <typename CanvasT>
void drawBombExplosion(CanvasT &canvas) {
  if (!bombExploding) return;
  uint32_t now = millis();
  float t = (float)(now - bombExplosionStartMs) / (float)BOMB_EXPLOSION_MS;
  t = clampf(t, 0.0f, 1.0f);

  int cx = (int)bombExplosionX;
  int cy = (int)bombExplosionY;
  int r0 = 18 + (int)(t * 108.0f);
  int r1 = 8 + (int)(t * 72.0f);
  int r2 = 26 + (int)(t * 136.0f);

  canvas.fillCircle(cx, cy, r0, rgb565(255, 122, 64));
  canvas.fillCircle(cx, cy, r1, rgb565(255, 222, 124));
  canvas.drawCircle(cx, cy, r2, rgb565(255, 236, 186));

  uint16_t warnBg = rgb565(54, 20, 20);
  canvas.fillRoundRect(SCREEN_W / 2 - 96, 52, 192, 28, 8, warnBg);
  canvas.setTextColor(rgb565(255, 228, 190), warnBg);
  canvas.drawCentreString("BOMB EXPLODING!", SCREEN_W / 2, 60, 2);
}

template <typename CanvasT>
void drawPlaying(CanvasT &canvas) {
  drawBackground(canvas);
  drawParticles(canvas);
  for (int i = 0; i < MAX_FRUITS; ++i) {
    if (!fruits[i].active) continue;
    drawFruit(canvas, fruits[i]);
  }
  drawTrail(canvas);
  drawBombExplosion(canvas);
  drawHud(canvas);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  drawBackground(canvas);

  canvas.fillRoundRect(56, 36, SCREEN_W - 112, 228, 16, rgb565(24, 36, 62));
  canvas.drawRoundRect(56, 36, SCREEN_W - 112, 228, 16, rgb565(122, 178, 252));

  canvas.setTextColor(rgb565(255, 208, 122), rgb565(24, 36, 62));
  canvas.drawCentreString("FRUIT NINJA", SCREEN_W / 2, 70, 4);
  canvas.setTextColor(rgb565(220, 242, 255), rgb565(24, 36, 62));
  canvas.drawCentreString("Factory Arcade Edition", SCREEN_W / 2, 108, 2);

  canvas.drawCentreString("Touch and swipe to slice fruits", SCREEN_W / 2, 144, 2);
  canvas.drawCentreString("Avoid bombs, miss 3 fruits = game over", SCREEN_W / 2, 168, 2);
  canvas.drawCentreString("Green: quit round", SCREEN_W / 2, 192, 2);

  int fruitX = 118;
  int fruitY = 198;
  canvas.fillCircle(fruitX + 5, fruitY + 10, 22, rgb565(20, 24, 34));
  canvas.fillCircle(fruitX, fruitY, 20, rgb565(194, 224, 82));
  canvas.fillCircle(fruitX, fruitY, 16, rgb565(248, 92, 96));
  canvas.fillCircle(fruitX - 6, fruitY - 8, 5, rgb565(255, 172, 176));
  canvas.fillCircle(fruitX - 5, fruitY - 7, 2, rgb565(255, 224, 226));
  canvas.fillRect(fruitX - 2, fruitY - 25, 4, 8, rgb565(98, 70, 40));
  canvas.fillRoundRect(fruitX + 1, fruitY - 27, 8, 4, 2, rgb565(92, 184, 92));
  canvas.drawLine(fruitX - 12, fruitY - 4, fruitX - 2, fruitY + 6, rgb565(80, 22, 30));
  canvas.drawLine(fruitX + 3, fruitY - 6, fruitX + 11, fruitY + 7, rgb565(80, 22, 30));

  int citrusX = 362;
  int citrusY = 198;
  canvas.fillCircle(citrusX + 4, citrusY + 9, 20, rgb565(22, 24, 34));
  canvas.fillCircle(citrusX, citrusY, 18, rgb565(255, 174, 54));
  canvas.fillCircle(citrusX, citrusY, 13, rgb565(255, 216, 120));
  for (int i = 0; i < 6; ++i) {
    int x0 = citrusX;
    int y0 = citrusY;
    int x1 = citrusX + (int)(cosf(i * 1.0472f) * 11.0f);
    int y1 = citrusY + (int)(sinf(i * 1.0472f) * 11.0f);
    canvas.drawLine(x0, y0, x1, y1, rgb565(250, 186, 86));
  }

  uint16_t blink = ((millis() / 450) % 2) ? rgb565(255, 250, 180) : rgb565(116, 126, 88);
  canvas.setTextColor(blink, rgb565(24, 36, 62));
  canvas.drawCentreString("TOUCH OR BLUE TO START", SCREEN_W / 2, 228, 2);
}

template <typename CanvasT>
void drawGameOver(CanvasT &canvas) {
  uint16_t bg = bombHit ? rgb565(42, 16, 18) : rgb565(20, 32, 26);
  canvas.fillRect(0, 0, SCREEN_W / 2, SCREEN_H, bg);
  canvas.fillRect(SCREEN_W / 2, 0, SCREEN_W - SCREEN_W / 2, SCREEN_H, bg);

  canvas.setTextColor(bombHit ? rgb565(255, 146, 138) : rgb565(186, 255, 196), bg);
  canvas.drawCentreString(bombHit ? "BOMB HIT" : "GAME OVER", SCREEN_W / 2, 108, 4);

  canvas.setTextColor(TFT_WHITE, bg);
  char line[64];
  snprintf(line, sizeof(line), "SCORE %d", score);
  canvas.drawCentreString(line, SCREEN_W / 2, 156, 2);
  canvas.drawCentreString("Entering leaderboard...", SCREEN_W / 2, 184, 2);
}

template <typename CanvasT>
void drawLeaderboard(CanvasT &canvas) {
  uint16_t bg = rgb565(16, 20, 34);
  uint16_t panel = rgb565(30, 38, 58);
  canvas.fillScreen(bg);

  canvas.fillRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, panel);
  canvas.drawRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, rgb565(124, 164, 232));

  canvas.setTextColor(rgb565(228, 242, 255), panel);
  canvas.drawCentreString("LEADERBOARD", SCREEN_W / 2, 42, 4);

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    bool isLatest = (i == latestRank);
    int y = 92 + i * 36;
    uint16_t rowBg = isLatest ? rgb565(86, 66, 24) : rgb565(40, 50, 80);
    uint16_t textColor = isLatest ? rgb565(255, 236, 160) : rgb565(214, 226, 252);

    canvas.fillRoundRect(92, y - 8, 296, 24, 6, rowBg);
    canvas.setTextColor(textColor, rowBg);

    char line[48];
    snprintf(line, sizeof(line), "%d. %d", i + 1, leaderboard[i]);
    canvas.drawString(line, 122, y, 2);

    if (isLatest) {
      canvas.drawString("< NOW", 300, y, 2);
    }
  }

  canvas.setTextColor(rgb565(172, 192, 224), panel);
  canvas.drawCentreString("Blue: back to title", SCREEN_W / 2, SCREEN_H - 42, 2);
}

void renderFrame() {
  if (frameBufferMode == FB_FULL) {
    WindowCanvas whole = {&frameBufferFull, 0};
    if (gameState == STATE_TITLE) {
      drawTitle(whole);
    } else if (gameState == STATE_PLAYING) {
      drawPlaying(whole);
    } else if (gameState == STATE_GAME_OVER) {
      drawGameOver(whole);
    } else {
      drawLeaderboard(whole);
    }

    frameBufferFull.pushSprite(0, 0);
    return;
  }

  if (frameBufferMode == FB_SPLIT) {
    WindowCanvas left = {&frameBufferLeft, 0};
    WindowCanvas right = {&frameBufferRight, FRAME_W};

    if (gameState == STATE_TITLE) {
      drawTitle(left);
      drawTitle(right);
    } else if (gameState == STATE_PLAYING) {
      drawPlaying(left);
      drawPlaying(right);
    } else if (gameState == STATE_GAME_OVER) {
      drawGameOver(left);
      drawGameOver(right);
    } else {
      drawLeaderboard(left);
      drawLeaderboard(right);
    }

    tft.startWrite();
    frameBufferLeft.pushSprite(0, 0);
    frameBufferRight.pushSprite(FRAME_W, 0);
    tft.endWrite();
    return;
  }

  if (gameState == STATE_TITLE) {
    drawTitle(tft);
  } else if (gameState == STATE_PLAYING) {
    drawPlaying(tft);
  } else if (gameState == STATE_GAME_OVER) {
    drawGameOver(tft);
  } else {
    drawLeaderboard(tft);
  }
}

void setup() {
  pinMode(GREEN_BTN_PIN, INPUT_PULLUP);
  pinMode(BLUE_BTN_PIN, INPUT_PULLUP);

  ledcSetup(REVERSE_CH, MOTOR_FREQUENCY, MOTOR_RESOLUTION);
  ledcSetup(FORWARD_CH, MOTOR_FREQUENCY, MOTOR_RESOLUTION);
  ledcAttachPin(REVERSE_PIN, REVERSE_CH);
  ledcAttachPin(FORWARD_PIN, FORWARD_CH);
  stopMotor();

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  tp.begin();
  tp.setRotation(ROTATION_RIGHT);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.setSwapBytes(false);
  tft.fillScreen(TFT_BLACK);

  initFruitSprites();
  initFrameBuffer();
  clearObjects();

  gameState = STATE_TITLE;
  stateStartMs = millis();
  lastStepMs = millis();
}

void loop() {
  readInput();
  updateVibration();

  if (gameState == STATE_TITLE) {
    if (touchPressed || bluePressed) {
      beginGame();
    }
  } else if (gameState == STATE_PLAYING) {
    uint32_t now = millis();
    int guard = 0;
    while ((now - lastStepMs) >= FRAME_MS && guard < 4) {
      lastStepMs += FRAME_MS;
      updatePlaying();
      guard++;
      if (gameState != STATE_PLAYING) break;
    }
  } else if (gameState == STATE_GAME_OVER) {
    if (millis() - stateStartMs >= 1100) {
      gameState = STATE_LEADERBOARD;
      stateStartMs = millis();
    }
  } else if (gameState == STATE_LEADERBOARD) {
    if (bluePressed || touchPressed) {
      gameState = STATE_TITLE;
      stateStartMs = millis();
    }
  }

  renderFrame();
  delay(1);
}
