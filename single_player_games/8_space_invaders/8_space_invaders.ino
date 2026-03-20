#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

const int x_pin = 1;
const int y_pin = 2;
const int green_btn_pin = 11;
const int blue_btn_pin = 12;

const int green_led_pin = 47;
const int blue_led_pin = 48;

const int reverse_pin = 45;
const int forward_pin = 46;
const int motor_frequency = 15000;
const int reverse_ch = 0;
const int forward_ch = 1;
const int motor_resolution = 8;

#define MAX98357_LRC 13
#define MAX98357_BCLK 14
#define MAX98357_DIN 4
#define SAMPLE_RATE 16000
#define AUDIO_CHUNK_SAMPLES 256

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBuffer = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int HUD_H = 24;

static const int PLAYER_PIXEL_W = 12;
static const int PLAYER_PIXEL_H = 9;
static const int PLAYER_PIXEL_SCALE = 2;
static const int PLAYER_W = PLAYER_PIXEL_W * PLAYER_PIXEL_SCALE;
static const int PLAYER_H = PLAYER_PIXEL_H * PLAYER_PIXEL_SCALE;
static const int PLAYER_Y = SCREEN_H - 12;

static const int ALIEN_COLS = 8;
static const int ALIEN_ROWS = 4;
static const int ALIEN_W = 20;
static const int ALIEN_H = 12;
static const int ALIEN_GAP_X = 12;
static const int ALIEN_GAP_Y = 14;
static const int ALIEN_PIXEL_W = 10;
static const int ALIEN_PIXEL_H = 6;
static const int ALIEN_PIXEL_SCALE = 2;

static const int MAX_PLAYER_BULLETS = 12;
static const int MAX_ENEMY_BULLETS = 6;

enum GameState {
  TITLE,
  PLAYING,
  GAME_OVER
};

struct Bullet {
  int x;
  int y;
  int vy;
  bool active;
};

struct RenderBullet {
  int x;
  int y;
  bool active;
};

struct SpriteFrame {
  uint8_t width;
  uint8_t height;
  const uint8_t *pixels;  // Row-major, color index 0 means transparent
};

struct SpriteAnimation {
  const SpriteFrame *frames;
  uint8_t frameCount;
  uint16_t intervalMs;
};

struct SpriteInstance {
  int x;
  int yBottom;
  const SpriteAnimation *animation;
  uint8_t frameIndex;
  unsigned long lastFrameMs;
};

struct Voice {
  bool active;
  float freq;
  float phase;
  int remainSamples;
  float volume;
  uint8_t wave;  // 0 square, 1 sine, 2 noise
};

struct Note {
  uint16_t freq;
  uint16_t ms;
};

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;

bool aliens[ALIEN_ROWS][ALIEN_COLS];
int formationX = 54;
int formationY = 44;
int alienDir = 1;
unsigned long lastAlienMoveMs = 0;
unsigned long alienMoveIntervalMs = 320;

Bullet playerBullets[MAX_PLAYER_BULLETS];
Bullet enemyBullets[MAX_ENEMY_BULLETS];

int playerX = SCREEN_W / 2;
int score = 0;
int lives = 3;
int level = 1;
unsigned long lastShotMs = 0;
unsigned long shootCooldownMs = 220;
int playerBulletCapacity = 3;
unsigned long invincibleUntil = 0;
unsigned long lastEnemyShotMs = 0;
unsigned long enemyShotIntervalMs = 850;
unsigned long lastGameUpdateMs = 0;
const uint16_t GAME_UPDATE_INTERVAL_MS = 12;

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
bool blueLatch = false;

// Hit vibration state
bool vibrating = false;
bool vibForward = true;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;

// Audio engine state
const i2s_config_t i2sOut_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 512
};

const i2s_pin_config_t i2sOut_pin_config = {
  .bck_io_num = MAX98357_BCLK,
  .ws_io_num = MAX98357_LRC,
  .data_out_num = MAX98357_DIN,
  .data_in_num = -1
};

Voice sfxVoices[3];
float bgmPhase = 0.0f;
int bgmIndex = 0;
int bgmRemainSamples = 0;

