#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

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
TFT_eSprite frameBufferLeft = TFT_eSprite(&tft);
TFT_eSprite frameBufferRight = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int FRAME_W = SCREEN_W / 2;
static const int HUD_H = 24;
static const uint16_t GAME_STEP_MS = 8;
static const int MAX_PLAYER_BULLETS = 20;
static const int MAX_ENEMIES = 8;
static const int MAX_ENEMY_BULLETS = 10;
static const int MAX_PICKUPS = 6;
static const int MAX_EXPLOSIONS = 12;
static const int STAR_COUNT = 44;

enum GameState {
  TITLE,
  PLAYING,
  BOSS,
  GAME_OVER,
  CLEAR
};

enum EnemyKind {
  ENEMY_NONE,
  ENEMY_SCOUT,
  ENEMY_WAVE,
  ENEMY_TURRET
};

enum PickupKind {
  PICKUP_NONE,
  PICKUP_POWER,
  PICKUP_SHIELD,
  PICKUP_SPECIAL
};

struct Voice {
  bool active;
  float freq;
  float phase;
  int remainSamples;
  float volume;
  uint8_t wave;
};

struct Note {
  uint16_t freq;
  uint16_t ms;
};

struct Player {
  float x;
  float y;
  float vx;
  float vy;
  int lives;
  int hp;
  bool shield;
  unsigned long shieldUntil;
  unsigned long respawnUntil;
  unsigned long fireCooldownUntil;
  unsigned long specialCooldownUntil;
  bool spreadShot;
  uint8_t specialAmmo;
};

struct Bullet {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  uint8_t damage;
};

struct Enemy {
  bool active;
  EnemyKind kind;
  float x;
  float y;
  float vx;
  float vy;
  float baseY;
  float phase;
  uint8_t hp;
  unsigned long fireAt;
};

struct Pickup {
  bool active;
  PickupKind kind;
  float x;
  float y;
  float vx;
};

struct Explosion {
  bool active;
  float x;
  float y;
  uint8_t radius;
  unsigned long endAt;
};

struct Boss {
  bool active;
  float x;
  float y;
  float vy;
  uint8_t coreHp;
  uint8_t podHp[2];
  unsigned long nextVolleyAt;
  unsigned long nextMoveAt;
};

struct SpecialWeapon {
  bool active;
  bool exploding;
  bool damageApplied;
  float x;
  float y;
  float targetX;
  uint8_t frame;
  unsigned long frameAt;
};

struct Star {
  int x;
  int y;
  uint8_t speed;
  uint16_t color;
};

struct WindowCanvas {
  TFT_eSprite *sprite;
  int xOffset;

