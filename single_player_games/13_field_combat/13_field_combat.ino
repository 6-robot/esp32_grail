#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

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
const int audio_lrc_pin = 13;
const int audio_bclk_pin = 14;
const int audio_din_pin = 4;

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBufferLeft = TFT_eSprite(&tft);
TFT_eSprite frameBufferRight = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int FRAME_W = SCREEN_W / 2;
static const int HUD_H = 30;
static const int FIELD_TOP = HUD_H + 4;
static const int GAME_STEP_MS = 16;
static constexpr float PLAYER_MAX_SPEED_X = 2.6f;
static constexpr float PLAYER_MAX_SPEED_Y = 2.3f;
static constexpr float PLAYER_ACCEL_X = 0.34f;
static constexpr float PLAYER_ACCEL_Y = 0.30f;
static constexpr float PLAYER_DAMPING = 0.78f;
static constexpr float CAPTURE_ZONE_OFFSET_Y = 66.0f;
static constexpr float CAPTURE_ZONE_HIT_RADIUS = 26.0f;
static constexpr float CAPTURE_CONTACT_RADIUS = 18.0f;
static const int LEADERBOARD_SIZE = 5;
static const int MAX_PLAYER_BULLETS = 16;
static const int MAX_ENEMY_BULLETS = 18;
static const int MAX_ENEMIES = 18;
static const int MAX_ALLIES = 6;
static const int MAX_SPARKS = 16;
static const int TARGET_PROGRESS = 100;
static const int SAMPLE_RATE = 16000;
static const int AUDIO_CHUNK_SAMPLES = 128;

enum GameState {
  TITLE,
  PLAYING,
  LEADERBOARD,
  GAME_OVER
};

enum UnitType {
  UNIT_INFANTRY,
  UNIT_TANK,
  UNIT_DRONE
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

struct Bullet {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  bool capture;
};

struct Enemy {
  bool active;
  UnitType type;
  float x;
  float y;
  float vx;
  float vy;
  int hp;
  int maxHp;
  unsigned long fireAt;
  bool capturing;
  float captureAngle;
  float captureSpin;
  float capturePullSpeed;
};

struct Ally {
  bool active;
  UnitType type;
  float x;
  float y;
  uint8_t slot;
  int hp;
  int maxHp;
  unsigned long fireAt;
};

struct Spark {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  uint16_t color;
  unsigned long endAt;
};

struct Player {
  float x;
  float y;
  float vx;
  float vy;
  int hp;
  unsigned long fireAt;
  unsigned long captureAt;
};

struct WindowCanvas {
  TFT_eSprite *sprite;
  int xOffset;

  void fillScreen(uint16_t color) { sprite->fillScreen(color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) { sprite->fillRect(x - xOffset, y, w, h, color); }
  void drawRect(int x, int y, int w, int h, uint16_t color) { sprite->drawRect(x - xOffset, y, w, h, color); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) { sprite->drawLine(x0 - xOffset, y0, x1 - xOffset, y1, color); }
  void drawFastHLine(int x, int y, int w, uint16_t color) { sprite->drawFastHLine(x - xOffset, y, w, color); }
  void drawFastVLine(int x, int y, int h, uint16_t color) { sprite->drawFastVLine(x - xOffset, y, h, color); }
  void drawPixel(int x, int y, uint16_t color) { sprite->drawPixel(x - xOffset, y, color); }
  void fillCircle(int x, int y, int r, uint16_t color) { sprite->fillCircle(x - xOffset, y, r, color); }
  void drawCircle(int x, int y, int r, uint16_t color) { sprite->drawCircle(x - xOffset, y, r, color); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    sprite->fillTriangle(x0 - xOffset, y0, x1 - xOffset, y1, x2 - xOffset, y2, color);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    sprite->fillRoundRect(x - xOffset, y, w, h, r, color);
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    sprite->drawRoundRect(x - xOffset, y, w, h, r, color);
  }
  void setTextColor(uint16_t fg, uint16_t bg) { sprite->setTextColor(fg, bg); }
  int drawString(const char *text, int x, int y, int font) { return sprite->drawString(text, x - xOffset, y, font); }
  int drawString(const String &text, int x, int y, int font) { return sprite->drawString(text, x - xOffset, y, font); }
  int drawCentreString(const char *text, int x, int y, int font) { return sprite->drawCentreString(text, x - xOffset, y, font); }
};

struct ScreenCanvas {
  TFT_eSPI *screen;

