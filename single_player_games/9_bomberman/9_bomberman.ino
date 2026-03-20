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
#define AUDIO_CHUNK_SAMPLES 128

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBuffer = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int ARENA_COLS = 15;
static const int ARENA_ROWS = 11;
static const int TILE_SIZE = 28;
static const int ARENA_W = ARENA_COLS * TILE_SIZE;
static const int ARENA_H = ARENA_ROWS * TILE_SIZE;
static const int ARENA_X = 8;
static const int ARENA_Y = 6;
static const int HUD_X = ARENA_X + ARENA_W + 8;
static const int HUD_W = SCREEN_W - HUD_X - 8;

static const uint16_t GAME_UPDATE_INTERVAL_MS = 16;
static const uint16_t PLAYER_STEP_MS = 120;
static const uint16_t ENEMY_STEP_MS = 170;
static const uint16_t BOMB_FUSE_MS = 3600;
static const uint16_t FLAME_MS = 450;
static const uint16_t STAGE_CLEAR_MS = 1200;
static const uint16_t PLAYER_INVULN_MS = 1200;
static const uint8_t MAX_BOMBS = 8;
static const uint8_t MAX_FLAMES = 48;
static const uint8_t MAX_ENEMIES = 6;
static const uint8_t MAX_POWERUPS = 12;

enum GameState {
  TITLE,
  STAGE_INTRO,
  PLAYING,
  STAGE_CLEAR,
  GAME_OVER
};

enum TileType : uint8_t {
  TILE_EMPTY = 0,
  TILE_SOLID = 1,
  TILE_BRICK = 2
};

enum EnemyType : uint8_t {
  ENEMY_WANDER = 0,
  ENEMY_CHASER = 1
};

enum Direction : uint8_t {
  DIR_UP = 0,
  DIR_RIGHT = 1,
  DIR_DOWN = 2,
  DIR_LEFT = 3
};

enum PowerUpType : uint8_t {
  POWER_BOMB = 0,
  POWER_RANGE = 1,
  POWER_SPEED = 2,
  POWER_PASS_BOMB = 3
};

struct Voice {
  bool active;
  float freq;
  float phase;
  int remainSamples;
  float volume;
  uint8_t wave;
  float slidePerSample;
};

struct Note {
  uint16_t freq;
  uint16_t ms;
};

struct Player {
  float x;
  float y;
  int col;
  int row;
  int lives;
  uint8_t bombLimit;
  uint8_t flameRange;
  uint8_t speedTier;
  bool canPassBombs;
  bool alive;
  Direction facing;
  bool moving;
  bool ghostBombActive;
  int ghostBombCol;
  int ghostBombRow;
  unsigned long invulnUntil;
};

struct Enemy {
  bool active;
  EnemyType type;
  float x;
  float y;
  int col;
  int row;
  Direction dir;
  uint8_t colorVariant;
  bool moving;
  int targetCol;
  int targetRow;
  unsigned long nextMoveMs;
};

struct Bomb {
  bool active;
  int col;
  int row;
  uint8_t range;
  unsigned long explodeAt;
};

struct Flame {
  bool active;
  int col;
  int row;
  bool horizontal;
  bool endpoint;
  unsigned long endAt;
};

struct PowerUp {
  bool active;
  int col;
  int row;
  PowerUpType type;
};

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;

TileType tiles[ARENA_ROWS][ARENA_COLS];
Player player;
Enemy enemies[MAX_ENEMIES];
Bomb bombs[MAX_BOMBS];
Flame flames[MAX_FLAMES];
PowerUp powerUps[MAX_POWERUPS];

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
int latestX = 2048;
int latestY = 2048;

bool vibrating = false;
bool vibForward = true;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;
unsigned long stateTimerMs = 0;
unsigned long lastGameUpdateMs = 0;

char bannerText[32] = "";
int stageNumber = 1;
bool frameBufferReady = false;

Voice sfxVoices[4];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
TaskHandle_t audioTaskHandle = nullptr;
float bgmPhase = 0.0f;
int bgmIndex = 0;
int bgmRemainSamples = 0;

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

const Note bgmLoop[] = {
  {330, 140}, {392, 140}, {523, 140}, {392, 140},
  {294, 140}, {349, 140}, {466, 140}, {349, 140},
  {330, 140}, {392, 140}, {523, 140}, {659, 140},
  {587, 140}, {523, 140}, {392, 180}, {0, 60}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);

static const uint8_t BOMBER_W = 10;
static const uint8_t BOMBER_H = 12;
static const uint8_t BOMBER_SCALE = 2;

static const uint8_t bomberFront0[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,7,7,1,1,0,0,
  0,1,1,3,3,3,3,1,1,0,
  0,1,3,3,3,3,3,3,1,0,
  0,1,3,2,3,3,2,3,1,0,
  0,1,3,3,3,3,3,3,1,0,
  1,1,1,7,4,4,7,1,1,1,
  1,5,1,4,4,4,4,1,5,1,
  0,1,4,4,6,6,4,4,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,6,6,0,0,0,0,6,6,0,
  6,6,0,0,0,0,0,0,6,6
};

static const uint8_t bomberFront1[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,7,7,1,1,0,0,
  0,1,1,3,3,3,3,1,1,0,
  0,1,3,3,3,3,3,3,1,0,
  0,1,3,2,3,3,2,3,1,0,
  0,1,3,3,3,3,3,3,1,0,
  1,1,1,7,4,4,7,1,1,1,
  1,5,1,4,4,4,4,1,5,1,
  0,1,4,4,6,6,4,4,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,0,6,6,0,0,6,6,0,0,
  0,6,0,0,0,0,0,0,6,0
};

static const uint8_t bomberFront2[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,7,7,1,1,0,0,
  0,1,1,3,3,3,3,1,1,0,
  0,1,3,3,3,3,3,3,1,0,
  0,1,3,2,3,3,2,3,1,0,
  0,1,3,3,3,3,3,3,1,0,
  1,1,1,7,4,4,7,1,1,1,
  1,5,1,4,4,4,4,1,5,1,
  0,1,4,4,6,6,4,4,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,0,0,6,6,6,6,0,0,0,
  0,0,6,0,0,0,0,6,0,0
};

static const uint8_t bomberFront3[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,7,7,1,1,0,0,
  0,1,1,3,3,3,3,1,1,0,
  0,1,3,3,3,3,3,3,1,0,
  0,1,3,2,3,3,2,3,1,0,
  0,1,3,3,3,3,3,3,1,0,
  1,1,1,7,4,4,7,1,1,1,
  1,5,1,4,4,4,4,1,5,1,
  0,1,4,4,6,6,4,4,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,6,0,0,0,0,0,0,6,0,
  0,0,6,6,0,0,6,6,0,0
};

static const uint8_t bomberBack0[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,7,7,1,1,0,0,
  0,1,1,4,4,4,4,1,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,1,4,1,7,7,1,4,1,0,
  0,1,4,1,3,3,1,4,1,0,
  1,1,1,4,4,4,4,1,1,1,
  1,5,1,4,4,4,4,1,5,1,
  0,1,4,4,6,6,4,4,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,6,6,0,0,0,0,6,6,0,
  6,6,0,0,0,0,0,0,6,6
};