  void fillScreen(uint16_t color) { sprite->fillScreen(color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) { sprite->fillRect(x - xOffset, y, w, h, color); }
  void drawFastHLine(int x, int y, int w, uint16_t color) { sprite->drawFastHLine(x - xOffset, y, w, color); }
  void drawFastVLine(int x, int y, int h, uint16_t color) { sprite->drawFastVLine(x - xOffset, y, h, color); }
  void drawPixel(int x, int y, uint16_t color) { sprite->drawPixel(x - xOffset, y, color); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    sprite->fillTriangle(x0 - xOffset, y0, x1 - xOffset, y1, x2 - xOffset, y2, color);
  }
  void fillCircle(int x, int y, int r, uint16_t color) { sprite->fillCircle(x - xOffset, y, r, color); }
  void drawCircle(int x, int y, int r, uint16_t color) { sprite->drawCircle(x - xOffset, y, r, color); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    sprite->fillRoundRect(x - xOffset, y, w, h, r, color);
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    sprite->drawRoundRect(x - xOffset, y, w, h, r, color);
  }
  void drawRect(int x, int y, int w, int h, uint16_t color) { sprite->drawRect(x - xOffset, y, w, h, color); }
  void setTextColor(uint16_t fg, uint16_t bg) { sprite->setTextColor(fg, bg); }
  void setTextDatum(uint8_t datum) { sprite->setTextDatum(datum); }
  int drawString(const char *text, int x, int y, int font) { return sprite->drawString(text, x - xOffset, y, font); }
  int drawString(const String &text, int x, int y, int font) { return sprite->drawString(text, x - xOffset, y, font); }
  int drawRightString(const char *text, int x, int y, int font) { return sprite->drawRightString(text, x - xOffset, y, font); }
  int drawCentreString(const char *text, int x, int y, int font) { return sprite->drawCentreString(text, x - xOffset, y, font); }
};

static const char *PLAYER_SPRITE[] = {
  "1...........",
  "211.........",
  "32111.......",
  "43322111....",
  "5544332211..",
  "666666654321",
  "5544332211..",
  "43322111....",
  "32111.......",
  "211.........",
  "1..........."
};

static const char *SCOUT_SPRITE[] = {
  "........11..",
  "......11221.",
  "...111233321",
  ".11223344331",
  "123344555551",
  ".11223344331",
  "...111233321",
  "......11221.",
  "........11.."
};

static const char *WAVE_SPRITE[] = {
  ".......11....",
  ".....11221...",
  "...112333211.",
  ".112334443321",
  "123445666666",
  ".112334443321",
  "...112333211.",
  ".....11221..."
};

static const char *TURRET_SPRITE[] = {
  "......1111...",
  "...111222211.",
  ".112233333221",
  "123344454332",
  "123456666666",
  "123344774332",
  ".112233333221",
  "...111222211."
};

static const char *BOSS_BODY_SPRITE[] = {
  "................111.....",
  "...........1111222211...",
  "......1111122233332211..",
  "...11122223334444433221.",
  ".11222333444555554433221",
  "122333444556666665443221",
  "123344556667777776654321",
  "123455667788888887765432",
  "123456778899999999999999",
  "123455667788888887765432",
  "123344556667777776654321",
  "122333444556666665443221",
  ".11222333444555554433221",
  "...11122223334444433221.",
  "......1111122233332211..",
  "...........1111222211..."
};

static const char *BOSS_POD_SPRITE[] = {
  ".....11...",
  "...11221..",
  ".112333211",
  "1233444431",
  ".112333211",
  "...11221..",
  ".....11..."
};

static const char *POWER_PICKUP_SPRITE[] = {
  "..1111..",
  ".122221.",
  "12366321",
  "12355321",
  "12366321",
  ".122221.",
  "..1111.."
};

static const char *SHIELD_PICKUP_SPRITE[] = {
  "..1111..",
  ".122221.",
  "12344321",
  "12355321",
  "12344321",
  ".122221.",
  "..1111.."
};

static const char *SPECIAL_PICKUP_SPRITE[] = {
  "...11...",
  "..1221..",
  ".123321.",
  "12366321",
  ".123321.",
  "..1221..",
  "...11..."
};

GameState gameState = TITLE;
GameState lastRenderedState = CLEAR;

Player player;
Bullet playerBullets[MAX_PLAYER_BULLETS];
Enemy enemies[MAX_ENEMIES];
Bullet enemyBullets[MAX_ENEMY_BULLETS];
Pickup pickups[MAX_PICKUPS];
Explosion explosions[MAX_EXPLOSIONS];
Boss boss;
SpecialWeapon specialWeapon;
Star stars[STAR_COUNT];

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
int latestX = 2048;
int latestY = 2048;

bool vibrating = false;
bool vibForward = true;
bool vibContinuous = false;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;

bool frameBufferReady = false;
TaskHandle_t audioTaskHandle = nullptr;
Voice sfxVoices[4];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
float bgmPhase = 0.0f;
int bgmIndex = 0;
int bgmRemainSamples = 0;
unsigned long lastStepMs = 0;
unsigned long stageStartMs = 0;
unsigned long lastEnemySpawnMs = 0;
unsigned long titleAnimMs = 0;
unsigned long statusBlinkMs = 0;
float scrollX = 0.0f;
char statusText[24] = "";

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
  {392, 140}, {523, 140}, {659, 140}, {784, 220},
  {659, 140}, {523, 140}, {440, 140}, {392, 220},
  {392, 140}, {523, 140}, {659, 140}, {880, 220},
  {784, 140}, {659, 140}, {523, 140}, {440, 220},
  {330, 140}, {392, 140}, {523, 140}, {659, 220},
  {523, 140}, {440, 140}, {392, 140}, {330, 220},
  {294, 140}, {392, 140}, {440, 140}, {523, 220},
  {392, 140}, {330, 140}, {262, 140}, {0, 220}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t spriteColor(char code, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4,
                     uint16_t c5, uint16_t c6, uint16_t c7, uint16_t c8, uint16_t c9) {
  switch (code) {
    case '1': return c1;
    case '2': return c2;
    case '3': return c3;
    case '4': return c4;
    case '5': return c5;
    case '6': return c6;
    case '7': return c7;
    case '8': return c8;
    case '9': return c9;
    default: return TFT_BLACK;
  }
}

template <typename CanvasT>
void drawPixelSprite(CanvasT &canvas, int centerX, int centerY, const char *const *rows,
                     int rowCount, int pixelSize,
                     uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4,
                     uint16_t c5, uint16_t c6, uint16_t c7 = TFT_BLACK,
                     uint16_t c8 = TFT_BLACK, uint16_t c9 = TFT_BLACK) {
  int spriteH = rowCount * pixelSize;
  int spriteW = (int)strlen(rows[0]) * pixelSize;
  int startX = centerX - spriteW / 2;
  int startY = centerY - spriteH / 2;

  for (int y = 0; y < rowCount; y++) {
    const char *row = rows[y];
    for (int x = 0; row[x] != '\0'; x++) {
      if (row[x] == '.') {
        continue;
      }
      uint16_t color = spriteColor(row[x], c1, c2, c3, c4, c5, c6, c7, c8, c9);
      canvas.fillRect(startX + x * pixelSize, startY + y * pixelSize, pixelSize, pixelSize, color);
    }
  }
}

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool overlapRect(float ax0, float ay0, float ax1, float ay1,
                 float bx0, float by0, float bx1, float by1) {
  return !(ax1 < bx0 || ax0 > bx1 || ay1 < by0 || ay0 > by1);
}

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startVibration(uint16_t durationMs = 120) {
  vibrating = true;
  vibForward = true;
  vibContinuous = false;
  vibEndMs = millis() + durationMs;
  vibLastToggleMs = 0;
}

void startContinuousVibration(uint16_t durationMs) {
  vibrating = true;
  vibForward = true;
  vibContinuous = true;
  vibEndMs = millis() + durationMs;
  vibLastToggleMs = 0;
}

void updateVibration() {
  if (!vibrating) {
    stopMotor();
    return;
  }

  unsigned long now = millis();
  if (now >= vibEndMs) {
    vibrating = false;
    stopMotor();
    return;
  }

  if (vibContinuous) {
    if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 12) {
      vibLastToggleMs = now;
      vibForward = !vibForward;
      const int duty = 190;
      if (vibForward) {
        ledcWrite(forward_ch, duty);
        ledcWrite(reverse_ch, 0);
      } else {
        ledcWrite(forward_ch, 0);
        ledcWrite(reverse_ch, duty);
      }
    }
    return;
  }

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 32) {
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

void assignVoice(Voice &voice, float freq, int durationSamples, float volume, uint8_t wave) {
  voice.active = true;
  voice.freq = freq;
  voice.phase = 0.0f;
  voice.remainSamples = durationSamples;
  voice.volume = volume;
  voice.wave = wave;
}

void startVoice(float freq, int ms, float volume, uint8_t wave, int preferredChannel = -1) {
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  taskENTER_CRITICAL(&audioMux);
  if (preferredChannel >= 0 && preferredChannel < 4) {
    assignVoice(sfxVoices[preferredChannel], freq, durationSamples, volume, wave);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }
  for (int i = 0; i < 4; i++) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }
  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxShoot() { startVoice(880.0f, 45, 0.10f, 0); }
void playSfxAlt() { startVoice(660.0f, 90, 0.11f, 0); }
void playSfxHit() { startVoice(240.0f, 80, 0.10f, 2); }
void playSfxExplode() { startVoice(110.0f, 180, 0.14f, 2); }
void playSfxPickup() { startVoice(740.0f, 70, 0.10f, 0); startVoice(988.0f, 80, 0.10f, 0); }

float bgmSample() {
  if (gameState != PLAYING && gameState != BOSS) {
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
  float sample = (bgmPhase < 0.5f) ? 0.010f : -0.010f;
  bgmPhase += (float)freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  return sample;
}

float waveSample(Voice &voice) {
  float sample = 0.0f;
  if (voice.wave == 0) {
    sample = (voice.phase < 0.5f) ? 1.0f : -1.0f;
  } else if (voice.wave == 1) {
    sample = sinf(voice.phase * 2.0f * PI);
  } else {
    sample = (float)random(-1000, 1000) / 1000.0f;
  }
  voice.phase += voice.freq / SAMPLE_RATE;
  if (voice.phase >= 1.0f) {
    voice.phase -= 1.0f;
  }
  return sample * voice.volume;
}

void audioTick() {
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; i++) {
    float mix = bgmSample();
    taskENTER_CRITICAL(&audioMux);
    for (int j = 0; j < 4; j++) {
      if (!sfxVoices[j].active) {
        continue;
      }
      mix += waveSample(sfxVoices[j]);
      sfxVoices[j].remainSamples--;
      if (sfxVoices[j].remainSamples <= 0) {
        sfxVoices[j].active = false;
      }
    }
    taskEXIT_CRITICAL(&audioMux);
    mix = clampf(mix, -0.6f, 0.6f);
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

void initFrameBuffer() {
  frameBufferLeft.setColorDepth(16);
  frameBufferRight.setColorDepth(16);
  bool leftReady = (frameBufferLeft.createSprite(FRAME_W, SCREEN_H) != nullptr);
  bool rightReady = (frameBufferRight.createSprite(FRAME_W, SCREEN_H) != nullptr);
  frameBufferReady = leftReady && rightReady;
  if (!frameBufferReady) {
    frameBufferLeft.deleteSprite();
    frameBufferRight.deleteSprite();
  }
}

void readButtons() {
  bool g = (digitalRead(green_btn_pin) == LOW);
  bool b = (digitalRead(blue_btn_pin) == LOW);
  greenPressed = (g && !greenDown);
  bluePressed = (b && !blueDown);
  greenDown = g;
  blueDown = b;
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);
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

void resetArrays() {
  memset(playerBullets, 0, sizeof(playerBullets));
  memset(enemies, 0, sizeof(enemies));
  memset(enemyBullets, 0, sizeof(enemyBullets));
  memset(pickups, 0, sizeof(pickups));
  memset(explosions, 0, sizeof(explosions));
  memset(&boss, 0, sizeof(boss));
  memset(&specialWeapon, 0, sizeof(specialWeapon));
}

void resetStars() {
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].x = random(0, SCREEN_W);
    stars[i].y = random(HUD_H + 4, SCREEN_H);
    stars[i].speed = random(1, 4);
    uint8_t tone = 120 + random(0, 100);
    stars[i].color = rgb565(tone, tone, 180 + random(0, 70));
  }
}

void spawnExplosion(float x, float y, uint8_t radius) {
  for (int i = 0; i < MAX_EXPLOSIONS; i++) {
    if (explosions[i].active) {
      continue;
    }
    explosions[i].active = true;
    explosions[i].x = x;
    explosions[i].y = y;
    explosions[i].radius = radius;
    explosions[i].endAt = millis() + 140;
    return;
  }
}

void beginGame() {
  resetArrays();
  player.x = 78.0f;
  player.y = SCREEN_H / 2.0f;
  player.vx = 0.0f;
  player.vy = 0.0f;
  player.lives = 3;
  player.hp = 3;
  player.shield = false;
  player.shieldUntil = 0;
  player.respawnUntil = 0;
  player.fireCooldownUntil = 0;
  player.specialCooldownUntil = 0;
  player.spreadShot = false;
  player.specialAmmo = 3;

  stageStartMs = millis();
  lastEnemySpawnMs = stageStartMs;
  lastStepMs = stageStartMs;
  titleAnimMs = stageStartMs;
  statusBlinkMs = stageStartMs;
  scrollX = 0.0f;
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  snprintf(statusText, sizeof(statusText), "SORTIE");
  resetStars();
  gameState = PLAYING;
}

void setStatus(const char *text) {
  snprintf(statusText, sizeof(statusText), "%s", text);
  statusBlinkMs = millis();
}

void loseLife() {
  if (player.shield) {
    player.shield = false;
    player.shieldUntil = 0;
    playSfxHit();
    startVibration(80);
    return;
  }

  player.hp--;
  playSfxHit();
  startVibration(120);
  if (player.hp > 0) {
    player.respawnUntil = millis() + 1000;
    player.x = 78.0f;
    player.y = SCREEN_H / 2.0f;
    player.vx = 0.0f;
    player.vy = 0.0f;
    return;
  }

  player.lives--;
  if (player.lives <= 0) {
    gameState = GAME_OVER;
    setStatus("MISSION FAIL");
    return;
  }

  player.hp = 3;
  player.shield = false;
  player.respawnUntil = millis() + 1200;
  player.x = 78.0f;
  player.y = SCREEN_H / 2.0f;
  player.vx = 0.0f;
  player.vy = 0.0f;
}

void spawnPlayerBullet(float x, float y, float vx, float vy, uint8_t damage) {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (playerBullets[i].active) {
      continue;
    }
    playerBullets[i].active = true;
    playerBullets[i].x = x;
    playerBullets[i].y = y;
    playerBullets[i].vx = vx;
    playerBullets[i].vy = vy;
    playerBullets[i].damage = damage;
    return;
  }
}