const Note bgmLoop[] = {
  {262, 140}, {330, 140}, {392, 140}, {330, 140},
  {440, 140}, {392, 140}, {330, 140}, {262, 140},
  {196, 180}, {247, 180}, {262, 180}, {330, 180}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

bool bgmEnabled = true;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
TaskHandle_t audioTaskHandle = nullptr;

int prevPlayerX = -1;
RenderBullet prevPlayerBullets[MAX_PLAYER_BULLETS];
RenderBullet prevEnemyBullets[MAX_ENEMY_BULLETS];
int prevFormationX = 0;
int prevFormationY = 0;
bool prevAliens[ALIEN_ROWS][ALIEN_COLS];
bool prevAliensValid = false;
int lastHudScore = -1;
int lastHudLives = -1;
int lastHudLevel = -1;
bool frameBufferReady = false;

enum SpriteColorIndex : uint8_t {
  SPRITE_TRANSPARENT = 0,
  SPRITE_BODY = 1,
  SPRITE_CANOPY = 2,
  SPRITE_ENGINE = 3,
  SPRITE_ACCENT = 4
};

const uint8_t playerShipFrame0[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
  0, 0, 1, 1, 1, 2, 2, 1, 1, 1, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0,
  0, 0, 1, 0, 3, 0, 0, 3, 0, 1, 0, 0,
  0, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0
};

const uint8_t playerShipFrame1[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
  0, 0, 1, 1, 1, 2, 2, 1, 1, 1, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0,
  0, 0, 1, 0, 0, 3, 3, 0, 0, 1, 0, 0,
  0, 0, 0, 0, 3, 0, 0, 3, 0, 0, 0, 0
};

const SpriteFrame playerShipFrames[] = {
  {PLAYER_PIXEL_W, PLAYER_PIXEL_H, playerShipFrame0},
  {PLAYER_PIXEL_W, PLAYER_PIXEL_H, playerShipFrame1}
};

const SpriteAnimation playerShipAnim = {
  playerShipFrames,
  2,
  80
};

const uint8_t dragonFrame0[ALIEN_PIXEL_W * ALIEN_PIXEL_H] = {
  0, 4, 0, 0, 1, 1, 0, 0, 4, 0,
  4, 4, 1, 1, 1, 1, 1, 1, 4, 4,
  0, 1, 1, 2, 3, 3, 2, 1, 1, 0,
  0, 1, 1, 2, 2, 2, 2, 1, 1, 0,
  0, 0, 1, 1, 1, 1, 1, 1, 0, 0,
  0, 0, 0, 4, 0, 0, 4, 0, 0, 0
};

const uint8_t dragonFrame1[ALIEN_PIXEL_W * ALIEN_PIXEL_H] = {
  0, 0, 4, 0, 1, 1, 0, 4, 0, 0,
  0, 4, 4, 1, 1, 1, 1, 4, 4, 0,
  4, 1, 1, 2, 3, 3, 2, 1, 1, 4,
  0, 1, 1, 2, 2, 2, 2, 1, 1, 0,
  0, 0, 1, 1, 1, 1, 1, 1, 0, 0,
  0, 0, 4, 0, 0, 0, 0, 4, 0, 0
};

const uint8_t dragonFrame2[ALIEN_PIXEL_W * ALIEN_PIXEL_H] = {
  0, 0, 0, 4, 1, 1, 4, 0, 0, 0,
  0, 0, 4, 1, 1, 1, 1, 4, 0, 0,
  0, 4, 1, 2, 3, 3, 2, 1, 4, 0,
  0, 1, 1, 2, 2, 2, 2, 1, 1, 0,
  0, 0, 1, 1, 1, 1, 1, 1, 0, 0,
  0, 0, 0, 4, 0, 0, 4, 0, 0, 0
};

const uint8_t dragonFrame3[ALIEN_PIXEL_W * ALIEN_PIXEL_H] = {
  0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
  0, 4, 0, 1, 1, 1, 1, 0, 4, 0,
  4, 1, 1, 2, 3, 3, 2, 1, 1, 4,
  0, 1, 1, 2, 2, 2, 2, 1, 1, 0,
  0, 0, 1, 1, 1, 1, 1, 1, 0, 0,
  0, 4, 0, 0, 0, 0, 0, 0, 4, 0
};

const uint8_t dragonFrame4[ALIEN_PIXEL_W * ALIEN_PIXEL_H] = {
  0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 1, 1, 1, 0, 0, 0,
  4, 0, 1, 2, 3, 3, 2, 1, 0, 4,
  4, 1, 1, 2, 2, 2, 2, 1, 1, 4,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  0, 0, 4, 0, 0, 0, 0, 4, 0, 0
};

const SpriteFrame dragonFrames[] = {
  {ALIEN_PIXEL_W, ALIEN_PIXEL_H, dragonFrame0},
  {ALIEN_PIXEL_W, ALIEN_PIXEL_H, dragonFrame1},
  {ALIEN_PIXEL_W, ALIEN_PIXEL_H, dragonFrame2},
  {ALIEN_PIXEL_W, ALIEN_PIXEL_H, dragonFrame3},
  {ALIEN_PIXEL_W, ALIEN_PIXEL_H, dragonFrame4}
};

const SpriteAnimation dragonAnim = {
  dragonFrames,
  5,
  85
};

SpriteInstance playerSprite = {
  SCREEN_W / 2,
  PLAYER_Y,
  &playerShipAnim,
  0,
  0
};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t shade565(uint16_t color, uint8_t pct) {
  uint8_t r = (((color >> 11) & 0x1F) * 255) / 31;
  uint8_t g = (((color >> 5) & 0x3F) * 255) / 63;
  uint8_t b = ((color & 0x1F) * 255) / 31;
  r = (uint8_t)((r * pct) / 100);
  g = (uint8_t)((g * pct) / 100);
  b = (uint8_t)((b * pct) / 100);
  return rgb565(r, g, b);
}

uint16_t tint565(uint16_t color, uint16_t tint, uint8_t tintPct) {
  uint8_t r = (((color >> 11) & 0x1F) * 255) / 31;
  uint8_t g = (((color >> 5) & 0x3F) * 255) / 63;
  uint8_t b = ((color & 0x1F) * 255) / 31;

  uint8_t tr = (((tint >> 11) & 0x1F) * 255) / 31;
  uint8_t tg = (((tint >> 5) & 0x3F) * 255) / 63;
  uint8_t tb = ((tint & 0x1F) * 255) / 31;

  uint8_t keepPct = 100 - tintPct;
  r = (uint8_t)((r * keepPct + tr * tintPct) / 100);
  g = (uint8_t)((g * keepPct + tg * tintPct) / 100);
  b = (uint8_t)((b * keepPct + tb * tintPct) / 100);
  return rgb565(r, g, b);
}

uint16_t alienBaseColor(int row, int col) {
  int idx = row * ALIEN_COLS + col + 1;
  uint8_t r = (uint8_t)(50 + ((idx * 67) % 180));
  uint8_t g = (uint8_t)(60 + ((idx * 97) % 170));
  uint8_t b = (uint8_t)(70 + ((idx * 53) % 160));
  return rgb565(r, g, b);
}

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startHitVibration(uint16_t durationMs = 220) {
  vibrating = true;
  vibForward = true;
  vibEndMs = millis() + durationMs;
  vibLastToggleMs = 0;
}

void updateVibration() {
  if (!vibrating) {
    return;
  }

  unsigned long now = millis();
  if (now >= vibEndMs) {
    vibrating = false;
    stopMotor();
    return;
  }

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 35) {
    vibLastToggleMs = now;
    vibForward = !vibForward;
    const int duty = 170;
    if (vibForward) {
      ledcWrite(forward_ch, duty);
      ledcWrite(reverse_ch, 0);
    } else {
      ledcWrite(forward_ch, 0);
      ledcWrite(reverse_ch, duty);
    }
  }
}