  void fillScreen(uint16_t color) { screen->fillScreen(color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) { screen->fillRect(x, y, w, h, color); }
  void drawRect(int x, int y, int w, int h, uint16_t color) { screen->drawRect(x, y, w, h, color); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) { screen->drawLine(x0, y0, x1, y1, color); }
  void drawFastHLine(int x, int y, int w, uint16_t color) { screen->drawFastHLine(x, y, w, color); }
  void drawFastVLine(int x, int y, int h, uint16_t color) { screen->drawFastVLine(x, y, h, color); }
  void drawPixel(int x, int y, uint16_t color) { screen->drawPixel(x, y, color); }
  void fillCircle(int x, int y, int r, uint16_t color) { screen->fillCircle(x, y, r, color); }
  void drawCircle(int x, int y, int r, uint16_t color) { screen->drawCircle(x, y, r, color); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    screen->fillTriangle(x0, y0, x1, y1, x2, y2, color);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) { screen->fillRoundRect(x, y, w, h, r, color); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) { screen->drawRoundRect(x, y, w, h, r, color); }
  void setTextColor(uint16_t fg, uint16_t bg) { screen->setTextColor(fg, bg); }
  int drawString(const char *text, int x, int y, int font) { return screen->drawString(text, x, y, font); }
  int drawString(const String &text, int x, int y, int font) { return screen->drawString(text, x, y, font); }
  int drawCentreString(const char *text, int x, int y, int font) { return screen->drawCentreString(text, x, y, font); }
};

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;

Player player;
Bullet playerBullets[MAX_PLAYER_BULLETS];
Bullet enemyBullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];
Ally allies[MAX_ALLIES];
Spark sparks[MAX_SPARKS];

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
int latestX = 2048;
int latestY = 2048;
bool frameBufferReady = false;
bool vibrating = false;
bool vibForward = false;

unsigned long lastStepMs = 0;
unsigned long lastSpawnMs = 0;
unsigned long bannerUntilMs = 0;
unsigned long titleBlinkMs = 0;
unsigned long stateTimerMs = 0;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;
int score = 0;
int progressPoints = 0;
int captures = 0;
int kills = 0;
int enemyPressure = 0;
int leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int latestRank = -1;
bool scoreSubmitted = false;
char bannerText[28] = "";
Voice sfxVoices[5];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t audioTaskHandle = nullptr;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
float bgmPhase = 0.0f;
int bgmIndex = 0;
int bgmRemainSamples = 0;
bool audioReady = false;

const Note bgmLoop[] = {
  {294, 140}, {330, 140}, {392, 140}, {440, 160},
  {392, 120}, {330, 120}, {294, 160}, {0, 60},
  {330, 140}, {392, 140}, {440, 140}, {523, 160},
  {440, 120}, {392, 120}, {330, 180}, {0, 60},
  {392, 140}, {440, 140}, {523, 140}, {587, 180},
  {523, 120}, {440, 120}, {392, 180}, {0, 60},
  {330, 140}, {392, 140}, {440, 140}, {392, 140},
  {330, 120}, {294, 120}, {262, 220}, {0, 80}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

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
  .bck_io_num = audio_bclk_pin,
  .ws_io_num = audio_lrc_pin,
  .data_out_num = audio_din_pin,
  .data_in_num = -1
};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t darken565(uint16_t color, uint8_t amount) {
  int r = ((color >> 11) & 0x1F) * 255 / 31;
  int g = ((color >> 5) & 0x3F) * 255 / 63;
  int b = (color & 0x1F) * 255 / 31;
  r = max(0, r - amount);
  g = max(0, g - amount);
  b = max(0, b - amount);
  return rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
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
  if (!audioReady) return;
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  taskENTER_CRITICAL(&audioMux);
  if (preferredChannel >= 0 && preferredChannel < 5) {
    assignVoice(sfxVoices[preferredChannel], freq, durationSamples, volume, wave);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }
  for (int i = 0; i < 5; i++) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }
  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxShoot() { startVoice(760.0f, 40, 0.08f, 0); }
void playSfxEnemyShoot() { startVoice(210.0f, 80, 0.06f, 0, 4); }
void playSfxHit() { startVoice(150.0f, 90, 0.10f, 2); }
void playSfxExplode() {
  startVoice(140.0f, 120, 0.10f, 2);
  startVoice(90.0f, 180, 0.10f, 2, 1);
}
void playSfxCaptureLock() {
  startVoice(660.0f, 60, 0.08f, 0);
  startVoice(988.0f, 80, 0.08f, 1, 1);
}
void playSfxConvert() {
  startVoice(392.0f, 70, 0.08f, 1);
  startVoice(523.0f, 90, 0.08f, 1, 1);
  startVoice(659.0f, 120, 0.08f, 1, 2);
}
void playSfxGuardBreak() {
  startVoice(180.0f, 70, 0.10f, 2);
  startVoice(120.0f, 140, 0.08f, 2, 1);
}
void playSfxGameOver() {
  startVoice(220.0f, 100, 0.08f, 0);
  startVoice(165.0f, 130, 0.08f, 0, 1);
  startVoice(110.0f, 220, 0.08f, 0, 2);
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
  float sample = (bgmPhase < 0.5f) ? 0.0055f : -0.0055f;
  bgmPhase += (float)freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  return sample;
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

void audioTick() {
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; i++) {
    float mix = bgmSample();
    taskENTER_CRITICAL(&audioMux);
    for (int v = 0; v < 5; v++) {
      if (!sfxVoices[v].active) continue;
      mix += waveSample(sfxVoices[v]);
      sfxVoices[v].remainSamples--;
      if (sfxVoices[v].remainSamples <= 0) {
        sfxVoices[v].active = false;
      }
    }
    taskEXIT_CRITICAL(&audioMux);
    mix = clampf(mix, -0.65f, 0.65f);
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

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startVibration(uint16_t durationMs = 160) {
  vibrating = true;
  vibForward = false;
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

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 20) {
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
}

float lengthSq(float ax, float ay, float bx, float by) {
  float dx = ax - bx;
  float dy = ay - by;
  return dx * dx + dy * dy;
}

void captureZoneCenter(float &x, float &y) {
  x = player.x;
  y = player.y - CAPTURE_ZONE_OFFSET_Y;
}

uint16_t unitColor(UnitType type, bool friendly) {
  if (friendly) {
    switch (type) {
      case UNIT_INFANTRY: return rgb565(110, 255, 155);
      case UNIT_TANK: return rgb565(90, 220, 255);
      case UNIT_DRONE: return rgb565(255, 235, 120);
    }
  }

  switch (type) {
    case UNIT_INFANTRY: return rgb565(255, 100, 90);
    case UNIT_TANK: return rgb565(255, 160, 70);
    case UNIT_DRONE: return rgb565(255, 110, 210);
  }
  return TFT_WHITE;
}

int unitBaseHp(UnitType type) {
  switch (type) {
    case UNIT_TANK: return 3;
    case UNIT_DRONE: return 1;
    case UNIT_INFANTRY:
    default: return 2;
  }
}

int allyCount() {
  int count = 0;
  for (int i = 0; i < MAX_ALLIES; i++) {
    if (allies[i].active) count++;
  }
  return count;
}

int findFreeAllySlot() {
  bool used[MAX_ALLIES] = {false};
  for (int i = 0; i < MAX_ALLIES; i++) {
    if (allies[i].active && allies[i].slot < MAX_ALLIES) {
      used[allies[i].slot] = true;
    }
  }
  for (int i = 0; i < MAX_ALLIES; i++) {
    if (!used[i]) return i;
  }
  return -1;
}

void clearArrays() {
  memset(playerBullets, 0, sizeof(playerBullets));
  memset(enemyBullets, 0, sizeof(enemyBullets));
  memset(enemies, 0, sizeof(enemies));
  memset(allies, 0, sizeof(allies));
  memset(sparks, 0, sizeof(sparks));
}

void setBanner(const char *text, uint16_t durationMs = 900) {
  strncpy(bannerText, text, sizeof(bannerText) - 1);
  bannerText[sizeof(bannerText) - 1] = '\0';
  bannerUntilMs = millis() + durationMs;
}

void beginGame() {
  clearArrays();
  player.x = SCREEN_W * 0.5f;
  player.y = SCREEN_H - 34.0f;
  player.vx = 0.0f;
  player.vy = 0.0f;
  player.hp = 5;
  player.fireAt = 0;
  player.captureAt = 0;
  score = 0;
  progressPoints = 0;
  captures = 0;
  kills = 0;
  enemyPressure = 0;
  latestRank = -1;
  scoreSubmitted = false;
  lastSpawnMs = 0;
  lastStepMs = millis();
  stateTimerMs = millis();
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  bannerText[0] = '\0';
  bannerUntilMs = 0;
  setBanner("Blue fire  Green capture", 1500);
  gameState = PLAYING;
}

void initFrameBuffers() {
  frameBufferLeft.setColorDepth(16);
  frameBufferRight.setColorDepth(16);
  bool leftReady = (frameBufferLeft.createSprite(FRAME_W, SCREEN_H) != nullptr);
  bool rightReady = (frameBufferRight.createSprite(FRAME_W, SCREEN_H) != nullptr);
  if (!leftReady || !rightReady) {
    frameBufferLeft.deleteSprite();
    frameBufferRight.deleteSprite();
    frameBufferLeft.setColorDepth(8);
    frameBufferRight.setColorDepth(8);
    leftReady = (frameBufferLeft.createSprite(FRAME_W, SCREEN_H) != nullptr);
    rightReady = (frameBufferRight.createSprite(FRAME_W, SCREEN_H) != nullptr);
  }
  frameBufferReady = leftReady && rightReady;
}

void initAudio() {
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, nullptr);
  if (err != ESP_OK) return;
  err = i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);
  if (err != ESP_OK) return;
  i2s_zero_dma_buffer(I2S_NUM_1);
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  audioReady = true;
  xTaskCreatePinnedToCore(audioTaskMain, "audio_task", 4096, nullptr, 2, &audioTaskHandle, 0);
}

void readInput() {
  bool newGreenDown = digitalRead(green_btn_pin) == LOW;
  bool newBlueDown = digitalRead(blue_btn_pin) == LOW;
  greenPressed = newGreenDown && !greenDown;
  bluePressed = newBlueDown && !blueDown;
  greenDown = newGreenDown;
  blueDown = newBlueDown;
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);