void firePrimary() {
  unsigned long now = millis();
  if (now < player.fireCooldownUntil || now < player.respawnUntil) {
    return;
  }
  player.fireCooldownUntil = now + 120;
  if (player.spreadShot) {
    spawnPlayerBullet(player.x + 16.0f, player.y, 1.55f, 0.0f, 1);
    spawnPlayerBullet(player.x + 12.0f, player.y - 5.0f, 1.45f, -0.18f, 1);
    spawnPlayerBullet(player.x + 12.0f, player.y + 5.0f, 1.45f, 0.18f, 1);
  } else {
    spawnPlayerBullet(player.x + 16.0f, player.y, 1.70f, 0.0f, 1);
  }
  playSfxShoot();
}

void fireSpecial() {
  unsigned long now = millis();
  if (now < player.specialCooldownUntil || now < player.respawnUntil) {
    return;
  }
  if (player.specialAmmo == 0 || specialWeapon.active) {
    return;
  }
  player.specialCooldownUntil = now + 400;
  player.specialAmmo--;
  specialWeapon.active = true;
  specialWeapon.exploding = false;
  specialWeapon.damageApplied = false;
  specialWeapon.x = player.x + 14.0f;
  specialWeapon.y = player.y;
  specialWeapon.targetX = player.x + 100.0f;
  if (specialWeapon.targetX > SCREEN_W - 28.0f) {
    specialWeapon.targetX = SCREEN_W - 28.0f;
  }
  specialWeapon.frame = 0;
  specialWeapon.frameAt = now;
  playSfxAlt();
}

void spawnEnemyBullet(float x, float y, float vx, float vy, uint8_t damage) {
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (enemyBullets[i].active) {
      continue;
    }
    enemyBullets[i].active = true;
    enemyBullets[i].x = x;
    enemyBullets[i].y = y;
    enemyBullets[i].vx = vx;
    enemyBullets[i].vy = vy;
    enemyBullets[i].damage = damage;
    return;
  }
}