static const uint8_t bomberBack1[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,1,1,1,1,0,0,
  0,1,1,4,4,4,4,1,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,1,4,1,1,1,1,4,1,0,
  0,1,4,1,3,3,1,4,1,0,
  1,1,1,4,4,4,4,1,1,1,
  1,5,1,4,4,4,4,1,5,1,
  0,1,4,4,6,6,4,4,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,0,6,6,0,0,6,6,0,0,
  0,6,0,0,0,0,0,0,6,0
};

static const uint8_t bomberBack2[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,1,1,1,1,0,0,
  0,1,1,4,4,4,4,1,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,1,4,1,1,1,1,4,1,0,
  0,1,4,1,3,3,1,4,1,0,
  1,1,1,4,4,4,4,1,1,1,
  1,5,1,4,4,4,4,1,5,1,
  0,1,4,4,6,6,4,4,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,0,0,6,6,6,6,0,0,0,
  0,0,6,0,0,0,0,6,0,0
};

static const uint8_t bomberBack3[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,1,1,1,1,0,0,
  0,1,1,4,4,4,4,1,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,1,4,1,1,1,1,4,1,0,
  0,1,4,1,3,3,1,4,1,0,
  1,1,1,4,4,4,4,1,1,1,
  1,5,1,4,4,4,4,1,5,1,
  0,1,4,4,6,6,4,4,1,0,
  0,1,4,4,4,4,4,4,1,0,
  0,6,0,0,0,0,0,0,6,0,
  0,0,6,6,0,0,6,6,0,0
};

static const uint8_t bomberSide0[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,1,1,1,1,0,0,
  0,1,1,3,3,3,3,1,1,0,
  0,1,3,3,3,3,3,1,1,0,
  0,1,3,2,3,3,3,1,0,0,
  0,1,3,3,3,3,3,1,0,0,
  1,1,1,4,4,4,4,1,1,0,
  1,5,1,4,4,4,4,1,5,0,
  0,1,4,4,6,6,4,1,0,0,
  0,1,4,4,4,4,4,1,0,0,
  0,6,6,0,0,0,6,0,0,0,
  6,6,0,0,0,6,6,0,0,0
};

static const uint8_t bomberSide1[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,1,1,1,1,0,0,
  0,1,1,3,3,3,3,1,1,0,
  0,1,3,3,3,3,3,1,1,0,
  0,1,3,2,3,3,3,1,0,0,
  0,1,3,3,3,3,3,1,0,0,
  1,1,1,4,4,4,4,1,1,0,
  1,5,1,4,4,4,4,1,5,0,
  0,1,4,4,6,6,4,1,0,0,
  0,1,4,4,4,4,4,1,0,0,
  0,0,6,6,0,6,0,0,0,0,
  0,6,0,0,6,6,0,0,0,0
};

static const uint8_t bomberSide2[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,1,1,1,1,0,0,
  0,1,1,3,3,3,3,1,1,0,
  0,1,3,3,3,3,3,1,1,0,
  0,1,3,2,3,3,3,1,0,0,
  0,1,3,3,3,3,3,1,0,0,
  1,1,1,4,4,4,4,1,1,0,
  1,5,1,4,4,4,4,1,5,0,
  0,1,4,4,6,6,4,1,0,0,
  0,1,4,4,4,4,4,1,0,0,
  0,0,0,6,6,6,0,0,0,0,
  0,0,6,0,0,0,6,0,0,0
};

static const uint8_t bomberSide3[BOMBER_W * BOMBER_H] = {
  0,0,0,1,1,1,1,0,0,0,
  0,0,1,1,1,1,1,1,0,0,
  0,1,1,3,3,3,3,1,1,0,
  0,1,3,3,3,3,3,1,1,0,
  0,1,3,2,3,3,3,1,0,0,
  0,1,3,3,3,3,3,1,0,0,
  1,1,1,4,4,4,4,1,1,0,
  1,5,1,4,4,4,4,1,5,0,
  0,1,4,4,6,6,4,1,0,0,
  0,1,4,4,4,4,4,1,0,0,
  0,6,0,0,6,6,0,0,0,0,
  0,0,6,6,0,6,0,0,0,0
};

const uint16_t bomberPalette[] = {
  0,
  TFT_WHITE,
  TFT_BLACK,
  rgb565(255, 214, 190),
  rgb565(72, 132, 255),
  rgb565(255, 120, 160),
  rgb565(56, 56, 86),
  rgb565(196, 220, 255)
};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

template <typename CanvasT>
void drawIndexedSprite(CanvasT &canvas, int left, int top, const uint8_t *pixels,
                       uint8_t width, uint8_t height, const uint16_t *palette,
                       uint8_t scale, bool flipH = false) {
  for (uint8_t py = 0; py < height; py++) {
    for (uint8_t px = 0; px < width; px++) {
      uint8_t srcX = flipH ? (width - 1 - px) : px;
      uint8_t index = pixels[py * width + srcX];
      if (index == 0) {
        continue;
      }
      canvas.fillRect(left + px * scale, top + py * scale, scale, scale, palette[index]);
    }
  }
}

bool inBounds(int col, int row) {
  return col >= 0 && col < ARENA_COLS && row >= 0 && row < ARENA_ROWS;
}

float tileCenterX(int col) {
  return ARENA_X + col * TILE_SIZE + TILE_SIZE * 0.5f;
}

float tileCenterY(int row) {
  return ARENA_Y + row * TILE_SIZE + TILE_SIZE * 0.5f;
}

void syncPlayerCell() {
  int col = (int)((player.x - ARENA_X) / TILE_SIZE);
  int row = (int)((player.y - ARENA_Y) / TILE_SIZE);
  if (col < 0) col = 0;
  if (col >= ARENA_COLS) col = ARENA_COLS - 1;
  if (row < 0) row = 0;
  if (row >= ARENA_ROWS) row = ARENA_ROWS - 1;
  player.col = col;
  player.row = row;
}

bool playerIntersectsCell(int col, int row) {
  float radius = 8.0f;
  float left = player.x - radius;
  float right = player.x + radius;
  float top = player.y - radius;
  float bottom = player.y + radius;
  float cellLeft = ARENA_X + col * TILE_SIZE;
  float cellTop = ARENA_Y + row * TILE_SIZE;
  float cellRight = cellLeft + TILE_SIZE;
  float cellBottom = cellTop + TILE_SIZE;
  return !(right <= cellLeft || left >= cellRight || bottom <= cellTop || top >= cellBottom);
}

bool enemyIntersectsPlayer(const Enemy &enemy) {
  if (!enemy.active) {
    return false;
  }
  float dx = enemy.x - player.x;
  float dy = enemy.y - player.y;
  return (dx * dx + dy * dy) < (14.0f * 14.0f);
}