  digitalWrite(green_led_pin, (millis() >= player.captureAt) ? LOW : HIGH);
  digitalWrite(blue_led_pin, (millis() >= player.fireAt) ? LOW : HIGH);
}

void spawnSpark(float x, float y, uint16_t color) {
  for (int i = 0; i < MAX_SPARKS; i++) {
    if (!sparks[i].active) {
      sparks[i].active = true;
      sparks[i].x = x;
      sparks[i].y = y;
      sparks[i].vx = (float)random(-14, 15) * 0.12f;
      sparks[i].vy = (float)random(-14, 15) * 0.12f;
      sparks[i].color = color;
      sparks[i].endAt = millis() + 260 + random(0, 160);
      return;
    }
  }
}

void spawnBurst(float x, float y, uint16_t color, int count) {
  for (int i = 0; i < count; i++) {
    spawnSpark(x, y, color);
  }
}

void addProgress(int delta) {
  progressPoints += delta;
  if (progressPoints >= TARGET_PROGRESS) {
    progressPoints = TARGET_PROGRESS;
  }
}

void submitScoreToLeaderboard() {
  latestRank = LEADERBOARD_SIZE;
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    if (score > leaderboard[i]) {
      latestRank = i;
      break;
    }
  }

  if (latestRank >= LEADERBOARD_SIZE) {
    if (score <= leaderboard[LEADERBOARD_SIZE - 1]) {
      latestRank = -1;
      scoreSubmitted = true;
      return;
    }
    latestRank = LEADERBOARD_SIZE - 1;
  }

  for (int i = LEADERBOARD_SIZE - 1; i > latestRank; i--) {
    leaderboard[i] = leaderboard[i - 1];
  }
  leaderboard[latestRank] = score;
  scoreSubmitted = true;
  playSfxConvert();
}

bool spawnFriendlyBullet(float x, float y, float vx, float vy, bool capture) {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!playerBullets[i].active) {
      playerBullets[i].active = true;
      playerBullets[i].capture = capture;
      playerBullets[i].x = x;
      playerBullets[i].y = y;
      playerBullets[i].vx = vx;
      playerBullets[i].vy = vy;
      return true;
    }
  }
  return false;
}

void spawnPlayerBullet(bool capture) {
  spawnFriendlyBullet(player.x, player.y - 14, 0.0f, capture ? -2.3f : -3.6f, capture);
  playSfxShoot();
}

void spawnEnemyBullet(float x, float y, float vx, float vy) {
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!enemyBullets[i].active) {
      enemyBullets[i].active = true;
      enemyBullets[i].capture = false;
      enemyBullets[i].x = x;
      enemyBullets[i].y = y;
      enemyBullets[i].vx = vx;
      enemyBullets[i].vy = vy;
      return;
    }
  }
}

void spawnEnemy() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      enemies[i].active = true;
      int roll = random(0, 100);
      if (roll < 42) {
        enemies[i].type = UNIT_INFANTRY;
        enemies[i].maxHp = 2;
        enemies[i].vx = (float)random(-10, 11) * 0.04f;
        enemies[i].vy = 0.52f;
      } else if (roll < 75) {
        enemies[i].type = UNIT_TANK;
        enemies[i].maxHp = 3;
        enemies[i].vx = (float)random(-8, 9) * 0.03f;
        enemies[i].vy = 0.34f;
      } else {
        enemies[i].type = UNIT_DRONE;
        enemies[i].maxHp = 1;
        enemies[i].vx = (float)random(-16, 17) * 0.05f;
        enemies[i].vy = 0.76f;
      }
      enemies[i].hp = enemies[i].maxHp;
      enemies[i].x = (float)random(26, SCREEN_W - 26);
      enemies[i].y = FIELD_TOP + 6.0f;
      enemies[i].fireAt = millis() + random(520, 1450);
      enemies[i].capturing = false;
      enemies[i].captureAngle = 0.0f;
      enemies[i].captureSpin = 0.0f;
      enemies[i].capturePullSpeed = 0.0f;
      return;
    }
  }
}

void releaseEnemy(Enemy &enemy, bool captured) {
  uint16_t color = unitColor(enemy.type, captured);
  spawnBurst(enemy.x, enemy.y, color, captured ? 5 : 4);
  if (!captured) {
    playSfxExplode();
  }
  enemy.active = false;
}