void spawnEnemy(EnemyKind kind, float x, float y) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      continue;
    }
    enemies[i].active = true;
    enemies[i].kind = kind;
    enemies[i].x = x;
    enemies[i].y = y;
    enemies[i].baseY = y;
    enemies[i].phase = random(0, 628) / 100.0f;
    enemies[i].fireAt = millis() + random(700, 1300);
    if (kind == ENEMY_SCOUT) {
      enemies[i].vx = -0.52f;
      enemies[i].vy = 0.0f;
      enemies[i].hp = 2;
    } else if (kind == ENEMY_WAVE) {
      enemies[i].vx = -0.45f;
      enemies[i].vy = 0.0f;
      enemies[i].hp = 2;
    } else {
      enemies[i].vx = -0.33f;
      enemies[i].vy = 0.0f;
      enemies[i].hp = 3;
    }
    return;
  }
}

void maybeSpawnPickup(float x, float y) {
  int roll = random(0, 100);
  if (roll > 24) {
    return;
  }
  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (pickups[i].active) {
      continue;
    }
    pickups[i].active = true;
    if (roll < 10) {
      pickups[i].kind = PICKUP_POWER;
    } else if (roll < 18) {
      pickups[i].kind = PICKUP_SHIELD;
    } else {
      pickups[i].kind = PICKUP_SPECIAL;
    }
    pickups[i].x = x;
    pickups[i].y = y;
    pickups[i].vx = -0.35f;
    return;
  }
}

void destroyEnemy(Enemy &enemy, bool allowPickup = true) {
  spawnExplosion(enemy.x, enemy.y, 14);
  playSfxExplode();
  if (allowPickup) {
    maybeSpawnPickup(enemy.x, enemy.y);
  }
  enemy.active = false;
}

void applySpecialBlastDamage() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      continue;
    }
    destroyEnemy(enemies[i], false);
  }

  if (!boss.active) {
    return;
  }

  if (boss.coreHp > 0) {
    boss.coreHp--;
    if (boss.coreHp == 0) {
      spawnExplosion(boss.x - 12.0f, boss.y, 26);
      playSfxExplode();
      boss.active = false;
      finishClear();
      return;
    }
  }

  const float podY[2] = {boss.y - 34.0f, boss.y + 34.0f};
  for (int pod = 0; pod < 2; pod++) {
    if (boss.podHp[pod] == 0) {
      continue;
    }
    boss.podHp[pod]--;
    if (boss.podHp[pod] == 0) {
      spawnExplosion(boss.x - 58.0f, podY[pod], 14);
      playSfxExplode();
    }
  }
}

void updateSpecialWeapon() {
  if (!specialWeapon.active) {
    return;
  }

  unsigned long now = millis();
  if (!specialWeapon.exploding) {
    specialWeapon.x += 2.2f;
    if (specialWeapon.x >= specialWeapon.targetX) {
      specialWeapon.x = specialWeapon.targetX;
      specialWeapon.exploding = true;
      specialWeapon.frame = 0;
      specialWeapon.frameAt = now;
      startContinuousVibration(360);
      if (!specialWeapon.damageApplied) {
        applySpecialBlastDamage();
        specialWeapon.damageApplied = true;
      }
    }
    return;
  }

  if (now - specialWeapon.frameAt >= 140) {
    specialWeapon.frameAt = now;
    specialWeapon.frame++;
    if (specialWeapon.frame >= 3) {
      specialWeapon.active = false;
    }
  }
}

void startBoss() {
  resetArrays();
  boss.active = true;
  boss.x = SCREEN_W - 82.0f;
  boss.y = SCREEN_H / 2.0f;
  boss.vy = 0.28f;
  boss.coreHp = 24;
  boss.podHp[0] = 10;
  boss.podHp[1] = 10;
  boss.nextVolleyAt = millis() + 700;
  boss.nextMoveAt = millis() + 420;
  gameState = BOSS;
  setStatus("WARNING");
}

void finishClear() {
  gameState = CLEAR;
  setStatus("STAGE CLEAR");
  startVibration(240);
}

void updateStars() {
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].x -= (stars[i].speed >= 3) ? 1 : 0;
    if (stars[i].x < 0) {
      stars[i].x = SCREEN_W - 1;
      stars[i].y = random(HUD_H + 4, SCREEN_H);
      stars[i].speed = random(1, 4);
    }
  }
  scrollX += 0.5f;
  if (scrollX >= SCREEN_W) {
    scrollX -= SCREEN_W;
  }
}

void updatePlayer() {
  unsigned long now = millis();
  if (player.shield && now >= player.shieldUntil) {
    player.shield = false;
  }

  const int center = 2048;
  const int deadZone = 220;
  const float maxSpeed = 0.90f;
  const float accel = 0.08f;
  const float damping = 0.82f;

  auto axisTarget = [&](int raw) -> float {
    float delta = (float)(raw - center);
    if (fabsf(delta) <= deadZone) {
      return 0.0f;
    }
    float sign = (delta > 0.0f) ? 1.0f : -1.0f;
    float magnitude = (fabsf(delta) - deadZone) / (2047.0f - deadZone);
    if (magnitude > 1.0f) {
      magnitude = 1.0f;
    }
    magnitude = magnitude * magnitude;
    return sign * magnitude * maxSpeed;
  };

  float targetVx = axisTarget(latestX);
  float targetVy = axisTarget(latestY);

  player.vx += (targetVx - player.vx) * accel;
  player.vy += (targetVy - player.vy) * accel;

  if (fabsf(targetVx) < 0.001f) {
    player.vx *= damping;
  }
  if (fabsf(targetVy) < 0.001f) {
    player.vy *= damping;
  }
  if (fabsf(player.vx) < 0.01f) {
    player.vx = 0.0f;
  }
  if (fabsf(player.vy) < 0.01f) {
    player.vy = 0.0f;
  }

  player.x = clampf(player.x + player.vx, 24.0f, SCREEN_W - 24.0f);
  player.y = clampf(player.y + player.vy, HUD_H + 18.0f, SCREEN_H - 18.0f);

  if (player.x <= 24.0f || player.x >= SCREEN_W - 24.0f) {
    player.vx = 0.0f;
  }
  if (player.y <= HUD_H + 18.0f || player.y >= SCREEN_H - 18.0f) {
    player.vy = 0.0f;
  }

  if (blueDown) {
    firePrimary();
  }
  if (greenPressed) {
    fireSpecial();
  }
}

void updatePlayerBullets() {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!playerBullets[i].active) {
      continue;
    }
    playerBullets[i].x += playerBullets[i].vx;
    playerBullets[i].y += playerBullets[i].vy;
    if (playerBullets[i].x > SCREEN_W + 8 || playerBullets[i].y < HUD_H || playerBullets[i].y > SCREEN_H) {
      playerBullets[i].active = false;
    }
  }
}