void resetBullets() {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    playerBullets[i].active = false;
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    enemyBullets[i].active = false;
  }
}

void initAliens() {
  for (int r = 0; r < ALIEN_ROWS; r++) {
    for (int c = 0; c < ALIEN_COLS; c++) {
      aliens[r][c] = true;
    }
  }
  formationX = 54;
  formationY = 44;
  alienDir = 1;
}

void resetRenderCache() {
  prevPlayerX = -1;
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    prevPlayerBullets[i].active = false;
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    prevEnemyBullets[i].active = false;
  }
  prevAliensValid = false;
  lastHudScore = -1;
  lastHudLives = -1;
  lastHudLevel = -1;
}

void beginGame() {
  score = 0;
  lives = 3;
  level = 1;
  playerX = SCREEN_W / 2;
  playerSprite.x = playerX;
  playerSprite.yBottom = PLAYER_Y;
  playerSprite.frameIndex = 0;
  playerSprite.lastFrameMs = millis();
  shootCooldownMs = 95;
  playerBulletCapacity = 3;
  alienMoveIntervalMs = 180;
  enemyShotIntervalMs = 430;
  invincibleUntil = 0;
  resetBullets();
  initAliens();
  lastAlienMoveMs = millis();
  lastEnemyShotMs = millis();
  lastGameUpdateMs = millis();
  gameState = PLAYING;
  resetRenderCache();
  stopMotor();
}

void nextWave() {
  level++;
  if (playerBulletCapacity < MAX_PLAYER_BULLETS) {
    playerBulletCapacity++;
  }
  if (alienMoveIntervalMs > 60) {
    alienMoveIntervalMs -= 56;
  }
  if (enemyShotIntervalMs > 140) {
    enemyShotIntervalMs -= 80;
  }
  if (shootCooldownMs > 60) {
    shootCooldownMs -= 20;
  }
  resetBullets();
  initAliens();
  resetRenderCache();
}

bool allAliensDead() {
  for (int r = 0; r < ALIEN_ROWS; r++) {
    for (int c = 0; c < ALIEN_COLS; c++) {
      if (aliens[r][c]) {
        return false;
      }
    }
  }
  return true;
}