void convertEnemy(Enemy &enemy) {
  int slot = findFreeAllySlot();
  if (slot < 0) {
    score += 30;
    addProgress(3);
    releaseEnemy(enemy, true);
    setBanner("Reserve full", 700);
    return;
  }

  for (int i = 0; i < MAX_ALLIES; i++) {
    if (!allies[i].active) {
      allies[i].active = true;
      allies[i].type = enemy.type;
      allies[i].slot = (uint8_t)slot;
      allies[i].x = enemy.x;
      allies[i].y = enemy.y;
      allies[i].maxHp = unitBaseHp(enemy.type);
      allies[i].hp = allies[i].maxHp;
      allies[i].fireAt = millis() + 180;
      captures++;
      score += 50;
      addProgress(5);
      releaseEnemy(enemy, true);
      playSfxConvert();
      setBanner("Unit converted", 700);
      return;
    }
  }
}

void beginCapture(Enemy &enemy) {
  enemy.capturing = true;
  enemy.captureAngle = 0.0f;
  enemy.captureSpin = 0.08f;
  enemy.capturePullSpeed = 0.275f;
  playSfxCaptureLock();
  setBanner("Capture engaged", 500);
}

void tryCaptureEnemy() {
  if (findFreeAllySlot() < 0) {
    setBanner("Reserve full", 700);
    return;
  }

  float zoneX = 0.0f;
  float zoneY = 0.0f;
  captureZoneCenter(zoneX, zoneY);

  int bestIndex = -1;
  float bestDist = 999999.0f;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy &enemy = enemies[i];
    if (!enemy.active || enemy.capturing || enemy.hp > 1) continue;
    float dist = sqrtf(lengthSq(zoneX, zoneY, enemy.x, enemy.y));
    if (dist <= CAPTURE_ZONE_HIT_RADIUS + 10.0f && dist < bestDist) {
      bestDist = dist;
      bestIndex = i;
    }
  }

  if (bestIndex >= 0) {
    beginCapture(enemies[bestIndex]);
  } else {
    setBanner("No weak target", 450);
  }
}

void destroyEnemy(Enemy &enemy) {
  kills++;
  score += 25;
  addProgress(3);
  releaseEnemy(enemy, false);
}

void destroyAlly(Ally &ally) {
  spawnBurst(ally.x, ally.y, unitColor(ally.type, true), 6);
  playSfxGuardBreak();
  startVibration(220);
  ally.active = false;
  setBanner("Guard down", 500);
}

bool damageAllyShield(int amount) {
  for (int damage = 0; damage < amount; damage++) {
    int target = -1;
    float bestDist = 999999.0f;
    for (int i = 0; i < MAX_ALLIES; i++) {
      if (!allies[i].active) continue;
      float dist = lengthSq(allies[i].x, allies[i].y, player.x, player.y);
      if (dist < bestDist) {
        bestDist = dist;
        target = i;
      }
    }
    if (target < 0) {
      return false;
    }
    Ally &ally = allies[target];
    ally.hp--;
    spawnBurst(ally.x, ally.y, rgb565(255, 210, 120), 3);
    if (ally.hp <= 0) {
      destroyAlly(ally);
    } else {
      playSfxHit();
      setBanner("Guard hit", 250);
    }
  }
  return true;
}

void damageFormation(int amount) {
  int remaining = amount;
  while (remaining > 0) {
    if (damageAllyShield(1)) {
      remaining--;
      continue;
    }
    break;
  }
  if (remaining <= 0) return;

  player.hp -= remaining;
  if (player.hp <= 0) {
    player.hp = 0;
    gameState = GAME_OVER;
    stateTimerMs = millis();
    startVibration(320);
    playSfxGameOver();
  } else {
    startVibration(280);
    playSfxHit();
    setBanner("Hit!", 400);
  }
}

void updatePlayer() {
  float inputX = ((float)latestX - 2048.0f) / 2048.0f;
  float inputY = ((float)latestY - 2048.0f) / 2048.0f;
  const float deadZone = 0.10f;

  if (fabsf(inputX) < deadZone) {
    inputX = 0.0f;
  } else {
    float norm = (fabsf(inputX) - deadZone) / (1.0f - deadZone);
    inputX = copysignf(norm * norm, inputX);
  }

  if (fabsf(inputY) < deadZone) {
    inputY = 0.0f;
  } else {
    float norm = (fabsf(inputY) - deadZone) / (1.0f - deadZone);
    inputY = copysignf(norm * norm, inputY);
  }

  player.vx += inputX * PLAYER_ACCEL_X;
  player.vy += inputY * PLAYER_ACCEL_Y;
  player.vx *= PLAYER_DAMPING;
  player.vy *= PLAYER_DAMPING;
  player.vx = clampf(player.vx, -PLAYER_MAX_SPEED_X, PLAYER_MAX_SPEED_X);
  player.vy = clampf(player.vy, -PLAYER_MAX_SPEED_Y, PLAYER_MAX_SPEED_Y);
  if (fabsf(player.vx) < 0.03f) player.vx = 0.0f;
  if (fabsf(player.vy) < 0.03f) player.vy = 0.0f;

  player.x += player.vx;
  player.y += player.vy;
  player.x = clampf(player.x, 18.0f, SCREEN_W - 18.0f);
  player.y = clampf(player.y, FIELD_TOP + 14.0f, SCREEN_H - 16.0f);
  if (player.x <= 18.0f || player.x >= SCREEN_W - 18.0f) player.vx = 0.0f;
  if (player.y <= FIELD_TOP + 14.0f || player.y >= SCREEN_H - 16.0f) player.vy = 0.0f;

  unsigned long now = millis();
  bool weaponUnlocked = (now - stateTimerMs) >= 1000;
  if (weaponUnlocked && blueDown && now >= player.fireAt) {
    player.fireAt = now + 150;
    spawnPlayerBullet(false);
  }
  if (weaponUnlocked && greenPressed && now >= player.captureAt) {
    player.captureAt = now + 520;
    tryCaptureEnemy();
  }
}