void updateEnemies() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      continue;
    }
    if (enemies[i].kind == ENEMY_SCOUT) {
      enemies[i].x += enemies[i].vx;
    } else if (enemies[i].kind == ENEMY_WAVE) {
      enemies[i].phase += 0.045f;
      enemies[i].x += enemies[i].vx;
      enemies[i].y = enemies[i].baseY + sinf(enemies[i].phase) * 28.0f;
    } else {
      enemies[i].x += enemies[i].vx;
      if (now >= enemies[i].fireAt) {
        enemies[i].fireAt = now + random(700, 1100);
        spawnEnemyBullet(enemies[i].x - 10.0f, enemies[i].y, -0.95f, 0.0f, 1);
      }
    }

    if (enemies[i].kind != ENEMY_TURRET && now >= enemies[i].fireAt) {
      enemies[i].fireAt = now + random(600, 1100);
      float dy = player.y - enemies[i].y;
      float norm = sqrtf(dy * dy + 2500.0f);
      spawnEnemyBullet(enemies[i].x - 8.0f, enemies[i].y, -1.03f, (dy / norm) * 0.65f, 1);
    }

    if (enemies[i].x < -24.0f) {
      enemies[i].active = false;
    }
  }
}

void updateEnemyBullets() {
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!enemyBullets[i].active) {
      continue;
    }
    enemyBullets[i].x += enemyBullets[i].vx;
    enemyBullets[i].y += enemyBullets[i].vy;
    if (enemyBullets[i].x < -12.0f || enemyBullets[i].y < HUD_H - 6 || enemyBullets[i].y > SCREEN_H + 6) {
      enemyBullets[i].active = false;
    }
  }
}

void updatePickups() {
  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].active) {
      continue;
    }
    pickups[i].x += pickups[i].vx;
    pickups[i].y += sinf((millis() / 280.0f) + i) * 0.18f;
    if (pickups[i].x < -18.0f) {
      pickups[i].active = false;
    }
  }
}

void updateExplosions() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_EXPLOSIONS; i++) {
    if (explosions[i].active && now >= explosions[i].endAt) {
      explosions[i].active = false;
    }
  }
}

void updateBoss() {
  if (!boss.active) {
    return;
  }
  unsigned long now = millis();
  if (now >= boss.nextMoveAt) {
    boss.nextMoveAt = now + 40;
    boss.y += boss.vy;
    if (boss.y < HUD_H + 54.0f || boss.y > SCREEN_H - 54.0f) {
      boss.vy *= -1.0f;
      boss.y = clampf(boss.y, HUD_H + 54.0f, SCREEN_H - 54.0f);
    }
  }

  if (now >= boss.nextVolleyAt) {
    boss.nextVolleyAt = now + 820;
    spawnEnemyBullet(boss.x - 48.0f, boss.y - 18.0f, -0.83f, -0.20f, 1);
    spawnEnemyBullet(boss.x - 56.0f, boss.y + 18.0f, -0.83f, 0.20f, 1);
    if (boss.podHp[0] > 0) {
      spawnEnemyBullet(boss.x - 78.0f, boss.y - 34.0f, -0.60f, 0.12f, 1);
    }
    if (boss.podHp[1] > 0) {
      spawnEnemyBullet(boss.x - 78.0f, boss.y + 34.0f, -0.60f, -0.12f, 1);
    }
  }
}

void handlePlayerHits() {
  if (millis() < player.respawnUntil) {
    return;
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!enemyBullets[i].active) {
      continue;
    }
    if (overlapRect(player.x - 12.0f, player.y - 8.0f, player.x + 12.0f, player.y + 8.0f,
                    enemyBullets[i].x - 3.0f, enemyBullets[i].y - 3.0f, enemyBullets[i].x + 3.0f, enemyBullets[i].y + 3.0f)) {
      enemyBullets[i].active = false;
      spawnExplosion(player.x - 4.0f, player.y, 8);
      loseLife();
      return;
    }
  }

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      continue;
    }
    if (overlapRect(player.x - 12.0f, player.y - 8.0f, player.x + 12.0f, player.y + 8.0f,
                    enemies[i].x - 12.0f, enemies[i].y - 10.0f, enemies[i].x + 12.0f, enemies[i].y + 10.0f)) {
      destroyEnemy(enemies[i]);
      loseLife();
      return;
    }
  }

  if (boss.active) {
    if (overlapRect(player.x - 12.0f, player.y - 8.0f, player.x + 12.0f, player.y + 8.0f,
                    boss.x - 62.0f, boss.y - 42.0f, boss.x + 26.0f, boss.y + 42.0f)) {
      spawnExplosion(player.x, player.y, 10);
      loseLife();
    }
  }
}

void handlePickups() {
  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].active) {
      continue;
    }
    if (overlapRect(player.x - 12.0f, player.y - 8.0f, player.x + 12.0f, player.y + 8.0f,
                    pickups[i].x - 8.0f, pickups[i].y - 8.0f, pickups[i].x + 8.0f, pickups[i].y + 8.0f)) {
      if (pickups[i].kind == PICKUP_POWER) {
        player.spreadShot = true;
        setStatus("DOUBLE FIRE");
      } else if (pickups[i].kind == PICKUP_SHIELD) {
        player.shield = true;
        player.shieldUntil = millis() + 12000;
        setStatus("SHIELD ON");
      } else {
        if (player.specialAmmo < 9) {
          player.specialAmmo++;
        }
        setStatus("SPECIAL +1");
      }
      pickups[i].active = false;
      playSfxPickup();
    }
  }
}

void handlePlayerBulletsVsEnemies() {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!playerBullets[i].active) {
      continue;
    }

    bool hitSomething = false;
    for (int j = 0; j < MAX_ENEMIES; j++) {
      if (!enemies[j].active) {
        continue;
      }
      if (!overlapRect(playerBullets[i].x - 3.0f, playerBullets[i].y - 2.0f, playerBullets[i].x + 3.0f, playerBullets[i].y + 2.0f,
                       enemies[j].x - 12.0f, enemies[j].y - 10.0f, enemies[j].x + 12.0f, enemies[j].y + 10.0f)) {
        continue;
      }
      if (enemies[j].hp <= playerBullets[i].damage) {
        destroyEnemy(enemies[j]);
      } else {
        enemies[j].hp -= playerBullets[i].damage;
        playSfxHit();
      }
      playerBullets[i].active = false;
      hitSomething = true;
      break;
    }
    if (hitSomething) {
      continue;
    }

    if (!boss.active) {
      continue;
    }
    if (overlapRect(playerBullets[i].x - 3.0f, playerBullets[i].y - 2.0f, playerBullets[i].x + 3.0f, playerBullets[i].y + 2.0f,
                    boss.x - 30.0f, boss.y - 16.0f, boss.x + 16.0f, boss.y + 16.0f)) {
      if (boss.coreHp > playerBullets[i].damage) {
        boss.coreHp -= playerBullets[i].damage;
        playSfxHit();
      } else {
        boss.coreHp = 0;
        spawnExplosion(boss.x - 12.0f, boss.y, 26);
        playSfxExplode();
        boss.active = false;
        finishClear();
      }
      playerBullets[i].active = false;
      continue;
    }

    const float podY[2] = {boss.y - 34.0f, boss.y + 34.0f};
    for (int pod = 0; pod < 2; pod++) {
      if (boss.podHp[pod] == 0) {
        continue;
      }
      if (overlapRect(playerBullets[i].x - 3.0f, playerBullets[i].y - 2.0f, playerBullets[i].x + 3.0f, playerBullets[i].y + 2.0f,
                      boss.x - 72.0f, podY[pod] - 14.0f, boss.x - 48.0f, podY[pod] + 14.0f)) {
        if (boss.podHp[pod] > playerBullets[i].damage) {
          boss.podHp[pod] -= playerBullets[i].damage;
          playSfxHit();
        } else {
          boss.podHp[pod] = 0;
          spawnExplosion(boss.x - 58.0f, podY[pod], 14);
          playSfxExplode();
        }
        playerBullets[i].active = false;
        break;
      }
    }
  }
}