void startVoice(float freq, int ms, float volume, uint8_t wave) {
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  for (int i = 0; i < 3; i++) {
    if (!sfxVoices[i].active) {
      sfxVoices[i].active = true;
      sfxVoices[i].freq = freq;
      sfxVoices[i].phase = 0.0f;
      sfxVoices[i].remainSamples = durationSamples;
      sfxVoices[i].volume = volume;
      sfxVoices[i].wave = wave;
      return;
    }
  }
  sfxVoices[0].active = true;
  sfxVoices[0].freq = freq;
  sfxVoices[0].phase = 0.0f;
  sfxVoices[0].remainSamples = durationSamples;
  sfxVoices[0].volume = volume;
  sfxVoices[0].wave = wave;
}

void playSfxShoot() {
  startVoice(880.0f, 70, 0.10f, 0);
}

void playSfxExplosion() {
  startVoice(130.0f, 120, 0.16f, 2);
  startVoice(220.0f, 120, 0.12f, 2);
}

void playSfxHit() {
  startVoice(140.0f, 180, 0.18f, 2);
}

void playSfxGameOver() {
  startVoice(392.0f, 120, 0.12f, 0);
  startVoice(294.0f, 150, 0.12f, 0);
  startVoice(196.0f, 260, 0.14f, 0);
}

float waveSample(Voice &v) {
  float s = 0.0f;
  if (v.wave == 0) {
    s = (v.phase < 0.5f) ? 1.0f : -1.0f;
  } else if (v.wave == 1) {
    s = sinf(v.phase * 2.0f * PI);
  } else {
    s = ((float)random(-1000, 1000)) / 1000.0f;
  }

  v.phase += v.freq / SAMPLE_RATE;
  if (v.phase >= 1.0f) {
    v.phase -= 1.0f;
  }

  return s * v.volume;
}

float bgmSample() {
  if (!bgmEnabled || gameState != PLAYING) {
    return 0.0f;
  }

  if (bgmRemainSamples <= 0) {
    bgmIndex = (bgmIndex + 1) % BGM_NOTE_COUNT;
    bgmRemainSamples = (SAMPLE_RATE * bgmLoop[bgmIndex].ms) / 1000;
  }

  float freq = (float)bgmLoop[bgmIndex].freq;
  float s = (bgmPhase < 0.5f) ? 0.026f : -0.026f;
  bgmPhase += freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  bgmRemainSamples--;
  return s;
}

void audioTick() {
  const int n = AUDIO_CHUNK_SAMPLES;
  for (int i = 0; i < n; i++) {
    float mix = bgmSample();
    for (int v = 0; v < 3; v++) {
      if (!sfxVoices[v].active) {
        continue;
      }
      mix += waveSample(sfxVoices[v]);
      sfxVoices[v].remainSamples--;
      if (sfxVoices[v].remainSamples <= 0) {
        sfxVoices[v].active = false;
      }
    }

    if (mix > 0.65f) {
      mix = 0.65f;
    }
    if (mix < -0.65f) {
      mix = -0.65f;
    }
    audioBuffer[i] = (int16_t)(mix * 32767.0f);
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_NUM_1, (const char *)audioBuffer, n * sizeof(int16_t), &bytesWritten, pdMS_TO_TICKS(2));
}

void audioTaskMain(void *param) {
  (void)param;
  while (true) {
    audioTick();
    // Yield briefly so rendering/input tasks can run on the same core if needed.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void readButtons() {
  bool g = (digitalRead(green_btn_pin) == LOW);
  bool b = (digitalRead(blue_btn_pin) == LOW);
  greenPressed = (g && !greenDown);
  bluePressed = (b && !blueDown);
  greenDown = g;
  blueDown = b;
}

void firePlayerBullet() {
  for (int i = 0; i < playerBulletCapacity; i++) {
    if (!playerBullets[i].active) {
      playerBullets[i].active = true;
      playerBullets[i].x = playerX;
      playerBullets[i].y = playerSprite.yBottom - PLAYER_H;
      playerBullets[i].vy = -5;
      playSfxShoot();
      return;
    }
  }
}

void fireEnemyBullet() {
  int aliveCols[ALIEN_COLS];
  int aliveColCount = 0;

  for (int c = 0; c < ALIEN_COLS; c++) {
    bool colAlive = false;
    for (int r = ALIEN_ROWS - 1; r >= 0; r--) {
      if (aliens[r][c]) {
        colAlive = true;
        break;
      }
    }
    if (colAlive) {
      aliveCols[aliveColCount++] = c;
    }
  }

  if (aliveColCount == 0) {
    return;
  }

  int c = aliveCols[random(0, aliveColCount)];
  int r = -1;
  for (int rr = ALIEN_ROWS - 1; rr >= 0; rr--) {
    if (aliens[rr][c]) {
      r = rr;
      break;
    }
  }
  if (r < 0) {
    return;
  }

  int ax = formationX + c * (ALIEN_W + ALIEN_GAP_X);
  int ay = formationY + r * (ALIEN_H + ALIEN_GAP_Y);

  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!enemyBullets[i].active) {
      enemyBullets[i].active = true;
      enemyBullets[i].x = ax + ALIEN_W / 2;
      enemyBullets[i].y = ay + ALIEN_H + 2;
      enemyBullets[i].vy = 3 + min(level, 3);
      return;
    }
  }
}