void resetPlayerPosition(int col, int row) {
  player.x = tileCenterX(col);
  player.y = tileCenterY(row);
  player.col = col;
  player.row = row;
  player.moving = false;
  player.ghostBombActive = false;
  player.ghostBombCol = -1;
  player.ghostBombRow = -1;
}

void resetEnemyPosition(Enemy &enemy, int col, int row) {
  enemy.x = tileCenterX(col);
  enemy.y = tileCenterY(row);
  enemy.col = col;
  enemy.row = row;
  enemy.moving = false;
  enemy.targetCol = col;
  enemy.targetRow = row;
}

int activeBombCount() {
  int count = 0;
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (bombs[i].active) {
      count++;
    }
  }
  return count;
}

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startVibration(uint16_t durationMs = 160) {
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

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 32) {
    vibLastToggleMs = now;
    vibForward = !vibForward;
    const int duty = 160;
    if (vibForward) {
      ledcWrite(forward_ch, duty);
      ledcWrite(reverse_ch, 0);
    } else {
      ledcWrite(forward_ch, 0);
      ledcWrite(reverse_ch, duty);
    }
  }
}

void assignVoice(Voice &voice, float freq, int durationSamples, float volume, uint8_t wave, float slide) {
  voice.active = true;
  voice.freq = freq;
  voice.phase = 0.0f;
  voice.remainSamples = durationSamples;
  voice.volume = volume;
  voice.wave = wave;
  voice.slidePerSample = slide;
}

void startVoice(float freq, int ms, float volume, uint8_t wave, float slide = 0.0f, int preferredChannel = -1) {
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  taskENTER_CRITICAL(&audioMux);
  if (preferredChannel >= 0 && preferredChannel < 4) {
    assignVoice(sfxVoices[preferredChannel], freq, durationSamples, volume, wave, slide);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }

  for (int i = 0; i < 4; i++) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave, slide);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }

  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave, slide);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxPlaceBomb() {
  startVoice(380.0f, 70, 0.11f, 0);
  startVoice(520.0f, 40, 0.07f, 0, 0.0f, 1);
}

void playSfxExplode() {
  startVoice(220.0f, 170, 0.20f, 2, -0.01f);
  startVoice(110.0f, 220, 0.14f, 0, -0.002f, 1);
}

void playSfxPickup() {
  startVoice(523.0f, 70, 0.08f, 0);
  startVoice(659.0f, 70, 0.08f, 0, 0.0f, 1);
  startVoice(784.0f, 90, 0.10f, 0, 0.0f, 2);
}

void playSfxHit() {
  startVoice(160.0f, 160, 0.14f, 2, -0.02f);
  startVoice(120.0f, 140, 0.08f, 0, -0.003f, 1);
}

void playSfxStageClear() {
  startVoice(523.0f, 90, 0.08f, 0);
  startVoice(659.0f, 90, 0.08f, 0, 0.0f, 1);
  startVoice(784.0f, 160, 0.10f, 0, 0.0f, 2);
}

float bgmSample() {
  if (gameState != PLAYING) {
    return 0.0f;
  }

  if (bgmRemainSamples <= 0) {
    bgmIndex = (bgmIndex + 1) % BGM_NOTE_COUNT;
    bgmRemainSamples = (SAMPLE_RATE * bgmLoop[bgmIndex].ms) / 1000;
  }

  uint16_t freq = bgmLoop[bgmIndex].freq;
  bgmRemainSamples--;
  if (freq == 0) {
    return 0.0f;
  }

  float s = (bgmPhase < 0.5f) ? 0.008f : -0.008f;
  bgmPhase += (float)freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  return s;
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
  v.freq += v.slidePerSample;
  if (v.freq < 30.0f) {
    v.freq = 30.0f;
  }
  return s * v.volume;
}

void audioTick() {
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; i++) {
    float mix = bgmSample();
    taskENTER_CRITICAL(&audioMux);
    for (int v = 0; v < 4; v++) {
      if (!sfxVoices[v].active) {
        continue;
      }
      mix += waveSample(sfxVoices[v]);
      sfxVoices[v].remainSamples--;
      if (sfxVoices[v].remainSamples <= 0) {
        sfxVoices[v].active = false;
      }
    }
    taskEXIT_CRITICAL(&audioMux);

    if (mix > 0.65f) mix = 0.65f;
    if (mix < -0.65f) mix = -0.65f;
    audioBuffer[i] = (int16_t)(mix * 32767.0f);
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_NUM_1, (const char *)audioBuffer, AUDIO_CHUNK_SAMPLES * sizeof(int16_t),
            &bytesWritten, pdMS_TO_TICKS(2));
}

void audioTaskMain(void *param) {
  (void)param;
  while (true) {
    audioTick();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void readButtons() {
  bool g = (digitalRead(green_btn_pin) == LOW);
  bool b = (digitalRead(blue_btn_pin) == LOW);
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);
  greenPressed = (g && !greenDown);
  bluePressed = (b && !blueDown);
  greenDown = g;
  blueDown = b;
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

bool bombAt(int col, int row) {
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (bombs[i].active && bombs[i].col == col && bombs[i].row == row) {
      return true;
    }
  }
  return false;
}

bool enemyTouchesCell(const Enemy &enemy, int col, int row) {
  if (!enemy.active) {
    return false;
  }
  if (enemy.col == col && enemy.row == row) {
    return true;
  }
  if (enemy.moving && enemy.targetCol == col && enemy.targetRow == row) {
    return true;
  }
  float cellLeft = ARENA_X + col * TILE_SIZE;
  float cellTop = ARENA_Y + row * TILE_SIZE;
  float cellRight = cellLeft + TILE_SIZE;
  float cellBottom = cellTop + TILE_SIZE;
  float radius = 8.0f;
  return !(enemy.x + radius <= cellLeft || enemy.x - radius >= cellRight ||
           enemy.y + radius <= cellTop || enemy.y - radius >= cellBottom);
}

bool enemyAt(int col, int row, int *enemyIndex = nullptr) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemyTouchesCell(enemies[i], col, row)) {
      if (enemyIndex != nullptr) {
        *enemyIndex = i;
      }
      return true;
    }
  }
  return false;
}

bool flameAt(int col, int row) {
  for (int i = 0; i < MAX_FLAMES; i++) {
    if (flames[i].active && flames[i].col == col && flames[i].row == row) {
      return true;
    }
  }
  return false;
}

PowerUp *powerUpAt(int col, int row) {
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (powerUps[i].active && powerUps[i].col == col && powerUps[i].row == row) {
      return &powerUps[i];
    }
  }
  return nullptr;
}

bool isSpawnClear(int col, int row) {
  if (!inBounds(col, row)) {
    return false;
  }
  if (tiles[row][col] != TILE_EMPTY) {
    return false;
  }
  return true;
}

void clearTransientObjects() {
  for (int i = 0; i < MAX_BOMBS; i++) {
    bombs[i].active = false;
  }
  for (int i = 0; i < MAX_FLAMES; i++) {
    flames[i].active = false;
  }
  for (int i = 0; i < MAX_POWERUPS; i++) {
    powerUps[i].active = false;
  }
  for (int i = 0; i < MAX_ENEMIES; i++) {
    enemies[i].active = false;
  }
}