void spawnStageWave() {
  unsigned long now = millis();
  unsigned long elapsed = now - stageStartMs;
  if (elapsed >= 45000) {
    startBoss();
    return;
  }
  if (now - lastEnemySpawnMs < 1200) {
    return;
  }
  lastEnemySpawnMs = now;

  int lane = random(0, 5);
  float y = HUD_H + 40.0f + lane * 48.0f;
  if (elapsed < 12000) {
    spawnEnemy(ENEMY_SCOUT, SCREEN_W + 16.0f, y);
    if (random(0, 100) < 12) {
      spawnEnemy(ENEMY_SCOUT, SCREEN_W + 52.0f, y + 18.0f);
    }
  } else if (elapsed < 28000) {
    spawnEnemy((random(0, 100) < 45) ? ENEMY_WAVE : ENEMY_SCOUT, SCREEN_W + 16.0f, y);
  } else {
    int roll = random(0, 100);
    if (roll < 28) {
      spawnEnemy(ENEMY_TURRET, SCREEN_W + 16.0f, y);
    } else if (roll < 60) {
      spawnEnemy(ENEMY_WAVE, SCREEN_W + 16.0f, y);
    } else {
      spawnEnemy(ENEMY_SCOUT, SCREEN_W + 16.0f, y);
      if (random(0, 100) < 20) {
        spawnEnemy(ENEMY_SCOUT, SCREEN_W + 58.0f, y - 24.0f);
      }
    }
  }
}

template <typename CanvasT>
void drawStars(CanvasT &canvas) {
  canvas.fillScreen(rgb565(6, 8, 20));
  for (int i = 0; i < STAR_COUNT; i++) {
    canvas.drawPixel(stars[i].x, stars[i].y, stars[i].color);
    if (stars[i].speed >= 3 && stars[i].x > 1) {
      canvas.drawPixel(stars[i].x - 1, stars[i].y, rgb565(90, 100, 140));
    }
  }
  int scrollStripe = (int)scrollX;
  for (int x = -(scrollStripe % 80); x < SCREEN_W; x += 80) {
    canvas.drawFastVLine(x, HUD_H, SCREEN_H - HUD_H, rgb565(10, 18, 32));
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.fillRect(0, 0, SCREEN_W, HUD_H, rgb565(14, 22, 40));
  canvas.drawFastHLine(0, HUD_H - 1, SCREEN_W, rgb565(70, 120, 180));
  canvas.setTextColor(TFT_WHITE, rgb565(14, 22, 40));
  canvas.setTextDatum(TL_DATUM);
  canvas.drawString("LIFE", 8, 4, 2);
  canvas.drawString(String(player.lives), 52, 4, 2);
  canvas.drawString("HP", 100, 4, 2);
  canvas.drawString(String(player.hp), 126, 4, 2);
  canvas.drawString("ARM", 164, 4, 2);
  canvas.drawString(player.spreadShot ? "DOUBLE" : "NORMAL", 196, 4, 2);
  canvas.drawString("SP", 304, 4, 2);
  canvas.drawString(String(player.specialAmmo), 330, 4, 2);
  if (player.shield) {
    canvas.setTextColor(rgb565(150, 255, 220), rgb565(14, 22, 40));
    canvas.drawString("SHIELD", 360, 4, 2);
  }
  if ((millis() - statusBlinkMs) < 2200) {
    canvas.setTextColor(rgb565(255, 220, 80), rgb565(14, 22, 40));
    canvas.drawRightString(statusText, SCREEN_W - 8, 4, 2);
  }
}

template <typename CanvasT>
void drawPlayer(CanvasT &canvas) {
  if (gameState == GAME_OVER) {
    return;
  }
  if (millis() < player.respawnUntil && ((millis() / 100) % 2 == 0)) {
    return;
  }
  drawPixelSprite(canvas, (int)player.x, (int)player.y, PLAYER_SPRITE, 11, 2,
                  rgb565(230, 246, 255), rgb565(134, 230, 255), rgb565(84, 170, 255),
                  rgb565(255, 202, 92), rgb565(255, 130, 90), rgb565(38, 54, 110));
  if (player.shield) {
    canvas.drawCircle((int)player.x, (int)player.y, 16, rgb565(100, 255, 220));
    canvas.drawCircle((int)player.x, (int)player.y, 17, rgb565(50, 180, 190));
  }
}

template <typename CanvasT>
void drawEnemy(CanvasT &canvas, const Enemy &enemy) {
  if (!enemy.active) {
    return;
  }
  if (enemy.kind == ENEMY_SCOUT) {
    drawPixelSprite(canvas, (int)enemy.x, (int)enemy.y, SCOUT_SPRITE, 9, 2,
                    rgb565(255, 242, 220), rgb565(255, 174, 120), rgb565(255, 112, 90),
                    rgb565(200, 74, 88), rgb565(255, 225, 116), rgb565(80, 46, 72));
  } else if (enemy.kind == ENEMY_WAVE) {
    drawPixelSprite(canvas, (int)enemy.x, (int)enemy.y, WAVE_SPRITE, 8, 2,
                    rgb565(255, 241, 202), rgb565(185, 255, 235), rgb565(84, 224, 218),
                    rgb565(68, 152, 210), rgb565(255, 170, 90), rgb565(34, 68, 112));
  } else {
    drawPixelSprite(canvas, (int)enemy.x, (int)enemy.y, TURRET_SPRITE, 8, 2,
                    rgb565(228, 234, 252), rgb565(170, 182, 218), rgb565(120, 136, 182),
                    rgb565(86, 102, 152), rgb565(255, 146, 102), rgb565(255, 214, 116),
                    rgb565(58, 72, 118));
  }
}

template <typename CanvasT>
void drawBoss(CanvasT &canvas) {
  if (!boss.active) {
    return;
  }
  drawPixelSprite(canvas, (int)boss.x - 18, (int)boss.y, BOSS_BODY_SPRITE, 16, 3,
                  rgb565(244, 248, 255), rgb565(194, 210, 242), rgb565(144, 164, 218),
                  rgb565(110, 132, 196), rgb565(255, 152, 106), rgb565(255, 108, 94),
                  rgb565(255, 222, 120), rgb565(72, 92, 154), rgb565(246, 248, 255));
  if (boss.podHp[0] > 0) {
    drawPixelSprite(canvas, (int)boss.x - 62, (int)boss.y - 34, BOSS_POD_SPRITE, 7, 3,
                    rgb565(255, 247, 224), rgb565(255, 204, 120), rgb565(255, 160, 84),
                    rgb565(224, 106, 98), rgb565(84, 92, 150), rgb565(40, 48, 96));
  }
  if (boss.podHp[1] > 0) {
    drawPixelSprite(canvas, (int)boss.x - 62, (int)boss.y + 34, BOSS_POD_SPRITE, 7, 3,
                    rgb565(255, 247, 224), rgb565(255, 204, 120), rgb565(255, 160, 84),
                    rgb565(224, 106, 98), rgb565(84, 92, 150), rgb565(40, 48, 96));
  }
  canvas.fillRect((int)boss.x - 34, (int)boss.y - 6, 18, 12, rgb565(255, 230, 214));
  canvas.fillRect((int)boss.x - 10, (int)boss.y - 6, 18, 12, rgb565(255, 230, 214));
  canvas.fillRect((int)boss.x - 28, (int)boss.y - 1, 12, 3, rgb565(74, 88, 160));
  canvas.fillRect((int)boss.x - 4, (int)boss.y - 1, 12, 3, rgb565(74, 88, 160));

  int hpWidth = (boss.coreHp * 80) / 24;
  canvas.drawRect(SCREEN_W - 102, HUD_H + 8, 86, 8, rgb565(180, 220, 255));
  canvas.fillRect(SCREEN_W - 100, HUD_H + 10, hpWidth, 4, rgb565(255, 120, 90));
}

template <typename CanvasT>
void drawBullets(CanvasT &canvas) {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (playerBullets[i].active) {
      canvas.fillRect((int)playerBullets[i].x - 2, (int)playerBullets[i].y - 1, 6, 3, rgb565(160, 255, 255));
    }
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (enemyBullets[i].active) {
      canvas.fillCircle((int)enemyBullets[i].x, (int)enemyBullets[i].y, 3, rgb565(255, 120, 90));
    }
  }
}