void updatePlayerBullets() {
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    Bullet &bullet = playerBullets[i];
    if (!bullet.active) continue;

    bullet.x += bullet.vx;
    bullet.y += bullet.vy;
    if (bullet.y < FIELD_TOP - 10 || bullet.y > SCREEN_H + 10) {
      bullet.active = false;
      continue;
    }

    bool consumed = false;
    for (int j = 0; j < MAX_ENEMIES; j++) {
      Enemy &enemy = enemies[j];
      if (!enemy.active || enemy.capturing) continue;
      if (fabsf(bullet.x - enemy.x) > 11.0f || fabsf(bullet.y - enemy.y) > 11.0f) continue;

      if (!bullet.capture) {
        enemy.hp--;
        spawnBurst(enemy.x, enemy.y, rgb565(255, 255, 255), 2);
        if (enemy.hp <= 0) {
          destroyEnemy(enemy);
        } else {
          playSfxHit();
        }
      }
      bullet.active = false;
      consumed = true;
      break;
    }

    if (consumed) continue;
  }
}

void updateEnemyBullets() {
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    Bullet &bullet = enemyBullets[i];
    if (!bullet.active) continue;
    bullet.x += bullet.vx;
    bullet.y += bullet.vy;
    if (bullet.x < -8 || bullet.x > SCREEN_W + 8 || bullet.y < FIELD_TOP - 8 || bullet.y > SCREEN_H + 8) {
      bullet.active = false;
      continue;
    }

    if (fabsf(bullet.x - player.x) < 9.0f && fabsf(bullet.y - player.y) < 9.0f) {
      bullet.active = false;
      spawnBurst(player.x, player.y, rgb565(255, 90, 90), 3);
      damageFormation(1);
    }
  }
}

void updateEnemies() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy &enemy = enemies[i];
    if (!enemy.active) continue;

    if (enemy.capturing) {
      float dx = player.x - enemy.x;
      float dy = player.y - enemy.y;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist <= CAPTURE_CONTACT_RADIUS) {
        convertEnemy(enemy);
        continue;
      }
      if (dist < 1.0f) dist = 1.0f;
      enemy.capturePullSpeed = min(enemy.capturePullSpeed + 0.03f, 1.35f);
      enemy.captureSpin += 0.02f;
      enemy.captureAngle += enemy.captureSpin;
      enemy.x += (dx / dist) * enemy.capturePullSpeed;
      enemy.y += (dy / dist) * enemy.capturePullSpeed;
      continue;
    }

    enemy.x += enemy.vx;
    enemy.y += enemy.vy;
    if (enemy.x < 16 || enemy.x > SCREEN_W - 16) {
      enemy.vx = -enemy.vx;
      enemy.x = clampf(enemy.x, 16.0f, SCREEN_W - 16.0f);
    }

    if (enemy.type != UNIT_TANK && now >= enemy.fireAt && enemy.y > FIELD_TOP + 18) {
      float dx = player.x - enemy.x;
      float dy = player.y - enemy.y;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist < 1.0f) dist = 1.0f;
      float speed = enemy.type == UNIT_DRONE ? 1.75f : 1.35f;
      spawnEnemyBullet(enemy.x, enemy.y + 6, dx / dist * speed, dy / dist * speed);
      enemy.fireAt = now + (enemy.type == UNIT_DRONE ? 4320 : 6000) + random(0, 1280);
    }

    if (fabsf(enemy.x - player.x) < 12.0f && fabsf(enemy.y - player.y) < 12.0f) {
      releaseEnemy(enemy, false);
      spawnBurst(player.x, player.y, rgb565(255, 60, 60), 4);
      damageFormation(1);
      continue;
    }

    if (enemy.y > SCREEN_H + 12) {
      enemy.active = false;
      enemyPressure++;
      if (progressPoints > 0) progressPoints--;
      if ((enemyPressure % 3) == 0) {
        damageFormation(1);
        setBanner("Line breached", 700);
      }
    }
  }
}

void updateAllies() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_ALLIES; i++) {
    Ally &ally = allies[i];
    if (!ally.active) continue;

    int row = ally.slot / 3;
    int col = ally.slot % 3;
    float targetX = player.x - 34.0f + col * 34.0f;
    float targetY = player.y + 22.0f + row * 24.0f;
    ally.x += (targetX - ally.x) * 0.18f;
    ally.y += (targetY - ally.y) * 0.18f;
    ally.x = clampf(ally.x, 14.0f, SCREEN_W - 14.0f);
    ally.y = clampf(ally.y, FIELD_TOP + 12.0f, SCREEN_H - 10.0f);

    int targetEnemy = -1;
    float bestDist = 999999.0f;
    for (int j = 0; j < MAX_ENEMIES; j++) {
      if (!enemies[j].active) continue;
      float dist = lengthSq(ally.x, ally.y, enemies[j].x, enemies[j].y);
      if (dist < bestDist) {
        bestDist = dist;
        targetEnemy = j;
      }
    }

    if (targetEnemy >= 0 && now >= ally.fireAt) {
      Enemy &enemy = enemies[targetEnemy];
      float dx = enemy.x - ally.x;
      float dy = enemy.y - ally.y;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist < 1.0f) dist = 1.0f;
      float speed = 1.7f;
      spawnFriendlyBullet(ally.x, ally.y - 6, dx / dist * speed, dy / dist * speed, false);
      ally.fireAt = now + 1720 + ally.slot * 120;
    }
  }
}

void updateSparks() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_SPARKS; i++) {
    if (!sparks[i].active) continue;
    sparks[i].x += sparks[i].vx;
    sparks[i].y += sparks[i].vy;
    sparks[i].vx *= 0.96f;
    sparks[i].vy *= 0.96f;
    if (now >= sparks[i].endAt) {
      sparks[i].active = false;
    }
  }
}

void updateSpawning() {
  unsigned long now = millis();
  if (now - lastSpawnMs < 680) return;
  lastSpawnMs = now;

  int desired = 5 + progressPoints / 18;
  int activeEnemies = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) activeEnemies++;
  }
  if (activeEnemies < desired) {
    spawnEnemy();
  }
}

void tickGame() {
  updatePlayer();
  updateSpawning();
  updateAllies();
  updatePlayerBullets();
  updateEnemyBullets();
  updateEnemies();
  updateSparks();
}