bool hitRect(int x, int y, int w, int h, int px, int py, int pw, int ph) {
  if (x + w < px) return false;
  if (x > px + pw) return false;
  if (y + h < py) return false;
  if (y > py + ph) return false;
  return true;
}

void updateAliens() {
  unsigned long now = millis();
  if (now - lastAlienMoveMs < alienMoveIntervalMs) {
    return;
  }
  lastAlienMoveMs = now;

  int leftMost = SCREEN_W;
  int rightMost = 0;
  int bottomMost = 0;

  for (int r = 0; r < ALIEN_ROWS; r++) {
    for (int c = 0; c < ALIEN_COLS; c++) {
      if (!aliens[r][c]) continue;
      int ax = formationX + c * (ALIEN_W + ALIEN_GAP_X);
      int ay = formationY + r * (ALIEN_H + ALIEN_GAP_Y);
      if (ax < leftMost) leftMost = ax;
      if (ax + ALIEN_W > rightMost) rightMost = ax + ALIEN_W;
      if (ay + ALIEN_H > bottomMost) bottomMost = ay + ALIEN_H;
    }
  }

  if (leftMost == SCREEN_W) {
    return;
  }

  bool atEdge = false;
  if (alienDir > 0 && rightMost + 8 >= SCREEN_W - 8) {
    atEdge = true;
  }
  if (alienDir < 0 && leftMost - 8 <= 8) {
    atEdge = true;
  }

  if (atEdge) {
    formationY += 12;
    alienDir = -alienDir;
  } else {
    formationX += alienDir * 8;
  }

  if (bottomMost >= PLAYER_Y - 6) {
    lives = 0;
    gameState = GAME_OVER;
    playSfxGameOver();
    stopMotor();
  }
}

void updatePlayerControl() {
  int xv = analogRead(x_pin);
  int delta = 0;
  if (xv < 1500) {
    delta = -map(xv, 1499, 0, 2, 8);
  } else if (xv > 2600) {
    delta = map(xv, 2601, 4095, 2, 8);
  }

  playerX += delta;
  int minX = (PLAYER_W / 2) + 2;
  int maxX = SCREEN_W - minX;
  if (playerX < minX) playerX = minX;
  if (playerX > maxX) playerX = maxX;
  playerSprite.x = playerX;
  playerSprite.yBottom = PLAYER_Y;

  unsigned long now = millis();
  if (playerSprite.animation != nullptr && playerSprite.animation->frameCount > 1 &&
      now - playerSprite.lastFrameMs >= playerSprite.animation->intervalMs) {
    playerSprite.lastFrameMs = now;
    playerSprite.frameIndex = (playerSprite.frameIndex + 1) % playerSprite.animation->frameCount;
  }
  if (greenPressed && now - lastShotMs >= shootCooldownMs) {
    lastShotMs = now;
    firePlayerBullet();
  }
}

void updateBulletsAndCollisions() {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!playerBullets[i].active) continue;

    playerBullets[i].y += playerBullets[i].vy;
    if (playerBullets[i].y < HUD_H + 2) {
      playerBullets[i].active = false;
      continue;
    }

    bool hit = false;
    for (int r = 0; r < ALIEN_ROWS && !hit; r++) {
      for (int c = 0; c < ALIEN_COLS && !hit; c++) {
        if (!aliens[r][c]) continue;
        int ax = formationX + c * (ALIEN_W + ALIEN_GAP_X);
        int ay = formationY + r * (ALIEN_H + ALIEN_GAP_Y);
        if (hitRect(playerBullets[i].x - 1, playerBullets[i].y - 4, 3, 8, ax, ay, ALIEN_W, ALIEN_H)) {
          aliens[r][c] = false;
          playerBullets[i].active = false;
          score += 10;
          playSfxExplosion();
          hit = true;
        }
      }
    }
  }

  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!enemyBullets[i].active) continue;

    enemyBullets[i].y += enemyBullets[i].vy;
    if (enemyBullets[i].y > SCREEN_H + 2) {
      enemyBullets[i].active = false;
      continue;
    }

    bool invincible = (millis() < invincibleUntil);
    if (!invincible && hitRect(enemyBullets[i].x - 2, enemyBullets[i].y - 3, 4, 7,
                               playerX - PLAYER_W / 2, PLAYER_Y - PLAYER_H, PLAYER_W, PLAYER_H + 2)) {
      enemyBullets[i].active = false;
      lives--;
      invincibleUntil = millis() + 1200;
      playSfxHit();
      startHitVibration();
      if (lives <= 0) {
        gameState = GAME_OVER;
        playSfxGameOver();
        stopMotor();
      }
    }
  }

  if (allAliensDead()) {
    nextWave();
  }
}