template <typename CanvasT>
void drawPickups(CanvasT &canvas) {
  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].active) {
      continue;
    }
    if (pickups[i].kind == PICKUP_POWER) {
      drawPixelSprite(canvas, (int)pickups[i].x, (int)pickups[i].y, POWER_PICKUP_SPRITE, 7, 2,
                      rgb565(255, 248, 220), rgb565(255, 214, 110), rgb565(255, 160, 82),
                      rgb565(232, 108, 84), rgb565(116, 82, 162), rgb565(72, 54, 118));
      canvas.fillRect((int)pickups[i].x - 2, (int)pickups[i].y - 4, 3, 8, rgb565(72, 54, 118));
      canvas.fillRect((int)pickups[i].x - 2, (int)pickups[i].y - 4, 6, 3, rgb565(72, 54, 118));
    } else {
      if (pickups[i].kind == PICKUP_SHIELD) {
        drawPixelSprite(canvas, (int)pickups[i].x, (int)pickups[i].y, SHIELD_PICKUP_SPRITE, 7, 2,
                        rgb565(238, 255, 250), rgb565(130, 255, 226), rgb565(78, 222, 206),
                        rgb565(56, 164, 196), rgb565(255, 255, 255), rgb565(52, 92, 156));
        canvas.drawCircle((int)pickups[i].x, (int)pickups[i].y, 5, rgb565(52, 92, 156));
      } else {
        drawPixelSprite(canvas, (int)pickups[i].x, (int)pickups[i].y, SPECIAL_PICKUP_SPRITE, 7, 2,
                        rgb565(255, 247, 222), rgb565(255, 190, 104), rgb565(255, 130, 80),
                        rgb565(190, 96, 226), rgb565(120, 78, 186), rgb565(62, 44, 122));
        canvas.fillRect((int)pickups[i].x - 1, (int)pickups[i].y - 5, 2, 10, rgb565(62, 44, 122));
        canvas.fillRect((int)pickups[i].x - 5, (int)pickups[i].y - 1, 10, 2, rgb565(62, 44, 122));
      }
    }
  }
}

template <typename CanvasT>
void drawSpecialWeapon(CanvasT &canvas) {
  if (!specialWeapon.active) {
    return;
  }

  if (!specialWeapon.exploding) {
    canvas.fillRect((int)specialWeapon.x - 8, (int)specialWeapon.y - 2, 10, 4, rgb565(255, 214, 106));
    canvas.fillRect((int)specialWeapon.x + 2, (int)specialWeapon.y - 1, 6, 2, rgb565(255, 255, 240));
    canvas.fillRect((int)specialWeapon.x - 10, (int)specialWeapon.y - 1, 2, 2, rgb565(255, 130, 84));
    return;
  }

  static const int radii[3] = {16, 30, 48};
  static const uint16_t outer[3] = {0xFBE9, 0xFB06, 0xE24F};
  int idx = (specialWeapon.frame > 2) ? 2 : specialWeapon.frame;
  canvas.fillCircle((int)specialWeapon.x, (int)specialWeapon.y, radii[idx], outer[idx]);
  canvas.fillCircle((int)specialWeapon.x, (int)specialWeapon.y, radii[idx] - 9, rgb565(255, 226, 120));
  canvas.fillCircle((int)specialWeapon.x, (int)specialWeapon.y, radii[idx] - 18, rgb565(255, 248, 220));
}

template <typename CanvasT>
void drawExplosions(CanvasT &canvas) {
  for (int i = 0; i < MAX_EXPLOSIONS; i++) {
    if (!explosions[i].active) {
      continue;
    }
    unsigned long remain = explosions[i].endAt - millis();
    int radius = explosions[i].radius - (int)(remain / 28);
    if (radius < 3) {
      radius = 3;
    }
    canvas.fillCircle((int)explosions[i].x, (int)explosions[i].y, radius, rgb565(255, 200, 80));
    canvas.drawCircle((int)explosions[i].x, (int)explosions[i].y, radius + 3, rgb565(255, 120, 70));
  }
}