template <typename CanvasT>
void drawGrid(CanvasT &canvas) {
  uint16_t bg = rgb565(16, 32, 24);
  uint16_t line = rgb565(28, 58, 42);
  canvas.fillScreen(bg);
  for (int x = 0; x < SCREEN_W; x += 24) {
    canvas.drawFastVLine(x, FIELD_TOP, SCREEN_H - FIELD_TOP, line);
  }
  for (int y = FIELD_TOP; y < SCREEN_H; y += 24) {
    canvas.drawFastHLine(0, y, SCREEN_W, line);
  }
  canvas.fillRect(0, FIELD_TOP - 4, SCREEN_W, 4, rgb565(70, 90, 70));
  canvas.fillRect(0, SCREEN_H - 22, SCREEN_W, 22, rgb565(46, 62, 32));
}

template <typename CanvasT>
void drawUnit(CanvasT &canvas, float x, float y, UnitType type, bool friendly, bool capturable) {
  uint16_t main = unitColor(type, friendly);
  uint16_t dark = darken565(main, 70);
  uint16_t accent = friendly ? rgb565(232, 255, 244) : rgb565(255, 220, 170);
  uint16_t trim = friendly ? rgb565(84, 180, 255) : rgb565(255, 84, 84);
  uint16_t shadow = darken565(main, 120);
  int ix = (int)x;
  int iy = (int)y;

  if (type == UNIT_INFANTRY) {
    canvas.fillCircle(ix, iy - 8, 5, accent);
    canvas.fillRoundRect(ix - 6, iy - 2, 12, 13, 3, main);
    canvas.fillRect(ix - 9, iy, 3, 9, dark);
    canvas.fillRect(ix + 6, iy, 3, 9, dark);
    canvas.fillRect(ix - 4, iy + 10, 3, 7, shadow);
    canvas.fillRect(ix + 1, iy + 10, 3, 7, shadow);
    canvas.drawFastHLine(ix - 10, iy + 4, 20, trim);
    canvas.fillRect(ix - 2, iy + 1, 4, 4, accent);
    canvas.drawFastHLine(ix - 6, iy - 3, 12, shadow);
  } else if (type == UNIT_TANK) {
    canvas.fillRoundRect(ix - 14, iy - 4, 28, 14, 4, dark);
    canvas.fillRoundRect(ix - 11, iy - 10, 22, 14, 4, main);
    canvas.fillRoundRect(ix - 5, iy - 15, 10, 8, 3, accent);
    canvas.fillRect(ix - 2, iy - 20, 4, 12, shadow);
    canvas.fillRect(ix - 13, iy + 9, 26, 3, shadow);
    canvas.fillCircle(ix - 8, iy + 11, 4, shadow);
    canvas.fillCircle(ix + 8, iy + 11, 4, shadow);
    canvas.drawFastHLine(ix - 10, iy - 1, 20, trim);
    canvas.drawFastVLine(ix, iy - 8, 8, trim);
  } else {
    canvas.fillTriangle(ix, iy - 14, ix - 13, iy + 3, ix + 13, iy + 3, main);
    canvas.fillTriangle(ix, iy - 8, ix - 8, iy + 10, ix + 8, iy + 10, dark);
    canvas.fillRoundRect(ix - 5, iy - 3, 10, 7, 3, accent);
    canvas.fillRect(ix - 2, iy + 3, 4, 12, shadow);
    canvas.drawFastHLine(ix - 10, iy + 2, 20, trim);
    canvas.drawFastVLine(ix - 9, iy - 2, 8, trim);
    canvas.drawFastVLine(ix + 9, iy - 2, 8, trim);
  }

  if (capturable) {
    canvas.drawCircle(ix, iy, 14, rgb565(255, 235, 80));
  }
}

template <typename CanvasT>
void drawCaptureZone(CanvasT &canvas) {
  float zoneXf = 0.0f;
  float zoneYf = 0.0f;
  captureZoneCenter(zoneXf, zoneYf);
  int zoneX = (int)zoneXf;
  int zoneY = (int)zoneYf;
  float pulse = 0.5f + 0.5f * sinf(millis() * 0.0105f);
  int displayRadius = 18 + (int)(pulse * 6.0f);
  uint16_t ring = rgb565(255, 225, 90);
  uint16_t mark = rgb565(255, 246, 180);
  canvas.drawCircle(zoneX, zoneY, displayRadius, ring);
  canvas.drawCircle(zoneX, zoneY, displayRadius - 5, darken565(ring, 80));

  canvas.drawFastHLine(zoneX - 8, zoneY, 16, mark);
  canvas.drawFastVLine(zoneX, zoneY - 8, 16, mark);
  canvas.drawFastHLine(zoneX - 16, zoneY - 10, 6, mark);
  canvas.drawFastHLine(zoneX + 10, zoneY - 10, 6, mark);
  canvas.drawFastHLine(zoneX - 16, zoneY + 10, 6, mark);
  canvas.drawFastHLine(zoneX + 10, zoneY + 10, 6, mark);
  canvas.drawFastVLine(zoneX - 10, zoneY - 16, 6, mark);
  canvas.drawFastVLine(zoneX - 10, zoneY + 10, 6, mark);
  canvas.drawFastVLine(zoneX + 10, zoneY - 16, 6, mark);
  canvas.drawFastVLine(zoneX + 10, zoneY + 10, 6, mark);
}

template <typename CanvasT>
void drawCaptureTethers(CanvasT &canvas) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active || !enemies[i].capturing) continue;
    int x0 = (int)player.x;
    int y0 = (int)player.y;
    int x1 = (int)enemies[i].x;
    int y1 = (int)enemies[i].y;
    canvas.drawLine(x0, y0, x1, y1, rgb565(255, 230, 110));
    canvas.drawLine(x0 - 1, y0, x1 - 1, y1, rgb565(255, 180, 80));
  }
}

template <typename CanvasT>
void drawCaptureSpin(CanvasT &canvas, const Enemy &enemy) {
  if (!enemy.capturing) return;
  int cx = (int)enemy.x;
  int cy = (int)enemy.y;
  float a = enemy.captureAngle;
  for (int i = 0; i < 4; i++) {
    float ang = a + i * 1.5707963f;
    int ox = (int)(cosf(ang) * 12.0f);
    int oy = (int)(sinf(ang) * 12.0f);
    canvas.fillCircle(cx + ox, cy + oy, 2, rgb565(255, 225, 120));
  }
}