void resetPlayer() {
  player.x = 0.0f;
  player.y = 0.0f;
  player.col = 0;
  player.row = 0;
  player.lives = 3;
  player.bombLimit = 1;
  player.flameRange = 2;
  player.speedTier = 0;
  player.canPassBombs = false;
  player.alive = true;
  player.facing = DIR_DOWN;
  player.moving = false;
  player.ghostBombActive = false;
  player.ghostBombCol = -1;
  player.ghostBombRow = -1;
  player.invulnUntil = 0;
}

void addPowerUp(int col, int row) {
  if (random(0, 100) >= 34) {
    return;
  }
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (!powerUps[i].active) {
      powerUps[i].active = true;
      powerUps[i].col = col;
      powerUps[i].row = row;
      powerUps[i].type = (PowerUpType)random(0, 4);
      return;
    }
  }
}

void buildStage() {
  clearTransientObjects();

  for (int row = 0; row < ARENA_ROWS; row++) {
    for (int col = 0; col < ARENA_COLS; col++) {
      if (row == 0 || row == ARENA_ROWS - 1 || col == 0 || col == ARENA_COLS - 1) {
        tiles[row][col] = TILE_SOLID;
      } else if ((row % 2 == 0) && (col % 2 == 0)) {
        tiles[row][col] = TILE_SOLID;
      } else {
        tiles[row][col] = TILE_EMPTY;
      }
    }
  }

  for (int row = 1; row < ARENA_ROWS - 1; row++) {
    for (int col = 1; col < ARENA_COLS - 1; col++) {
      if (tiles[row][col] != TILE_EMPTY) {
        continue;
      }
      bool keepSpawn = (col <= 2 && row <= 2);
      bool keepExitZone = (col >= ARENA_COLS - 3 && row >= ARENA_ROWS - 3);
      if (keepSpawn || keepExitZone) {
        continue;
      }
      int brickChance = 42 + min(stageNumber * 3, 18);
      if (random(0, 100) < brickChance) {
        tiles[row][col] = TILE_BRICK;
      }
    }
  }

  resetPlayerPosition(1, 1);
  player.alive = true;
  player.facing = DIR_DOWN;
  player.invulnUntil = millis() + 1200;

  int enemyCount = 2 + min(stageNumber, 4);
  for (int i = 0; i < enemyCount; i++) {
    for (int attempt = 0; attempt < 60; attempt++) {
      int col = random(ARENA_COLS - 4, ARENA_COLS - 1);
      int row = random(ARENA_ROWS - 4, ARENA_ROWS - 1);
      if (!isSpawnClear(col, row) || enemyAt(col, row)) {
        continue;
      }
      enemies[i].active = true;
      enemies[i].type = (i % 2 == 0) ? ENEMY_WANDER : ENEMY_CHASER;
      resetEnemyPosition(enemies[i], col, row);
      enemies[i].dir = (Direction)random(0, 4);
      enemies[i].colorVariant = i % 5;
      enemies[i].nextMoveMs = millis() + random(140, 420);
      break;
    }
  }

  snprintf(bannerText, sizeof(bannerText), "STAGE %d", stageNumber);
  stateTimerMs = millis();
  gameState = STAGE_INTRO;
}

void beginGame() {
  stageNumber = 1;
  resetPlayer();
  buildStage();
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  lastGameUpdateMs = millis();
  stopMotor();
}

bool passableForPlayer(int col, int row) {
  if (!inBounds(col, row)) {
    return false;
  }
  if (tiles[row][col] != TILE_EMPTY) {
    return false;
  }
  if (bombAt(col, row) &&
      !player.canPassBombs &&
      !(player.ghostBombActive && player.ghostBombCol == col && player.ghostBombRow == row)) {
    return false;
  }
  return true;
}

bool passableForEnemy(int col, int row) {
  if (!inBounds(col, row)) {
    return false;
  }
  if (tiles[row][col] != TILE_EMPTY) {
    return false;
  }
  if (bombAt(col, row)) {
    return false;
  }
  return !enemyAt(col, row);
}

void chooseEnemyDirection(Enemy &enemy);

void enemyDirDelta(Direction dir, int &dc, int &dr) {
  dc = 0;
  dr = 0;
  if (dir == DIR_LEFT) dc = -1;
  if (dir == DIR_RIGHT) dc = 1;
  if (dir == DIR_UP) dr = -1;
  if (dir == DIR_DOWN) dr = 1;
}

bool chooseEnemyTarget(Enemy &enemy) {
  int dc = 0;
  int dr = 0;
  enemyDirDelta(enemy.dir, dc, dr);
  int nextCol = enemy.col + dc;
  int nextRow = enemy.row + dr;
  if (!passableForEnemy(nextCol, nextRow)) {
    chooseEnemyDirection(enemy);
    enemyDirDelta(enemy.dir, dc, dr);
    nextCol = enemy.col + dc;
    nextRow = enemy.row + dr;
  }

  if (!passableForEnemy(nextCol, nextRow)) {
    return false;
  }

  enemy.targetCol = nextCol;
  enemy.targetRow = nextRow;
  enemy.moving = true;
  return true;
}

void addFlameCell(int col, int row, bool horizontal, bool endpoint, unsigned long endAt) {
  for (int i = 0; i < MAX_FLAMES; i++) {
    if (!flames[i].active) {
      flames[i].active = true;
      flames[i].col = col;
      flames[i].row = row;
      flames[i].horizontal = horizontal;
      flames[i].endpoint = endpoint;
      flames[i].endAt = endAt;
      return;
    }
  }
}

void triggerBomb(int bombIndex);

void damagePlayer() {
  unsigned long now = millis();
  if (now < player.invulnUntil || !player.alive) {
    return;
  }
  player.lives--;
  player.invulnUntil = now + PLAYER_INVULN_MS;
  playSfxHit();
  startVibration(220);

  if (player.lives <= 0) {
    player.alive = false;
    snprintf(bannerText, sizeof(bannerText), "GAME OVER");
    gameState = GAME_OVER;
    stateTimerMs = now;
    return;
  }

  resetPlayerPosition(1, 1);
  player.facing = DIR_DOWN;
  player.invulnUntil = now + PLAYER_INVULN_MS;
}

void damageEnemyAt(int col, int row) {
  int index = -1;
  if (enemyAt(col, row, &index)) {
    enemies[index].active = false;
  }
}

void clearPowerUpAt(int col, int row) {
  PowerUp *p = powerUpAt(col, row);
  if (p != nullptr) {
    p->active = false;
  }
}