template <typename CanvasT>
void drawHUDTo(CanvasT &canvas) {
  canvas.fillRect(0, 0, SCREEN_W, HUD_H, TFT_DARKGREY);
  canvas.setTextColor(TFT_WHITE, TFT_DARKGREY);
  canvas.setTextSize(1);
  canvas.setCursor(6, 8);
  canvas.printf("SCORE:%d  LIVES:%d  LV:%d", score, lives, level);
}

void drawHUD() {
  drawHUDTo(tft);
}

void drawPlayerAt(int x, uint16_t color) {
  tft.fillRect(x - (PLAYER_W / 2) - 1, PLAYER_Y - PLAYER_H - 1, PLAYER_W + 2, PLAYER_H + 2, color);
}

template <typename CanvasT>
void drawSpriteFrame(CanvasT &canvas, const SpriteFrame &frame, int xCenter, int yBottom, const uint16_t *palette, int scale) {
  int left = xCenter - (frame.width * scale) / 2;
  int top = yBottom - (frame.height * scale);
  for (int y = 0; y < frame.height; y++) {
    for (int x = 0; x < frame.width; x++) {
      uint8_t idx = frame.pixels[y * frame.width + x];
      if (idx == SPRITE_TRANSPARENT) {
        continue;
      }
      uint16_t color = palette[idx];
      canvas.fillRect(left + x * scale, top + y * scale, scale, scale, color);
    }
  }
}

template <typename CanvasT>
void drawPlayerTo(CanvasT &canvas) {
  bool invincible = (millis() < invincibleUntil);
  const uint16_t normalPalette[4] = {TFT_BLACK, TFT_CYAN, TFT_WHITE, TFT_ORANGE};
  const uint16_t invinciblePalette[4] = {TFT_BLACK, TFT_YELLOW, TFT_WHITE, TFT_RED};
  const uint16_t *palette = invincible ? invinciblePalette : normalPalette;

  const SpriteAnimation *anim = playerSprite.animation;
  if (anim == nullptr || anim->frameCount == 0) {
    return;
  }
  uint8_t idx = playerSprite.frameIndex % anim->frameCount;
  drawSpriteFrame(canvas, anim->frames[idx], playerSprite.x, playerSprite.yBottom, palette, PLAYER_PIXEL_SCALE);
}

void drawPlayer() {
  drawPlayerTo(tft);
}

template <typename CanvasT>
void drawAliensTo(CanvasT &canvas) {
  uint8_t baseAnimFrame = (uint8_t)((millis() / dragonAnim.intervalMs) % dragonAnim.frameCount);
  for (int r = 0; r < ALIEN_ROWS; r++) {
    for (int c = 0; c < ALIEN_COLS; c++) {
      if (!aliens[r][c]) continue;
      int ax = formationX + c * (ALIEN_W + ALIEN_GAP_X);
      int ay = formationY + r * (ALIEN_H + ALIEN_GAP_Y);

      uint16_t base = alienBaseColor(r, c);
      uint16_t palette[5] = {
        TFT_BLACK,
        base,
        tint565(base, TFT_WHITE, 32),
        TFT_YELLOW,
        shade565(base, 55)
      };

      uint8_t phaseOffset = (uint8_t)((r * ALIEN_COLS + c) % dragonAnim.frameCount);
      uint8_t frameIndex = (baseAnimFrame + phaseOffset) % dragonAnim.frameCount;
      int xCenter = ax + ALIEN_W / 2;
      int yBottom = ay + ALIEN_H;
      drawSpriteFrame(canvas, dragonAnim.frames[frameIndex], xCenter, yBottom, palette, ALIEN_PIXEL_SCALE);
    }
  }
}

void drawAliens() {
  drawAliensTo(tft);
}

template <typename CanvasT>
void drawBulletsTo(CanvasT &canvas) {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (playerBullets[i].active) {
      canvas.fillRect(playerBullets[i].x - 1, playerBullets[i].y - 4, 3, 8, TFT_WHITE);
    }
  }

  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (enemyBullets[i].active) {
      canvas.fillRect(enemyBullets[i].x - 1, enemyBullets[i].y - 3, 3, 7, TFT_RED);
    }
  }
}

void drawBullets() {
  drawBulletsTo(tft);
}

void drawPlaySceneBuffered() {
  frameBuffer.fillSprite(TFT_BLACK);
  drawHUDTo(frameBuffer);
  drawAliensTo(frameBuffer);
  drawPlayerTo(frameBuffer);
  drawBulletsTo(frameBuffer);
  frameBuffer.pushSprite(0, 0);
}