template <typename CanvasT>
void drawBullet(CanvasT &canvas, const Bullet &bullet) {
  uint16_t color = bullet.capture ? rgb565(255, 235, 80) : rgb565(220, 255, 255);
  int x0 = (int)bullet.x;
  int y0 = (int)bullet.y;
  float vx = bullet.vx;
  float vy = bullet.vy;
  float len = sqrtf(vx * vx + vy * vy);
  if (len < 0.01f) len = 0.01f;
  float nx = vx / len;
  float ny = vy / len;
  int tail = bullet.capture ? 12 : 9;
  int x1 = (int)(bullet.x - nx * tail);
  int y1 = (int)(bullet.y - ny * tail);

  canvas.drawLine(x0, y0, x1, y1, color);
  canvas.drawLine(x0 + 1, y0, x1 + 1, y1, darken565(color, 70));
  if (bullet.capture) {
    canvas.drawCircle(x0, y0, 5, rgb565(255, 200, 60));
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.fillRect(0, 0, SCREEN_W, HUD_H, rgb565(12, 18, 20));
  canvas.drawFastHLine(0, HUD_H - 1, SCREEN_W, rgb565(65, 90, 95));
  canvas.setTextColor(rgb565(210, 255, 230), rgb565(12, 18, 20));

  uint16_t hpOn = rgb565(92, 228, 120);
  uint16_t hpOff = rgb565(108, 112, 118);
  int hpStartX = 12;
  int hpY = 14;
  for (int i = 0; i < 5; i++) {
    if (i < player.hp) {
      canvas.fillCircle(hpStartX + i * 16, hpY, 4, hpOn);
      canvas.drawCircle(hpStartX + i * 16, hpY, 5, rgb565(180, 255, 196));
    } else {
      canvas.fillCircle(hpStartX + i * 16, hpY, 3, hpOff);
    }
  }

  char line[48];
  snprintf(line, sizeof(line), "SCORE %d", score);
  canvas.drawString(line, 104, 7, 2);
  snprintf(line, sizeof(line), "ALLY %d  CAP %d", allyCount(), captures);
  canvas.drawString(line, 252, 7, 2);
}

template <typename CanvasT>
void drawBanner(CanvasT &canvas) {
  if (millis() >= bannerUntilMs || bannerText[0] == '\0') return;
  canvas.fillRoundRect(124, 40, 232, 22, 6, rgb565(32, 44, 38));
  canvas.drawRoundRect(124, 40, 232, 22, 6, rgb565(120, 156, 126));
  canvas.setTextColor(rgb565(255, 248, 214), rgb565(32, 44, 38));
  canvas.drawCentreString(bannerText, SCREEN_W / 2, 44, 2);
}

template <typename CanvasT>
void drawPlayingScene(CanvasT &canvas) {
  drawGrid(canvas);
  drawHud(canvas);
  drawCaptureZone(canvas);

  canvas.fillRect(SCREEN_W / 2 - 42, FIELD_TOP + 6, 84, 10, rgb565(90, 38, 38));
  canvas.fillRect(SCREEN_W / 2 - 32, FIELD_TOP + 16, 64, 8, rgb565(150, 60, 60));
  canvas.drawString("ENEMY HQ", SCREEN_W / 2 - 32, FIELD_TOP + 2, 1);

  for (int i = 0; i < MAX_SPARKS; i++) {
    if (sparks[i].active) {
      canvas.fillCircle((int)sparks[i].x, (int)sparks[i].y, 1, sparks[i].color);
    }
  }

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    bool capturable = enemies[i].hp <= 1;
    drawUnit(canvas, enemies[i].x, enemies[i].y, enemies[i].type, false, capturable);
    drawCaptureSpin(canvas, enemies[i]);
    if (enemies[i].maxHp > 1) {
      canvas.drawRect((int)enemies[i].x - 10, (int)enemies[i].y - 17, 20, 4, rgb565(90, 90, 90));
      int hpW = (18 * enemies[i].hp) / enemies[i].maxHp;
      canvas.fillRect((int)enemies[i].x - 9, (int)enemies[i].y - 16, hpW, 2, capturable ? rgb565(255, 220, 70) : rgb565(255, 100, 90));
    }
  }

  for (int i = 0; i < MAX_ALLIES; i++) {
    if (allies[i].active) {
      drawUnit(canvas, allies[i].x, allies[i].y, allies[i].type, true, false);
    }
  }

  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (playerBullets[i].active) drawBullet(canvas, playerBullets[i]);
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (enemyBullets[i].active) {
      canvas.fillCircle((int)enemyBullets[i].x, (int)enemyBullets[i].y, 2, rgb565(255, 120, 120));
    }
  }

  drawCaptureTethers(canvas);
  drawUnit(canvas, player.x, player.y, UNIT_TANK, true, false);
  canvas.drawCircle((int)player.x, (int)player.y, 18, rgb565(140, 255, 220));

  drawBanner(canvas);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  uint16_t bg = rgb565(12, 18, 22);
  canvas.fillScreen(bg);
  for (int y = 0; y < SCREEN_H; y += 20) {
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(20, 42, 40));
  }
  for (int x = 0; x < SCREEN_W; x += 20) {
    canvas.drawFastVLine(x, 0, SCREEN_H, rgb565(20, 42, 40));
  }

  canvas.fillRoundRect(88, 42, 304, 78, 10, rgb565(32, 52, 46));
  canvas.drawRoundRect(88, 42, 304, 78, 10, rgb565(132, 180, 146));
  canvas.setTextColor(rgb565(245, 250, 228), rgb565(32, 52, 46));
  canvas.drawCentreString("FIELD COMBAT", SCREEN_W / 2, 58, 4);
  canvas.drawCentreString("Lock weak targets inside the capture ring", SCREEN_W / 2, 92, 2);

  drawUnit(canvas, 160, 180, UNIT_INFANTRY, false, true);
  drawUnit(canvas, 240, 172, UNIT_TANK, true, false);
  drawUnit(canvas, 320, 180, UNIT_DRONE, true, false);
  canvas.drawString("Blue: fire and weaken", 118, 220, 2);
  canvas.drawString("Green: pull weak unit in the ring", 118, 244, 2);
  canvas.drawString("Joystick: move and place capture ring", 118, 268, 2);

  bool showBlink = ((millis() / 450) % 2) == 0;
  if (showBlink) {
    canvas.setTextColor(rgb565(255, 232, 130), bg);
    canvas.drawCentreString("Press blue to deploy", SCREEN_W / 2, 294, 2);
  }
}