void explodeLine(int originCol, int originRow, int dc, int dr, uint8_t range, unsigned long endAt) {
  for (uint8_t step = 1; step <= range; step++) {
    int col = originCol + dc * step;
    int row = originRow + dr * step;
    if (!inBounds(col, row)) {
      break;
    }
    if (tiles[row][col] == TILE_SOLID) {
      break;
    }

    bool endpoint = (step == range);
    bool horizontal = (dc != 0);
    addFlameCell(col, row, horizontal, endpoint, endAt);
    damageEnemyAt(col, row);
    if (playerIntersectsCell(col, row)) {
      damagePlayer();
    }
    clearPowerUpAt(col, row);

    for (int i = 0; i < MAX_BOMBS; i++) {
      if (bombs[i].active && bombs[i].col == col && bombs[i].row == row) {
        triggerBomb(i);
      }
    }

    if (tiles[row][col] == TILE_BRICK) {
      tiles[row][col] = TILE_EMPTY;
      addPowerUp(col, row);
      endpoint = true;
      break;
    }
  }
}

void triggerBomb(int bombIndex) {
  if (bombIndex < 0 || bombIndex >= MAX_BOMBS || !bombs[bombIndex].active) {
    return;
  }

  Bomb bomb = bombs[bombIndex];
  bombs[bombIndex].active = false;

  unsigned long endAt = millis() + FLAME_MS;
  addFlameCell(bomb.col, bomb.row, false, false, endAt);
  playSfxExplode();

  if (playerIntersectsCell(bomb.col, bomb.row)) {
    damagePlayer();
  }
  damageEnemyAt(bomb.col, bomb.row);
  clearPowerUpAt(bomb.col, bomb.row);

  explodeLine(bomb.col, bomb.row, 1, 0, bomb.range, endAt);
  explodeLine(bomb.col, bomb.row, -1, 0, bomb.range, endAt);
  explodeLine(bomb.col, bomb.row, 0, 1, bomb.range, endAt);
  explodeLine(bomb.col, bomb.row, 0, -1, bomb.range, endAt);
}

void placeBomb() {
  if (activeBombCount() >= player.bombLimit || bombAt(player.col, player.row)) {
    return;
  }

  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!bombs[i].active) {
      bombs[i].active = true;
      bombs[i].col = player.col;
      bombs[i].row = player.row;
      bombs[i].range = player.flameRange;
      bombs[i].explodeAt = millis() + BOMB_FUSE_MS;
      player.ghostBombActive = true;
      player.ghostBombCol = player.col;
      player.ghostBombRow = player.row;
      playSfxPlaceBomb();
      return;
    }
  }
}

void applyPowerUp(PowerUpType type) {
  if (type == POWER_BOMB && player.bombLimit < 5) {
    player.bombLimit++;
  } else if (type == POWER_RANGE && player.flameRange < 5) {
    player.flameRange++;
  } else if (type == POWER_SPEED && player.speedTier < 3) {
    player.speedTier++;
  } else if (type == POWER_PASS_BOMB) {
    player.canPassBombs = true;
  }
  playSfxPickup();
}

bool canPlayerOccupy(float nextX, float nextY) {
  float radius = 8.0f;
  float left = nextX - radius;
  float right = nextX + radius;
  float top = nextY - radius;
  float bottom = nextY + radius;

  int leftCol = (int)((left - ARENA_X) / TILE_SIZE);
  int rightCol = (int)((right - ARENA_X) / TILE_SIZE);
  int topRow = (int)((top - ARENA_Y) / TILE_SIZE);
  int bottomRow = (int)((bottom - ARENA_Y) / TILE_SIZE);

  if (!inBounds(leftCol, topRow) || !inBounds(rightCol, bottomRow)) {
    return false;
  }

  for (int row = topRow; row <= bottomRow; row++) {
    for (int col = leftCol; col <= rightCol; col++) {
      if (tiles[row][col] != TILE_EMPTY) {
        return false;
      }
      if (bombAt(col, row) &&
          !player.canPassBombs &&
          !(player.ghostBombActive && player.ghostBombCol == col && player.ghostBombRow == row)) {
        return false;
      }
    }
  }

  return true;
}

void updatePlayerMovement(unsigned long now) {
  if (!player.alive) {
    return;
  }

  float axisX = 0.0f;
  float axisY = 0.0f;
  if (latestX < 1700) {
    axisX = -((1700.0f - latestX) / 1700.0f);
  } else if (latestX > 2400) {
    axisX = (latestX - 2400.0f) / (4095.0f - 2400.0f);
  }
  if (latestY < 1700) {
    axisY = -((1700.0f - latestY) / 1700.0f);
  } else if (latestY > 2400) {
    axisY = (latestY - 2400.0f) / (4095.0f - 2400.0f);
  }

  if (fabsf(axisX) < 0.12f) axisX = 0.0f;
  if (fabsf(axisY) < 0.12f) axisY = 0.0f;

  player.moving = (axisX != 0.0f || axisY != 0.0f);
  if (!player.moving) {
    syncPlayerCell();
    return;
  }

  if (fabsf(axisX) >= fabsf(axisY)) {
    player.facing = (axisX < 0.0f) ? DIR_LEFT : DIR_RIGHT;
  } else {
    player.facing = (axisY < 0.0f) ? DIR_UP : DIR_DOWN;
  }

  float length = sqrtf(axisX * axisX + axisY * axisY);
  if (length > 1.0f) {
    axisX /= length;
    axisY /= length;
  }

  float speed = (1.35f + player.speedTier * 0.28f) * 4.0f;
  float nextX = player.x + axisX * speed;
  float nextY = player.y + axisY * speed;

  if (canPlayerOccupy(nextX, player.y)) {
    player.x = nextX;
  }
  if (canPlayerOccupy(player.x, nextY)) {
    player.y = nextY;
  }

  syncPlayerCell();
  if (player.ghostBombActive && !playerIntersectsCell(player.ghostBombCol, player.ghostBombRow)) {
    player.ghostBombActive = false;
    player.ghostBombCol = -1;
    player.ghostBombRow = -1;
  }

  PowerUp *picked = powerUpAt(player.col, player.row);
  if (picked != nullptr && playerIntersectsCell(player.col, player.row)) {
    applyPowerUp(picked->type);
    picked->active = false;
  }
}

void chooseEnemyDirection(Enemy &enemy) {
  Direction options[4];
  int optionCount = 0;
  const Direction dirs[4] = {DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT};

  for (int i = 0; i < 4; i++) {
    int dc = 0;
    int dr = 0;
    if (dirs[i] == DIR_LEFT) dc = -1;
    if (dirs[i] == DIR_RIGHT) dc = 1;
    if (dirs[i] == DIR_UP) dr = -1;
    if (dirs[i] == DIR_DOWN) dr = 1;
    if (passableForEnemy(enemy.col + dc, enemy.row + dr)) {
      options[optionCount++] = dirs[i];
    }
  }

  if (optionCount == 0) {
    return;
  }

  if (enemy.type == ENEMY_CHASER) {
    Direction preferred = enemy.dir;
    if (abs(player.col - enemy.col) > abs(player.row - enemy.row)) {
      preferred = (player.col < enemy.col) ? DIR_LEFT : DIR_RIGHT;
    } else if (player.row != enemy.row) {
      preferred = (player.row < enemy.row) ? DIR_UP : DIR_DOWN;
    }
    for (int i = 0; i < optionCount; i++) {
      if (options[i] == preferred) {
        enemy.dir = preferred;
        return;
      }
    }
  }

  enemy.dir = options[random(0, optionCount)];
}