bool aliensChangedForRender() {
  if (!prevAliensValid) {
    return true;
  }
  if (formationX != prevFormationX || formationY != prevFormationY) {
    return true;
  }
  for (int r = 0; r < ALIEN_ROWS; r++) {
    for (int c = 0; c < ALIEN_COLS; c++) {
      if (prevAliens[r][c] != aliens[r][c]) {
        return true;
      }
    }
  }
  return false;
}

void snapshotAlienRenderState() {
  prevFormationX = formationX;
  prevFormationY = formationY;
  for (int r = 0; r < ALIEN_ROWS; r++) {
    for (int c = 0; c < ALIEN_COLS; c++) {
      prevAliens[r][c] = aliens[r][c];
    }
  }
  prevAliensValid = true;
}

void drawPlayScene() {
  tft.startWrite();

  if (score != lastHudScore || lives != lastHudLives || level != lastHudLevel) {
    drawHUD();
    lastHudScore = score;
    lastHudLives = lives;
    lastHudLevel = level;
  }

  if (prevPlayerX >= 0) {
    drawPlayerAt(prevPlayerX, TFT_BLACK);
  }

  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (prevPlayerBullets[i].active) {
      tft.fillRect(prevPlayerBullets[i].x - 1, prevPlayerBullets[i].y - 4, 3, 8, TFT_BLACK);
    }
  }

  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (prevEnemyBullets[i].active) {
      tft.fillRect(prevEnemyBullets[i].x - 1, prevEnemyBullets[i].y - 3, 3, 7, TFT_BLACK);
    }
  }

  if (aliensChangedForRender() && prevAliensValid) {
    int prevW = ALIEN_COLS * (ALIEN_W + ALIEN_GAP_X) - ALIEN_GAP_X;
    int prevH = ALIEN_ROWS * (ALIEN_H + ALIEN_GAP_Y) - ALIEN_GAP_Y;
    tft.fillRect(prevFormationX - 2, prevFormationY - 2, prevW + 4, prevH + 4, TFT_BLACK);
  }

  drawAliens();
  drawPlayer();
  drawBullets();

  snapshotAlienRenderState();
  prevPlayerX = playerX;
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    prevPlayerBullets[i].x = playerBullets[i].x;
    prevPlayerBullets[i].y = playerBullets[i].y;
    prevPlayerBullets[i].active = playerBullets[i].active;
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    prevEnemyBullets[i].x = enemyBullets[i].x;
    prevEnemyBullets[i].y = enemyBullets[i].y;
    prevEnemyBullets[i].active = enemyBullets[i].active;
  }

  tft.endWrite();
}

void drawTitle() {
  tft.fillScreen(TFT_NAVY);
  int artX = 370;
  int artY = 176;
  uint16_t glowOuter = rgb565(22, 62, 112);
  uint16_t glowInner = rgb565(38, 98, 156);
  uint16_t alienDark = rgb565(74, 176, 86);
  uint16_t alienMid = rgb565(108, 216, 118);
  uint16_t alienLight = rgb565(188, 252, 196);
  uint16_t shipDark = rgb565(58, 96, 154);
  uint16_t shipMid = rgb565(94, 142, 212);
  uint16_t shipLight = rgb565(208, 230, 255);
  tft.fillCircle(artX, artY + 2, 56, glowOuter);
  tft.fillCircle(artX, artY + 2, 40, glowInner);

  tft.fillRoundRect(artX - 30, artY - 22, 60, 30, 7, alienDark);
  tft.fillRoundRect(artX - 26, artY - 18, 52, 24, 6, alienMid);
  tft.fillRoundRect(artX - 20, artY - 14, 40, 17, 5, alienLight);
  tft.fillRect(artX - 40, artY - 10, 12, 10, alienMid);
  tft.fillRect(artX + 28, artY - 10, 12, 10, alienMid);
  tft.fillRect(artX - 26, artY + 6, 10, 8, alienDark);
  tft.fillRect(artX + 16, artY + 6, 10, 8, alienDark);
  tft.fillCircle(artX - 11, artY - 7, 4, TFT_WHITE);
  tft.fillCircle(artX + 11, artY - 7, 4, TFT_WHITE);
  tft.fillCircle(artX - 10, artY - 7, 2, rgb565(28, 62, 114));
  tft.fillCircle(artX + 10, artY - 7, 2, rgb565(28, 62, 114));
  tft.drawFastHLine(artX - 12, artY + 1, 24, rgb565(26, 70, 42));

  int shipY = artY + 42;
  tft.fillRoundRect(artX - 42, shipY - 12, 84, 22, 9, shipDark);
  tft.fillRoundRect(artX - 34, shipY - 8, 68, 14, 7, shipMid);
  tft.fillRoundRect(artX - 24, shipY - 6, 48, 8, 4, shipLight);
  tft.fillRect(artX - 10, shipY - 17, 20, 5, rgb565(255, 204, 120));
  tft.fillRect(artX - 3, shipY - 23, 6, 7, rgb565(255, 232, 172));
  tft.fillRect(artX - 2, shipY - 30, 4, 7, rgb565(174, 238, 255));

  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(3);
  tft.setCursor(58, 48);
  tft.print("SPACE INVADERS");

  tft.setTextSize(2);
  tft.setCursor(56, 110);
  tft.print("Joystick: Move");
  tft.setCursor(56, 138);
  tft.print("Green: Shoot");
  tft.setCursor(56, 166);
  tft.print("Blue: Start / Restart");

  tft.setTextSize(1);
  tft.setCursor(82, 220);
  tft.print("Hit by enemy bullet => motor vibrates");
  tft.setCursor(130, 244);
  tft.print("Press Blue Button to Start");
}