template <typename CanvasT>
void drawGameOverSplash(CanvasT &canvas) {
  uint16_t bg = rgb565(34, 12, 12);
  canvas.fillScreen(bg);
  canvas.setTextColor(rgb565(255, 176, 160), bg);
  canvas.drawCentreString("GAME OVER", SCREEN_W / 2, 112, 6);
  canvas.setTextColor(rgb565(255, 220, 180), bg);
  canvas.drawCentreString("Retreating from sector...", SCREEN_W / 2, 176, 2);
}

template <typename CanvasT>
void drawLeaderboard(CanvasT &canvas) {
  uint16_t bg = rgb565(16, 18, 30);
  uint16_t rowBg = rgb565(48, 54, 76);
  uint16_t title = rgb565(246, 212, 90);

  canvas.fillScreen(bg);
  canvas.fillRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, bg);
  canvas.drawRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, rgb565(120, 150, 220));
  canvas.setTextColor(title, bg);
  canvas.drawCentreString("WAR BOARD", SCREEN_W / 2, 48, 4);

  char buf[48];
  canvas.setTextColor(TFT_WHITE, bg);
  snprintf(buf, sizeof(buf), "Score %d  Cap %d  KO %d", score, captures, kills);
  canvas.drawCentreString(buf, SCREEN_W / 2, 80, 2);

  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    int y = 112 + i * 28;
    uint16_t fillColor = (i == latestRank) ? rgb565(84, 66, 26) : rowBg;
    uint16_t textColor = (i == latestRank) ? rgb565(250, 224, 120) : TFT_WHITE;
    canvas.fillRoundRect(92, y - 8, 296, 24, 6, fillColor);
    canvas.setTextColor(textColor, fillColor);
    snprintf(buf, sizeof(buf), "%d. %d", i + 1, leaderboard[i]);
    canvas.drawString(buf, 156, y - 4, 2);
    if (i == latestRank) {
      canvas.fillTriangle(344, y + 4, 360, y - 4, 360, y + 12, rgb565(250, 210, 92));
      canvas.fillCircle(328, y + 4, 6, rgb565(250, 210, 92));
    }
  }

  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawString("Blue Retry", 130, 286, 2);
  canvas.drawString("Green Title", 310, 286, 2);
}

void renderPlayingDirect() {
  ScreenCanvas whole = {&tft};
  drawPlayingScene(whole);
}

void renderPlaying() {
  if (frameBufferReady) {
    WindowCanvas left = {&frameBufferLeft, 0};
    WindowCanvas right = {&frameBufferRight, FRAME_W};
    drawPlayingScene(left);
    drawPlayingScene(right);
    frameBufferLeft.pushSprite(0, 0);
    frameBufferRight.pushSprite(FRAME_W, 0);
  } else {
    renderPlayingDirect();
  }
}

void renderStatic(GameState state) {
  if (state == TITLE) {
    if (frameBufferReady) {
      WindowCanvas left = {&frameBufferLeft, 0};
      WindowCanvas right = {&frameBufferRight, FRAME_W};
      drawTitle(left);
      drawTitle(right);
      frameBufferLeft.pushSprite(0, 0);
      frameBufferRight.pushSprite(FRAME_W, 0);
    } else {
      ScreenCanvas whole = {&tft};
      drawTitle(whole);
    }
  } else if (state == LEADERBOARD) {
    if (frameBufferReady) {
      WindowCanvas left = {&frameBufferLeft, 0};
      WindowCanvas right = {&frameBufferRight, FRAME_W};
      drawLeaderboard(left);
      drawLeaderboard(right);
      frameBufferLeft.pushSprite(0, 0);
      frameBufferRight.pushSprite(FRAME_W, 0);
    } else {
      ScreenCanvas whole = {&tft};
      drawLeaderboard(whole);
    }
  } else if (state == GAME_OVER) {
    if (frameBufferReady) {
      WindowCanvas left = {&frameBufferLeft, 0};
      WindowCanvas right = {&frameBufferRight, FRAME_W};
      drawGameOverSplash(left);
      drawGameOverSplash(right);
      frameBufferLeft.pushSprite(0, 0);
      frameBufferRight.pushSprite(FRAME_W, 0);
    } else {
      ScreenCanvas whole = {&tft};
      drawGameOverSplash(whole);
    }
  }
}

void setup() {
  pinMode(x_pin, INPUT);
  pinMode(y_pin, INPUT);
  pinMode(green_btn_pin, INPUT_PULLUP);
  pinMode(blue_btn_pin, INPUT_PULLUP);
  pinMode(green_led_pin, OUTPUT);
  pinMode(blue_led_pin, OUTPUT);
  ledcSetup(reverse_ch, motor_frequency, motor_resolution);
  ledcAttachPin(reverse_pin, reverse_ch);
  ledcSetup(forward_ch, motor_frequency, motor_resolution);
  ledcAttachPin(forward_pin, forward_ch);
  stopMotor();

  digitalWrite(green_led_pin, HIGH);
  digitalWrite(blue_led_pin, HIGH);

  analogReadResolution(12);
  randomSeed((uint32_t)analogRead(x_pin) << 12 ^ (uint32_t)analogRead(y_pin));

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);

  initFrameBuffers();
  initAudio();
  lastRenderedState = GAME_OVER;
  gameState = TITLE;
}

void loop() {
  readInput();

  if (gameState == TITLE) {
    if (bluePressed) beginGame();
  } else if (gameState == PLAYING) {
    unsigned long now = millis();
    while (now - lastStepMs >= GAME_STEP_MS) {
      lastStepMs += GAME_STEP_MS;
      tickGame();
      if (gameState != PLAYING) break;
    }
    renderPlaying();
  } else if (gameState == GAME_OVER) {
    if (!scoreSubmitted) {
      submitScoreToLeaderboard();
    }
    if (millis() - stateTimerMs >= 1200) {
      gameState = LEADERBOARD;
      stateTimerMs = millis();
    }
  } else if (gameState == LEADERBOARD) {
    if (millis() - stateTimerMs >= 1000) {
      if (bluePressed) {
        beginGame();
      } else if (greenPressed) {
        gameState = TITLE;
      }
    }
  } else {
    if (bluePressed) {
      gameState = TITLE;
    }
  }

  if (gameState != PLAYING) {
    if (gameState != lastRenderedState || millis() - titleBlinkMs >= 200) {
      titleBlinkMs = millis();
      renderStatic(gameState);
      lastRenderedState = gameState;
    }
  } else {
    lastRenderedState = PLAYING;
  }

  updateVibration();
  delay(1);
}