void updateEnemies(unsigned long now) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy &enemy = enemies[i];
    if (!enemy.active) {
      continue;
    }
    uint16_t moveDelay = ENEMY_STEP_MS;
    if (enemy.type == ENEMY_CHASER) {
      moveDelay = 145;
    }
    int speedBoost = stageNumber * 6;
    if (speedBoost > 40) {
      speedBoost = 40;
    }
    int adjustedDelay = (int)moveDelay - speedBoost;
    if (adjustedDelay < 90) {
      adjustedDelay = 90;
    }
    moveDelay = (uint16_t)adjustedDelay;
    float enemySpeed = (float)TILE_SIZE / (float)moveDelay * (float)GAME_UPDATE_INTERVAL_MS;

    if (!enemy.moving) {
      if (now >= enemy.nextMoveMs) {
        if (random(0, 100) < 28) {
          chooseEnemyDirection(enemy);
        }
        if (chooseEnemyTarget(enemy)) {
          enemy.nextMoveMs = now + moveDelay;
        }
      }
    }

    if (enemy.moving) {
      float targetX = tileCenterX(enemy.targetCol);
      float targetY = tileCenterY(enemy.targetRow);
      float dx = targetX - enemy.x;
      float dy = targetY - enemy.y;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist <= enemySpeed || dist < 0.8f) {
        enemy.x = targetX;
        enemy.y = targetY;
        enemy.col = enemy.targetCol;
        enemy.row = enemy.targetRow;
        enemy.moving = false;
        if (enemy.type == ENEMY_CHASER || random(0, 100) < 20) {
          chooseEnemyDirection(enemy);
        }
      } else if (dist > 0.0f) {
        enemy.x += dx / dist * enemySpeed;
        enemy.y += dy / dist * enemySpeed;
      }
    }

    if (enemyIntersectsPlayer(enemy)) {
      damagePlayer();
    }
  }
}

void updateBombs(unsigned long now) {
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (bombs[i].active && now >= bombs[i].explodeAt) {
      triggerBomb(i);
    }
  }
}

void updateFlames(unsigned long now) {
  for (int i = 0; i < MAX_FLAMES; i++) {
    if (!flames[i].active) {
      continue;
    }
    if (now >= flames[i].endAt) {
      flames[i].active = false;
      continue;
    }
    if (playerIntersectsCell(flames[i].col, flames[i].row)) {
      damagePlayer();
    }
    damageEnemyAt(flames[i].col, flames[i].row);
  }
}

bool allEnemiesDefeated() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      return false;
    }
  }
  return true;
}

void updateGameStep() {
  unsigned long now = millis();
  updatePlayerMovement(now);
  if (bluePressed) {
    placeBomb();
  }
  updateBombs(now);
  updateFlames(now);
  updateEnemies(now);

  if (allEnemiesDefeated()) {
    playSfxStageClear();
    snprintf(bannerText, sizeof(bannerText), "CLEAR");
    gameState = STAGE_CLEAR;
    stateTimerMs = now;
  }
}

template <typename CanvasT>
void drawArenaBackground(CanvasT &canvas) {
  canvas.fillRect(0, 0, SCREEN_W, SCREEN_H, rgb565(28, 30, 36));
  canvas.fillRect(ARENA_X - 4, ARENA_Y - 4, ARENA_W + 8, ARENA_H + 8, rgb565(66, 70, 82));
  canvas.fillRect(ARENA_X, ARENA_Y, ARENA_W, ARENA_H, rgb565(160, 170, 184));
  for (int row = 0; row < ARENA_ROWS; row++) {
    for (int col = 0; col < ARENA_COLS; col++) {
      int x = ARENA_X + col * TILE_SIZE;
      int y = ARENA_Y + row * TILE_SIZE;
      uint16_t shade = (((row + col) & 1) == 0) ? rgb565(188, 198, 212) : rgb565(176, 186, 202);
      canvas.fillRect(x, y, TILE_SIZE, TILE_SIZE, shade);
    }
  }
  canvas.fillRoundRect(HUD_X, 6, HUD_W, SCREEN_H - 12, 10, rgb565(26, 32, 44));
}

template <typename CanvasT>
void drawSolidTile(CanvasT &canvas, int x, int y) {
  canvas.fillRect(x + 1, y + 1, TILE_SIZE - 2, TILE_SIZE - 2, rgb565(88, 94, 110));
  canvas.fillRect(x + 4, y + 4, TILE_SIZE - 8, TILE_SIZE - 8, rgb565(120, 126, 144));
  canvas.drawFastHLine(x + 3, y + 6, TILE_SIZE - 6, rgb565(154, 160, 176));
  canvas.drawFastVLine(x + 6, y + 3, TILE_SIZE - 6, rgb565(154, 160, 176));
}

template <typename CanvasT>
void drawBrickTile(CanvasT &canvas, int x, int y) {
  canvas.fillRect(x + 2, y + 3, TILE_SIZE - 4, TILE_SIZE - 6, rgb565(176, 96, 50));
  canvas.drawRect(x + 2, y + 3, TILE_SIZE - 4, TILE_SIZE - 6, rgb565(110, 54, 24));
  canvas.drawFastHLine(x + 4, y + 10, TILE_SIZE - 8, rgb565(215, 150, 86));
  canvas.drawFastHLine(x + 4, y + 18, TILE_SIZE - 8, rgb565(139, 70, 30));
  canvas.drawFastVLine(x + 11, y + 5, 6, rgb565(110, 54, 24));
  canvas.drawFastVLine(x + 19, y + 12, 7, rgb565(110, 54, 24));
}

template <typename CanvasT>
void drawTiles(CanvasT &canvas) {
  for (int row = 0; row < ARENA_ROWS; row++) {
    for (int col = 0; col < ARENA_COLS; col++) {
      int x = ARENA_X + col * TILE_SIZE;
      int y = ARENA_Y + row * TILE_SIZE;
      if (tiles[row][col] == TILE_SOLID) {
        drawSolidTile(canvas, x, y);
      } else if (tiles[row][col] == TILE_BRICK) {
        drawBrickTile(canvas, x, y);
      }
    }
  }
}

template <typename CanvasT>
void drawBombs(CanvasT &canvas) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!bombs[i].active) {
      continue;
    }
    int cx = ARENA_X + bombs[i].col * TILE_SIZE + TILE_SIZE / 2;
    int cy = ARENA_Y + bombs[i].row * TILE_SIZE + TILE_SIZE / 2 + 1;
    bool flash = (bombs[i].explodeAt - now < 400) && (((now / 70) & 1) == 0);
    uint16_t body = flash ? rgb565(255, 220, 120) : rgb565(20, 20, 26);
    canvas.fillCircle(cx, cy, 8, body);
    canvas.fillCircle(cx - 3, cy - 3, 2, TFT_WHITE);
    canvas.drawLine(cx, cy - 9, cx + 3, cy - 14, rgb565(255, 160, 48));
    canvas.fillCircle(cx + 4, cy - 15, 2, flash ? TFT_RED : rgb565(255, 180, 60));
  }
}