void drawGameOver() {
  tft.fillScreen(TFT_MAROON);
  tft.setTextColor(TFT_WHITE, TFT_MAROON);
  tft.setTextSize(3);
  tft.setCursor(120, 78);
  tft.print("GAME OVER");

  tft.setTextSize(2);
  tft.setCursor(140, 140);
  tft.printf("Score: %d", score);
  tft.setCursor(140, 168);
  tft.printf("Level: %d", level);
  tft.setCursor(88, 220);
  tft.print("Press Blue Button to Restart");
}

void setupHardware() {
  pinMode(x_pin, INPUT);
  pinMode(y_pin, INPUT);
  pinMode(green_btn_pin, INPUT_PULLUP);
  pinMode(blue_btn_pin, INPUT_PULLUP);

  pinMode(green_led_pin, OUTPUT);
  pinMode(blue_led_pin, OUTPUT);
  digitalWrite(green_led_pin, HIGH);
  digitalWrite(blue_led_pin, HIGH);

  ledcSetup(reverse_ch, motor_frequency, motor_resolution);
  ledcAttachPin(reverse_pin, reverse_ch);
  ledcSetup(forward_ch, motor_frequency, motor_resolution);
  ledcAttachPin(forward_pin, forward_ch);
  stopMotor();

  i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

void initFrameBuffer() {
  frameBuffer.setColorDepth(16);
  frameBufferReady = (frameBuffer.createSprite(SCREEN_W, SCREEN_H) != nullptr);
  if (!frameBufferReady) {
    frameBuffer.deleteSprite();
    frameBuffer.setColorDepth(8);
    frameBufferReady = (frameBuffer.createSprite(SCREEN_W, SCREEN_H) != nullptr);
  }
}

void setup() {
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  initFrameBuffer();

  setupHardware();
  drawTitle();
  lastRenderedState = TITLE;

  bgmIndex = 0;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;

  xTaskCreatePinnedToCore(
      audioTaskMain,
      "audio_task",
      4096,
      nullptr,
      2,
      &audioTaskHandle,
      0);
}

void loop() {
  readButtons();

  bool blueNow = (digitalRead(blue_btn_pin) == LOW);
  if (gameState == TITLE) {
    if (blueNow && !blueLatch) {
      beginGame();
      blueLatch = true;
    }
  } else if (gameState == PLAYING) {
    unsigned long now = millis();
    if (now - lastGameUpdateMs >= GAME_UPDATE_INTERVAL_MS) {
      lastGameUpdateMs = now;
      updatePlayerControl();
      updateAliens();
      updateBulletsAndCollisions();

      if (now - lastEnemyShotMs >= enemyShotIntervalMs) {
        lastEnemyShotMs = now;
        fireEnemyBullet();
      }
    }
  } else if (gameState == GAME_OVER) {
    if (blueNow && !blueLatch) {
      beginGame();
      blueLatch = true;
    }
  }
  if (!blueNow) {
    blueLatch = false;
  }

  updateVibration();

  if (gameState == TITLE) {
    if (lastRenderedState != TITLE) {
      drawTitle();
      lastRenderedState = TITLE;
    }
  } else if (gameState == PLAYING) {
    if (lastRenderedState != PLAYING) {
      if (!frameBufferReady) {
        tft.fillScreen(TFT_BLACK);
        tft.fillRect(0, 0, SCREEN_W, HUD_H, TFT_DARKGREY);
      }
      resetRenderCache();
      lastRenderedState = PLAYING;
    }
    if (frameBufferReady) {
      drawPlaySceneBuffered();
    } else {
      drawPlayScene();
    }
  } else {
    if (lastRenderedState != GAME_OVER) {
      drawGameOver();
      lastRenderedState = GAME_OVER;
    }
  }

  delay(1);
}