template <typename CanvasT>
void drawScene(CanvasT &canvas) {
  drawStars(canvas);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    drawEnemy(canvas, enemies[i]);
  }
  drawBoss(canvas);
  drawBullets(canvas);
  drawSpecialWeapon(canvas);
  drawPickups(canvas);
  drawExplosions(canvas);
  drawPlayer(canvas);
  drawHud(canvas);
}

void renderPlaying() {
  if (!frameBufferReady) {
    drawScene(tft);
    return;
  }
  WindowCanvas left = {&frameBufferLeft, 0};
  WindowCanvas right = {&frameBufferRight, FRAME_W};
  drawScene(left);
  drawScene(right);
  frameBufferLeft.pushSprite(0, 0);
  frameBufferRight.pushSprite(FRAME_W, 0);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  canvas.fillScreen(rgb565(6, 8, 20));
  for (int i = 0; i < STAR_COUNT; i++) {
    int px = (stars[i].x + (millis() / stars[i].speed)) % SCREEN_W;
    canvas.drawPixel(px, stars[i].y, stars[i].color);
  }
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(rgb565(140, 230, 255), rgb565(6, 8, 20));
  canvas.drawString("HORIZON STRIKE", SCREEN_W / 2, 92, 4);
  canvas.setTextColor(rgb565(255, 190, 90), rgb565(6, 8, 20));
  canvas.drawString("FC STYLE SHOOTER", SCREEN_W / 2, 128, 2);
  int shipY = 170 + (int)(sinf((millis() - titleAnimMs) / 220.0f) * 10.0f);
  drawPixelSprite(canvas, 188, shipY, PLAYER_SPRITE, 11, 3,
                  rgb565(230, 246, 255), rgb565(134, 230, 255), rgb565(84, 170, 255),
                  rgb565(255, 202, 92), rgb565(255, 130, 90), rgb565(38, 54, 110));
  drawPixelSprite(canvas, 282, shipY - 2, SCOUT_SPRITE, 9, 3,
                  rgb565(255, 242, 220), rgb565(255, 174, 120), rgb565(255, 112, 90),
                  rgb565(200, 74, 88), rgb565(255, 225, 116), rgb565(80, 46, 72));
  if (((millis() / 350) % 2) == 0) {
    canvas.setTextColor(TFT_WHITE, rgb565(6, 8, 20));
    canvas.drawString("BLUE START", SCREEN_W / 2, 232, 4);
  }
  canvas.setTextColor(rgb565(150, 170, 210), rgb565(6, 8, 20));
  canvas.drawString("BLUE: FIRE   GREEN: SPECIAL", SCREEN_W / 2, 274, 2);
}

template <typename CanvasT>
void drawEndScreen(CanvasT &canvas, const char *headline, uint16_t color) {
  canvas.fillScreen(rgb565(6, 8, 20));
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(color, rgb565(6, 8, 20));
  canvas.drawString(headline, SCREEN_W / 2, 112, 4);
  canvas.setTextColor(TFT_WHITE, rgb565(6, 8, 20));
  canvas.drawString(statusText, SCREEN_W / 2, 160, 2);
  canvas.drawString("BLUE RESTART", SCREEN_W / 2, 212, 4);
}

void updateGame() {
  updateStars();
  updatePlayer();
  updatePlayerBullets();
  updateSpecialWeapon();
  if (gameState == PLAYING) {
    spawnStageWave();
    updateEnemies();
  } else if (gameState == BOSS) {
    updateBoss();
  }
  updateEnemyBullets();
  updatePickups();
  updateExplosions();
  handlePlayerBulletsVsEnemies();
  handlePickups();
  handlePlayerHits();

  digitalWrite(green_led_pin, player.shield ? LOW : HIGH);
  digitalWrite(blue_led_pin, (gameState == BOSS) ? LOW : HIGH);
}

void setup() {
  randomSeed(esp_random());
  setupHardware();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);
  initFrameBuffer();
  resetStars();
  setStatus("READY");

  xTaskCreatePinnedToCore(
    audioTaskMain,
    "audio_task",
    4096,
    nullptr,
    2,
    &audioTaskHandle,
    0
  );
}

void loop() {
  readButtons();
  updateVibration();

  if (gameState == TITLE) {
    if (!frameBufferReady) {
      drawTitle(tft);
    } else {
      WindowCanvas left = {&frameBufferLeft, 0};
      WindowCanvas right = {&frameBufferRight, FRAME_W};
      drawTitle(left);
      drawTitle(right);
      frameBufferLeft.pushSprite(0, 0);
      frameBufferRight.pushSprite(FRAME_W, 0);
    }
    lastRenderedState = TITLE;
    if (bluePressed) {
      beginGame();
    }
    delay(1);
    return;
  }

  if (gameState == GAME_OVER) {
    if (lastRenderedState != GAME_OVER) {
      if (!frameBufferReady) {
        drawEndScreen(tft, "GAME OVER", rgb565(255, 110, 90));
      } else {
        WindowCanvas left = {&frameBufferLeft, 0};
        WindowCanvas right = {&frameBufferRight, FRAME_W};
        drawEndScreen(left, "GAME OVER", rgb565(255, 110, 90));
        drawEndScreen(right, "GAME OVER", rgb565(255, 110, 90));
        frameBufferLeft.pushSprite(0, 0);
        frameBufferRight.pushSprite(FRAME_W, 0);
      }
      lastRenderedState = GAME_OVER;
    }
    if (bluePressed) {
      beginGame();
    }
    delay(1);
    return;
  }

  if (gameState == CLEAR) {
    if (lastRenderedState != CLEAR) {
      if (!frameBufferReady) {
        drawEndScreen(tft, "MISSION CLEAR", rgb565(120, 255, 180));
      } else {
        WindowCanvas left = {&frameBufferLeft, 0};
        WindowCanvas right = {&frameBufferRight, FRAME_W};
        drawEndScreen(left, "MISSION CLEAR", rgb565(120, 255, 180));
        drawEndScreen(right, "MISSION CLEAR", rgb565(120, 255, 180));
        frameBufferLeft.pushSprite(0, 0);
        frameBufferRight.pushSprite(FRAME_W, 0);
      }
      lastRenderedState = CLEAR;
    }
    if (bluePressed) {
      beginGame();
    }
    delay(1);
    return;
  }

  lastRenderedState = gameState;
  unsigned long now = millis();
  while (now - lastStepMs >= GAME_STEP_MS) {
    lastStepMs += GAME_STEP_MS;
    updateGame();
  }
  renderPlaying();
  delay(1);
}