template <typename CanvasT>
void drawFlames(CanvasT &canvas) {
  for (int i = 0; i < MAX_FLAMES; i++) {
    if (!flames[i].active) {
      continue;
    }
    int x = ARENA_X + flames[i].col * TILE_SIZE;
    int y = ARENA_Y + flames[i].row * TILE_SIZE;
    uint16_t outer = rgb565(255, 116, 36);
    uint16_t inner = rgb565(255, 228, 96);
    if (!flames[i].horizontal) {
      canvas.fillRect(x + 9, y + 2, 10, TILE_SIZE - 4, outer);
      canvas.fillRect(x + 11, y + 5, 6, TILE_SIZE - 10, inner);
    } else {
      canvas.fillRect(x + 2, y + 9, TILE_SIZE - 4, 10, outer);
      canvas.fillRect(x + 5, y + 11, TILE_SIZE - 10, 6, inner);
    }
    if (flames[i].endpoint) {
      canvas.fillCircle(x + TILE_SIZE / 2, y + TILE_SIZE / 2, 6, inner);
    } else {
      canvas.fillCircle(x + TILE_SIZE / 2, y + TILE_SIZE / 2, 4, inner);
    }
  }
}

template <typename CanvasT>
void drawPowerUps(CanvasT &canvas) {
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (!powerUps[i].active) {
      continue;
    }
    int x = ARENA_X + powerUps[i].col * TILE_SIZE;
    int y = ARENA_Y + powerUps[i].row * TILE_SIZE;
    uint16_t color = TFT_WHITE;
    if (powerUps[i].type == POWER_BOMB) color = rgb565(90, 200, 255);
    if (powerUps[i].type == POWER_RANGE) color = rgb565(255, 188, 64);
    if (powerUps[i].type == POWER_SPEED) color = rgb565(120, 255, 148);
    if (powerUps[i].type == POWER_PASS_BOMB) color = rgb565(232, 120, 255);
    canvas.fillRoundRect(x + 7, y + 7, 14, 14, 4, color);
    canvas.drawRoundRect(x + 7, y + 7, 14, 14, 4, TFT_BLACK);
  }
}

template <typename CanvasT>
void drawPlayer(CanvasT &canvas) {
  if (!player.alive) {
    return;
  }
  unsigned long now = millis();
  bool blink = (now < player.invulnUntil) && (((now / 90) & 1) == 0);
  if (blink) {
    return;
  }
  int left = (int)(player.x - (BOMBER_W * BOMBER_SCALE) * 0.5f);
  int top = (int)(player.y - (BOMBER_H * BOMBER_SCALE) * 0.5f - 1.0f);
  uint8_t frameIndex = player.moving ? ((now / 95) & 0x03) : 0;
  const uint8_t *frame = bomberFront0;
  bool flipH = false;

  if (player.facing == DIR_UP) {
    const uint8_t *frames[4] = {bomberBack0, bomberBack1, bomberBack2, bomberBack3};
    frame = frames[frameIndex];
  } else if (player.facing == DIR_DOWN) {
    const uint8_t *frames[4] = {bomberFront0, bomberFront1, bomberFront2, bomberFront3};
    frame = frames[frameIndex];
  } else {
    const uint8_t *frames[4] = {bomberSide0, bomberSide1, bomberSide2, bomberSide3};
    frame = frames[frameIndex];
    flipH = (player.facing == DIR_LEFT);
  }

  canvas.fillEllipse((int)player.x, (int)(player.y + 12), 9, 3, rgb565(110, 120, 138));
  drawIndexedSprite(canvas, left, top, frame, BOMBER_W, BOMBER_H, bomberPalette, BOMBER_SCALE, flipH);
  if (player.facing == DIR_UP || player.facing == DIR_DOWN) {
    canvas.drawLine((int)player.x, top - 1, (int)player.x, top + 2, rgb565(255, 120, 160));
    canvas.fillCircle((int)player.x, top - 2, 2, rgb565(255, 120, 160));
  }
}

template <typename CanvasT>
void drawEnemy(CanvasT &canvas, const Enemy &enemy) {
  if (!enemy.active) {
    return;
  }
  int x = (int)(enemy.x - TILE_SIZE * 0.5f);
  int y = (int)(enemy.y - TILE_SIZE * 0.5f);
  static const uint16_t slimeBase[5] = {
    rgb565(92, 230, 140),
    rgb565(96, 186, 255),
    rgb565(255, 146, 108),
    rgb565(210, 132, 255),
    rgb565(255, 214, 92)
  };
  static const uint16_t slimeShade[5] = {
    rgb565(42, 148, 78),
    rgb565(40, 104, 176),
    rgb565(176, 80, 44),
    rgb565(124, 74, 172),
    rgb565(176, 136, 36)
  };
  bool frame = enemy.moving ? (((millis() / 140) & 1) == 0) : false;
  uint8_t variant = enemy.colorVariant % 5;
  uint16_t body = slimeBase[variant];
  uint16_t shade = slimeShade[variant];
  int bodyY = frame ? y + 8 : y + 9;
  int bodyH = frame ? 13 : 11;
  int bodyW = frame ? 18 : 20;
  int bodyX = x + (TILE_SIZE - bodyW) / 2;

  canvas.fillEllipse(x + 14, y + 25, 9, 3, rgb565(108, 118, 136));
  canvas.fillRoundRect(bodyX, bodyY, bodyW, bodyH, 6, body);
  canvas.fillRoundRect(bodyX + 1, bodyY + bodyH / 2, bodyW - 2, bodyH / 2 + 1, 5, shade);
  canvas.fillCircle(bodyX + bodyW / 2, bodyY + 2, 7, body);
  canvas.fillEllipse(bodyX + 5, bodyY + 3, 3, 2, TFT_WHITE);
  canvas.fillEllipse(bodyX + 8, bodyY + 5, 2, 1, rgb565(255, 245, 210));
  canvas.fillRect(bodyX + 5, bodyY + 5, 2, 2, TFT_BLACK);
  canvas.fillRect(bodyX + bodyW - 7, bodyY + 5, 2, 2, TFT_BLACK);
  if (frame) {
    canvas.drawFastHLine(bodyX + 7, bodyY + bodyH - 2, bodyW - 14, TFT_WHITE);
  } else {
    canvas.drawFastHLine(bodyX + 6, bodyY + bodyH - 2, bodyW - 12, TFT_WHITE);
  }
}

template <typename CanvasT>
void drawActors(CanvasT &canvas) {
  drawPlayer(canvas);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    drawEnemy(canvas, enemies[i]);
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.setTextColor(TFT_WHITE, rgb565(26, 32, 44));
  canvas.setTextSize(1);
  canvas.setCursor(HUD_X + 4, 18);
  canvas.printf("BMBR");
  canvas.setCursor(HUD_X + 4, 40);
  canvas.printf("STG%d", stageNumber);
  canvas.setCursor(HUD_X + 4, 70);
  canvas.printf("LIF%d", player.lives);
  canvas.setCursor(HUD_X + 4, 92);
  canvas.printf("BMB%d", player.bombLimit);
  canvas.setCursor(HUD_X + 4, 114);
  canvas.printf("PWR%d", player.flameRange);
  canvas.setCursor(HUD_X + 4, 136);
  canvas.printf("SPD%d", player.speedTier);
  canvas.setCursor(HUD_X + 4, 158);
  canvas.printf("PAS%s", player.canPassBombs ? "Y" : "N");

  canvas.setCursor(HUD_X + 4, 206);
  canvas.printf("MOVE");
  canvas.setCursor(HUD_X + 4, 220);
  canvas.printf("STCK");
  canvas.setCursor(HUD_X + 4, 248);
  canvas.printf("BLUE");
  canvas.setCursor(HUD_X + 4, 262);
  canvas.printf("SET");
  canvas.setCursor(HUD_X + 4, 276);
  canvas.printf("BOMB");
}

template <typename CanvasT>
void drawScene(CanvasT &canvas) {
  drawArenaBackground(canvas);
  drawTiles(canvas);
  drawPowerUps(canvas);
  drawBombs(canvas);
  drawFlames(canvas);
  drawActors(canvas);
  drawHud(canvas);
}

void renderPlaying() {
  if (frameBufferReady) {
    drawScene(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    tft.fillScreen(TFT_BLACK);
    drawScene(tft);
  }
}

void drawTitleBombArt() {
  const int cx = 364;
  const int cy = 182;
  uint16_t glowOuter = rgb565(62, 42, 32);
  uint16_t glowInner = rgb565(92, 58, 44);
  uint16_t bombDark = rgb565(24, 24, 30);
  uint16_t bombMid = rgb565(44, 46, 56);
  uint16_t bombLight = rgb565(114, 120, 142);
  uint16_t fuse = rgb565(224, 176, 102);
  uint16_t spark = rgb565(255, 212, 92);

  tft.fillCircle(cx, cy + 3, 54, glowOuter);
  tft.fillCircle(cx, cy + 3, 38, glowInner);
  tft.fillCircle(cx, cy + 4, 34, bombDark);
  tft.fillCircle(cx, cy + 3, 30, bombMid);
  tft.fillCircle(cx - 10, cy - 8, 11, bombLight);
  tft.fillCircle(cx - 14, cy - 12, 4, rgb565(228, 236, 255));
  tft.drawCircle(cx, cy + 3, 30, rgb565(180, 190, 220));

  tft.fillRoundRect(cx - 10, cy - 34, 20, 10, 3, rgb565(96, 102, 128));
  tft.fillRect(cx - 2, cy - 40, 4, 8, fuse);
  tft.drawLine(cx, cy - 40, cx + 8, cy - 48, fuse);
  tft.drawLine(cx + 8, cy - 48, cx + 15, cy - 42, fuse);

  tft.fillCircle(cx + 19, cy - 42, 8, spark);
  tft.fillCircle(cx + 19, cy - 42, 5, rgb565(255, 242, 162));
  tft.fillCircle(cx + 19, cy - 42, 2, TFT_WHITE);
  tft.drawLine(cx + 19, cy - 56, cx + 19, cy - 49, spark);
  tft.drawLine(cx + 19, cy - 35, cx + 19, cy - 28, spark);
  tft.drawLine(cx + 5, cy - 42, cx + 12, cy - 42, spark);
  tft.drawLine(cx + 26, cy - 42, cx + 33, cy - 42, spark);
}

void drawTitle() {
  tft.fillScreen(rgb565(18, 22, 34));
  tft.fillRoundRect(32, 28, 416, 88, 16, rgb565(30, 46, 80));
  tft.fillRoundRect(48, 138, 382, 116, 12, rgb565(38, 32, 46));
  drawTitleBombArt();

  tft.setTextColor(TFT_WHITE, rgb565(30, 46, 80));
  tft.setTextSize(3);
  tft.setCursor(76, 58);
  tft.print("BOMBER");

  tft.setTextSize(2);
  tft.setTextColor(rgb565(255, 220, 120), rgb565(38, 32, 46));
  tft.setCursor(86, 156);
  tft.print("Joystick: Move");
  tft.setCursor(86, 186);
  tft.print("Blue Button: Place Bomb");
  tft.setCursor(86, 216);
  tft.print("Green Button: Start");

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, rgb565(18, 22, 34));
  tft.setCursor(96, 282);
  tft.print("Grid action game with staged explosions and pickup progression");
}

void drawBannerScreen(uint16_t bg, const char *line1, const char *line2) {
  tft.fillScreen(bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextSize(3);
  tft.setCursor(132, 102);
  tft.print(line1);
  tft.setTextSize(2);
  tft.setCursor(92, 154);
  tft.print(line2);
}

void setup() {
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  initFrameBuffer();
  setupHardware();

  resetPlayer();
  drawTitle();
  lastRenderedState = TITLE;
  bgmIndex = 0;
  bgmPhase = 0.0f;
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

  if (gameState == TITLE) {
    if (greenPressed || bluePressed) {
      beginGame();
    }
  } else if (gameState == STAGE_INTRO) {
    if (millis() - stateTimerMs >= 900) {
      gameState = PLAYING;
      lastGameUpdateMs = millis();
    }
  } else if (gameState == PLAYING) {
    unsigned long now = millis();
    if (now - lastGameUpdateMs >= GAME_UPDATE_INTERVAL_MS) {
      lastGameUpdateMs = now;
      updateGameStep();
    }
  } else if (gameState == STAGE_CLEAR) {
    if (millis() - stateTimerMs >= STAGE_CLEAR_MS) {
      stageNumber++;
      buildStage();
    }
  } else if (gameState == GAME_OVER) {
    if (greenPressed || bluePressed) {
      beginGame();
    }
  }

  updateVibration();

  if (gameState == TITLE) {
    if (lastRenderedState != TITLE) {
      drawTitle();
      lastRenderedState = TITLE;
    }
  } else if (gameState == STAGE_INTRO) {
    if (lastRenderedState != STAGE_INTRO) {
      drawBannerScreen(rgb565(22, 30, 60), bannerText, "Blue places bombs");
      lastRenderedState = STAGE_INTRO;
    }
  } else if (gameState == PLAYING) {
    lastRenderedState = PLAYING;
    renderPlaying();
  } else if (gameState == STAGE_CLEAR) {
    if (lastRenderedState != STAGE_CLEAR) {
      drawBannerScreen(rgb565(12, 78, 52), bannerText, "Next stage loading");
      lastRenderedState = STAGE_CLEAR;
    }
  } else if (gameState == GAME_OVER) {
    if (lastRenderedState != GAME_OVER) {
      drawBannerScreen(rgb565(92, 18, 18), "GAME OVER", "Green or Blue to restart");
      lastRenderedState = GAME_OVER;
    }
  }

  delay(1);
}
