#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <esp_system.h>

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
static const int HUD_H = 28;
static const int SKY_BOTTOM = 104;
static const int MAX_ITEMS = 14;
static const int MAX_EXPLOSIONS = 4;
static const int MAX_SCORE_POPUPS = 3;
static const int LEADERBOARD_SIZE = 5;

static const float PI_F = 3.14159265f;
static const float DEG_TO_RAD_F = PI_F / 180.0f;
static const float HOOK_PIVOT_X = SCREEN_W * 0.5f;
static const float MINER_FEET_Y = SKY_BOTTOM - 4.0f;
static const float HOOK_PIVOT_Y = MINER_FEET_Y - 26.0f;
static const float HOOK_MIN_LEN = 26.0f;
static const float HOOK_MAX_LEN = 312.0f;
static const float HOOK_SHOOT_SPEED = 5.0f;
static const float HOOK_RETRACT_SPEED = 6.0f;
static const float HOOK_SWING_SPEED = 1.2f;
static const float HOOK_SWING_LIMIT = 72.0f;

enum GameState {
  TITLE,
  PLAYING,
  LEVEL_CLEAR,
  GAME_OVER
};

enum HookState {
  HOOK_SWINGING,
  HOOK_SHOOTING,
  HOOK_PULLING
};

enum ItemType {
  ITEM_NONE,
  ITEM_SMALL_GOLD,
  ITEM_BIG_GOLD,
  ITEM_STONE,
  ITEM_DIAMOND,
  ITEM_GIFT
};

struct MineItem {
  bool active;
  ItemType type;
  float x;
  float y;
  float radius;
  float weight;
  int value;
};

struct Hook {
  HookState state;
  float angleDeg;
  float swingDir;
  float launchSwingDir;
  float length;
  int targetIndex;
};

struct Explosion {
  bool active;
  float x;
  float y;
  float radius;
  unsigned long startMs;
  unsigned long durationMs;
};

struct ScorePopup {
  bool active;
  int delta;
  float x;
  float y;
  unsigned long startMs;
  unsigned long durationMs;
};

struct Voice {
  bool active;
  float freq;
  float phase;
  int remainSamples;
  float volume;
  uint8_t wave;
};

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;
Hook hook;
MineItem items[MAX_ITEMS];
Explosion explosions[MAX_EXPLOSIONS];
ScorePopup scorePopups[MAX_SCORE_POPUPS];
int itemCount = 0;
int leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int latestRank = -1;

bool frameBufferReady = false;
bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
int latestX = 2048;
int latestY = 2048;

int levelIndex = 1;
int score = 0;
int targetScore = 650;
int timeLeft = 60;
int bombCount = 3;
unsigned long levelStartMs = 0;
unsigned long lastSecondTickMs = 0;
unsigned long lastUpdateMs = 0;
unsigned long stateStartedMs = 0;
int lastCountdownCue = -1;

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

Voice sfxVoices[6];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
TaskHandle_t audioTaskHandle = nullptr;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t sandColor() { return rgb565(180, 128, 58); }
uint16_t dirtColor() { return rgb565(114, 71, 31); }
uint16_t tunnelColor() { return rgb565(83, 48, 18); }
uint16_t ropeColor() { return rgb565(245, 231, 165); }
uint16_t accentColor() { return rgb565(255, 212, 70); }

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void pulseMotor(uint16_t durationMs) {
  ledcWrite(forward_ch, 180);
  delay(durationMs);
  stopMotor();
}

void readInputs() {
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
  if (preferredChannel >= 0 && preferredChannel < 6) {
    assignVoice(sfxVoices[preferredChannel], freq, durationSamples, volume, wave);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }

  for (int i = 0; i < 6; ++i) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }

  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxHookShoot() {
  startVoice(920.0f, 45, 0.10f, 0, 0);
  startVoice(1240.0f, 35, 0.07f, 0, 1);
}

void playSfxBomb() {
  startVoice(180.0f, 140, 0.14f, 2, 2);
  startVoice(110.0f, 200, 0.12f, 0, 3);
  startVoice(420.0f, 60, 0.07f, 0, 4);
}

void playSfxCountdown() {
  startVoice(980.0f, 70, 0.08f, 0, 0);
}

void playSfxClear() {
  startVoice(523.0f, 90, 0.08f, 0, 0);
  startVoice(659.0f, 100, 0.09f, 0, 1);
  startVoice(784.0f, 180, 0.10f, 0, 2);
}

void playSfxGameOver() {
  startVoice(330.0f, 90, 0.09f, 0, 0);
  startVoice(220.0f, 120, 0.10f, 0, 1);
  startVoice(147.0f, 220, 0.11f, 0, 2);
}

float sampleVoice(Voice &voice) {
  if (!voice.active || voice.remainSamples <= 0) {
    voice.active = false;
    return 0.0f;
  }

  float s = 0.0f;
  if (voice.wave == 0) {
    s = (voice.phase < 0.5f) ? voice.volume : -voice.volume;
  } else if (voice.wave == 1) {
    s = sinf(voice.phase * PI_F * 2.0f) * voice.volume;
  } else {
    s = ((float)random(-1000, 1001) / 1000.0f) * voice.volume;
  }

  voice.phase += voice.freq / SAMPLE_RATE;
  if (voice.phase >= 1.0f) {
    voice.phase -= 1.0f;
  }
  voice.remainSamples--;
  if (voice.remainSamples <= 0) {
    voice.active = false;
  }
  return s;
}

void audioTick() {
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    float mix = 0.0f;
    taskENTER_CRITICAL(&audioMux);
    for (int v = 0; v < 6; ++v) {
      mix += sampleVoice(sfxVoices[v]);
    }
    taskEXIT_CRITICAL(&audioMux);

    if (mix > 0.7f) {
      mix = 0.7f;
    } else if (mix < -0.7f) {
      mix = -0.7f;
    }
    audioBuffer[i] = (int16_t)(mix * 32767.0f);
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_NUM_1, (const char *)audioBuffer, AUDIO_CHUNK_SAMPLES * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
}

void audioTaskMain(void *param) {
  (void)param;
  while (true) {
    audioTick();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

float randomRange(float minV, float maxV) {
  return minV + ((float)random(0, 10000) / 10000.0f) * (maxV - minV);
}

bool overlapsExisting(float x, float y, float radius) {
  for (int i = 0; i < itemCount; ++i) {
    if (!items[i].active) {
      continue;
    }
    float dx = x - items[i].x;
    float dy = y - items[i].y;
    float minDist = radius + items[i].radius + 14.0f;
    if (dx * dx + dy * dy < minDist * minDist) {
      return true;
    }
  }
  return false;
}

void addItem(ItemType type, float x, float y) {
  if (itemCount >= MAX_ITEMS) {
    return;
  }

  MineItem &it = items[itemCount++];
  it.active = true;
  it.type = type;
  it.x = x;
  it.y = y;

  if (type == ITEM_SMALL_GOLD) {
    it.radius = 11.0f;
    it.weight = 1.2f;
    it.value = 80;
  } else if (type == ITEM_BIG_GOLD) {
    it.radius = 19.0f;
    it.weight = 2.6f;
    it.value = 240;
  } else if (type == ITEM_STONE) {
    it.radius = 16.0f;
    it.weight = 3.4f;
    it.value = 35;
  } else if (type == ITEM_DIAMOND) {
    it.radius = 9.0f;
    it.weight = 0.8f;
    it.value = 150;
  } else {
    it.radius = 10.0f;
    it.weight = 1.4f;
    it.value = 120;
  }
}

void populateLevel() {
  itemCount = 0;
  for (int i = 0; i < MAX_ITEMS; ++i) {
    items[i].active = false;
  }

  int bigGoldCount = 2 + (levelIndex / 2);
  int smallGoldCount = 4 + levelIndex;
  int stoneCount = 3 + levelIndex;
  int diamondCount = 2 + (levelIndex / 3);
  int giftCount = 1 + ((levelIndex >= 3) ? 1 : 0);

  while (bigGoldCount-- > 0 && itemCount < MAX_ITEMS) {
    for (int tries = 0; tries < 30; ++tries) {
      float x = randomRange(54.0f, SCREEN_W - 54.0f);
      float y = randomRange(148.0f, SCREEN_H - 26.0f);
      if (!overlapsExisting(x, y, 19.0f)) {
        addItem(ITEM_BIG_GOLD, x, y);
        break;
      }
    }
  }

  while (smallGoldCount-- > 0 && itemCount < MAX_ITEMS) {
    for (int tries = 0; tries < 30; ++tries) {
      float x = randomRange(34.0f, SCREEN_W - 34.0f);
      float y = randomRange(132.0f, SCREEN_H - 24.0f);
      if (!overlapsExisting(x, y, 11.0f)) {
        addItem(ITEM_SMALL_GOLD, x, y);
        break;
      }
    }
  }

  while (stoneCount-- > 0 && itemCount < MAX_ITEMS) {
    for (int tries = 0; tries < 30; ++tries) {
      float x = randomRange(42.0f, SCREEN_W - 42.0f);
      float y = randomRange(140.0f, SCREEN_H - 22.0f);
      if (!overlapsExisting(x, y, 16.0f)) {
        addItem(ITEM_STONE, x, y);
        break;
      }
    }
  }

  while (diamondCount-- > 0 && itemCount < MAX_ITEMS) {
    for (int tries = 0; tries < 30; ++tries) {
      float x = randomRange(24.0f, SCREEN_W - 24.0f);
      float y = randomRange(128.0f, SCREEN_H - 20.0f);
      if (!overlapsExisting(x, y, 9.0f)) {
        addItem(ITEM_DIAMOND, x, y);
        break;
      }
    }
  }

  while (giftCount-- > 0 && itemCount < MAX_ITEMS) {
    for (int tries = 0; tries < 30; ++tries) {
      float x = randomRange(24.0f, SCREEN_W - 24.0f);
      float y = randomRange(136.0f, SCREEN_H - 20.0f);
      if (!overlapsExisting(x, y, 10.0f)) {
        addItem(ITEM_GIFT, x, y);
        break;
      }
    }
  }
}

void resetHook() {
  hook.state = HOOK_SWINGING;
  if (hook.angleDeg > HOOK_SWING_LIMIT) {
    hook.angleDeg = HOOK_SWING_LIMIT;
  } else if (hook.angleDeg < -HOOK_SWING_LIMIT) {
    hook.angleDeg = -HOOK_SWING_LIMIT;
  }
  hook.swingDir = hook.launchSwingDir;
  if (hook.swingDir == 0.0f) {
    hook.swingDir = 1.0f;
  }
  hook.length = HOOK_MIN_LEN;
  hook.targetIndex = -1;
}

void clearTransientEffects() {
  for (int i = 0; i < MAX_EXPLOSIONS; ++i) {
    explosions[i].active = false;
  }
  for (int i = 0; i < MAX_SCORE_POPUPS; ++i) {
    scorePopups[i].active = false;
  }
}

void beginLevel() {
  targetScore = 650 + (levelIndex - 1) * 280;
  timeLeft = max(35, 60 - (levelIndex - 1) * 2);
  bombCount = min(6, 3 + (levelIndex - 1) / 2);
  populateLevel();
  clearTransientEffects();
  resetHook();
  gameState = PLAYING;
  lastRenderedState = LEVEL_CLEAR;
  levelStartMs = millis();
  lastSecondTickMs = levelStartMs;
  lastUpdateMs = levelStartMs;
  stateStartedMs = levelStartMs;
  lastCountdownCue = -1;
  digitalWrite(green_led_pin, LOW);
  digitalWrite(blue_led_pin, HIGH);
}

void startNewGame() {
  score = 0;
  levelIndex = 1;
  latestRank = -1;
  beginLevel();
}

void recordScore() {
  latestRank = LEADERBOARD_SIZE;
  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    if (score > leaderboard[i]) {
      latestRank = i;
      break;
    }
  }

  if (latestRank >= LEADERBOARD_SIZE) {
    if (score <= leaderboard[LEADERBOARD_SIZE - 1]) {
      latestRank = -1;
      return;
    }
    latestRank = LEADERBOARD_SIZE - 1;
  }

  for (int i = LEADERBOARD_SIZE - 1; i > latestRank; --i) {
    leaderboard[i] = leaderboard[i - 1];
  }
  leaderboard[latestRank] = score;
}

void startPulling(int targetIndex) {
  hook.state = HOOK_PULLING;
  hook.targetIndex = targetIndex;
  digitalWrite(blue_led_pin, LOW);
  pulseMotor(18);
}

float reelSpeed() {
  if (hook.targetIndex < 0 || !items[hook.targetIndex].active) {
    return HOOK_RETRACT_SPEED;
  }
  float speed = HOOK_RETRACT_SPEED - items[hook.targetIndex].weight * 1.4f;
  if (speed < 1.1f) {
    speed = 1.1f;
  }
  return speed;
}

void spawnExplosion(float x, float y, float radius) {
  for (int i = 0; i < MAX_EXPLOSIONS; ++i) {
    if (explosions[i].active) {
      continue;
    }
    explosions[i].active = true;
    explosions[i].x = x;
    explosions[i].y = y;
    explosions[i].radius = radius;
    explosions[i].startMs = millis();
    explosions[i].durationMs = 220;
    return;
  }

  explosions[0].active = true;
  explosions[0].x = x;
  explosions[0].y = y;
  explosions[0].radius = radius;
  explosions[0].startMs = millis();
  explosions[0].durationMs = 220;
}

void detonateHeldItem() {
  if (bombCount <= 0) {
    return;
  }
  if (hook.state != HOOK_PULLING || hook.targetIndex < 0) {
    return;
  }
  if (!items[hook.targetIndex].active) {
    return;
  }

  float blastX = items[hook.targetIndex].x;
  float blastY = items[hook.targetIndex].y;
  float blastR = items[hook.targetIndex].radius;
  bombCount--;
  spawnExplosion(blastX, blastY, blastR);
  items[hook.targetIndex].active = false;
  hook.targetIndex = -1;
  if (hook.length > HOOK_MIN_LEN + 20.0f) {
    hook.length -= 18.0f;
  }
  digitalWrite(green_led_pin, LOW);
  pulseMotor(28);
  digitalWrite(green_led_pin, HIGH);
  playSfxBomb();
}

void updateExplosions() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_EXPLOSIONS; ++i) {
    if (!explosions[i].active) {
      continue;
    }
    if (now - explosions[i].startMs >= explosions[i].durationMs) {
      explosions[i].active = false;
    }
  }
}

void spawnScorePopup(int delta) {
  for (int i = 0; i < MAX_SCORE_POPUPS; ++i) {
    if (scorePopups[i].active) {
      continue;
    }
    scorePopups[i].active = true;
    scorePopups[i].delta = delta;
    scorePopups[i].x = HOOK_PIVOT_X;
    scorePopups[i].y = HOOK_PIVOT_Y - 28.0f;
    scorePopups[i].startMs = millis();
    scorePopups[i].durationMs = 900;
    return;
  }

  scorePopups[0].active = true;
  scorePopups[0].delta = delta;
  scorePopups[0].x = HOOK_PIVOT_X;
  scorePopups[0].y = HOOK_PIVOT_Y - 28.0f;
  scorePopups[0].startMs = millis();
  scorePopups[0].durationMs = 900;
}

void applyScoreDelta(int delta) {
  score += delta;
  if (score < 0) {
    score = 0;
  }
  spawnScorePopup(delta);
}

void updateScorePopups() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_SCORE_POPUPS; ++i) {
    if (!scorePopups[i].active) {
      continue;
    }
    if (now - scorePopups[i].startMs >= scorePopups[i].durationMs) {
      scorePopups[i].active = false;
    }
  }
}

void updateHook(float dtScale) {
  if (hook.state == HOOK_SWINGING) {
    hook.angleDeg += hook.swingDir * HOOK_SWING_SPEED * dtScale;
    if (hook.angleDeg >= HOOK_SWING_LIMIT) {
      hook.angleDeg = HOOK_SWING_LIMIT;
      hook.swingDir = -1.0f;
    }
    if (hook.angleDeg <= -HOOK_SWING_LIMIT) {
      hook.angleDeg = -HOOK_SWING_LIMIT;
      hook.swingDir = 1.0f;
    }

    if (bluePressed) {
      hook.launchSwingDir = hook.swingDir;
      hook.state = HOOK_SHOOTING;
      digitalWrite(blue_led_pin, LOW);
      playSfxHookShoot();
    }
    return;
  }

  float angleRad = hook.angleDeg * DEG_TO_RAD_F;
  float dx = sinf(angleRad);
  float dy = cosf(angleRad);

  if (hook.state == HOOK_SHOOTING) {
    hook.length += HOOK_SHOOT_SPEED * dtScale;
    float tipX = HOOK_PIVOT_X + dx * hook.length;
    float tipY = HOOK_PIVOT_Y + dy * hook.length;

    if (tipX < 8.0f || tipX > SCREEN_W - 8.0f || tipY > SCREEN_H - 8.0f || hook.length >= HOOK_MAX_LEN) {
      startPulling(-1);
      return;
    }

    for (int i = 0; i < itemCount; ++i) {
      if (!items[i].active) {
        continue;
      }
      float ix = tipX - items[i].x;
      float iy = tipY - items[i].y;
      float hitR = items[i].radius + 4.0f;
      if (ix * ix + iy * iy <= hitR * hitR) {
        startPulling(i);
        return;
      }
    }
    return;
  }

  hook.length -= reelSpeed() * dtScale;
  if (hook.targetIndex >= 0 && items[hook.targetIndex].active) {
    float tipX = HOOK_PIVOT_X + dx * hook.length;
    float tipY = HOOK_PIVOT_Y + dy * hook.length;
    items[hook.targetIndex].x = tipX;
    items[hook.targetIndex].y = tipY + items[hook.targetIndex].radius;
  }
  if (hook.length <= HOOK_MIN_LEN) {
    hook.length = HOOK_MIN_LEN;
    if (hook.targetIndex >= 0 && items[hook.targetIndex].active) {
      applyScoreDelta(items[hook.targetIndex].value);
      items[hook.targetIndex].active = false;
      if (score >= targetScore) {
        playSfxClear();
        gameState = LEVEL_CLEAR;
        stateStartedMs = millis();
        digitalWrite(green_led_pin, HIGH);
        digitalWrite(blue_led_pin, HIGH);
      }
    }
    resetHook();
    digitalWrite(blue_led_pin, HIGH);
  }
}

template <typename CanvasT>
void drawCloud(CanvasT &canvas, int x, int y, int w, uint16_t color) {
  canvas.fillCircle(x, y, w / 4, color);
  canvas.fillCircle(x + w / 4, y - 8, w / 5, color);
  canvas.fillCircle(x + w / 2, y - 2, w / 4, color);
  canvas.fillRect(x, y, w / 2, w / 5, color);
}

template <typename CanvasT>
void drawMiner(CanvasT &canvas) {
  int baseX = (int)HOOK_PIVOT_X;
  int feetY = (int)MINER_FEET_Y;
  int bodyTop = feetY - 34;
  uint16_t skin = rgb565(255, 216, 176);
  uint16_t hatBrown = rgb565(110, 72, 24);
  uint16_t hatGold = accentColor();
  uint16_t shirtRed = rgb565(194, 62, 40);
  uint16_t denim = rgb565(46, 88, 143);
  uint16_t beard = rgb565(84, 48, 18);
  uint16_t boots = rgb565(58, 35, 18);
  uint16_t wood = rgb565(118, 78, 30);

  canvas.fillRoundRect(baseX - 42, SKY_BOTTOM - 10, 84, 10, 3, wood);
  canvas.drawFastHLine(baseX - 42, SKY_BOTTOM - 1, 84, rgb565(175, 128, 58));

  canvas.fillCircle(baseX, bodyTop - 10, 10, skin);
  canvas.fillRoundRect(baseX - 20, bodyTop - 24, 40, 7, 3, hatBrown);
  canvas.fillRoundRect(baseX - 15, bodyTop - 30, 30, 8, 3, hatGold);
  canvas.fillRect(baseX - 11, bodyTop - 16, 22, 3, hatBrown);

  canvas.fillCircle(baseX - 3, bodyTop - 11, 1, TFT_BLACK);
  canvas.fillCircle(baseX + 3, bodyTop - 11, 1, TFT_BLACK);
  canvas.drawFastHLine(baseX - 5, bodyTop - 6, 10, beard);
  canvas.fillRoundRect(baseX - 7, bodyTop - 5, 14, 7, 3, beard);

  canvas.fillRoundRect(baseX - 14, bodyTop, 28, 18, 5, shirtRed);
  canvas.fillRoundRect(baseX - 12, bodyTop + 7, 24, 16, 4, denim);
  canvas.fillRect(baseX - 2, bodyTop + 7, 4, 16, rgb565(214, 184, 62));
  canvas.fillCircle(baseX - 7, bodyTop + 8, 2, rgb565(214, 184, 62));
  canvas.fillCircle(baseX + 7, bodyTop + 8, 2, rgb565(214, 184, 62));
  canvas.fillRoundRect(baseX + 17, bodyTop + 5, 14, 18, 3, rgb565(120, 79, 31));
  canvas.drawCircle(baseX + 24, bodyTop + 14, 4, rgb565(88, 58, 24));
  canvas.drawFastVLine(baseX + 24, bodyTop - 3, 10, rgb565(88, 58, 24));
  canvas.drawFastHLine(baseX + 18, bodyTop + 21, 12, rgb565(78, 50, 19));

  canvas.fillRect(baseX - 10, bodyTop + 23, 8, 13, denim);
  canvas.fillRect(baseX + 2, bodyTop + 23, 8, 13, denim);
  canvas.fillRoundRect(baseX - 12, feetY - 2, 12, 6, 2, boots);
  canvas.fillRoundRect(baseX + 0, feetY - 2, 12, 6, 2, boots);
}

template <typename CanvasT>
void drawGround(CanvasT &canvas) {
  for (int y = HUD_H; y < SKY_BOTTOM; ++y) {
    float t = (float)(y - HUD_H) / (float)(SKY_BOTTOM - HUD_H);
    uint8_t r = (uint8_t)(36 + t * 202);
    uint8_t g = (uint8_t)(87 + t * 99);
    uint8_t b = (uint8_t)(158 - t * 39);
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(r, g, b));
  }

  canvas.fillRect(0, SKY_BOTTOM, SCREEN_W, SCREEN_H - SKY_BOTTOM, dirtColor());
  canvas.fillRect(0, SKY_BOTTOM, SCREEN_W, 14, sandColor());
  drawCloud(canvas, 56, 56, 52, rgb565(245, 241, 231));
  drawCloud(canvas, 352, 42, 64, rgb565(245, 241, 231));

  for (int y = SKY_BOTTOM + 18; y < SCREEN_H; y += 34) {
    canvas.drawFastHLine(0, y, SCREEN_W, tunnelColor());
  }

  for (int x = 22; x < SCREEN_W; x += 38) {
    canvas.drawFastVLine(x, SKY_BOTTOM + 12, SCREEN_H - SKY_BOTTOM - 12, tunnelColor());
  }
}

template <typename CanvasT>
void drawGold(CanvasT &canvas, int x, int y, int r, bool large) {
  uint16_t bright = rgb565(255, 221, 78);
  uint16_t mid = rgb565(235, 172, 32);
  canvas.fillCircle(x, y, r, mid);
  canvas.fillCircle(x - r / 4, y - r / 4, r / 2, bright);
  canvas.drawCircle(x, y, r, rgb565(160, 96, 10));
  if (large) {
    canvas.fillCircle(x + r / 3, y + r / 6, r / 4, bright);
  }
}

template <typename CanvasT>
void drawStone(CanvasT &canvas, int x, int y, int r) {
  canvas.fillCircle(x, y, r, rgb565(119, 122, 126));
  canvas.drawCircle(x, y, r, rgb565(63, 67, 74));
  canvas.drawFastHLine(x - r / 2, y - 2, r, rgb565(171, 173, 177));
  canvas.drawFastHLine(x - r / 3, y + 4, r - 3, rgb565(90, 95, 99));
}

template <typename CanvasT>
void drawDiamond(CanvasT &canvas, int x, int y, int r) {
  uint16_t c = rgb565(112, 244, 255);
  uint16_t edge = rgb565(28, 147, 166);
  canvas.fillTriangle(x, y - r, x + r, y, x, y + r, c);
  canvas.fillTriangle(x, y - r, x - r, y, x, y + r, rgb565(152, 255, 255));
  canvas.drawLine(x, y - r, x + r, y, edge);
  canvas.drawLine(x + r, y, x, y + r, edge);
  canvas.drawLine(x, y + r, x - r, y, edge);
  canvas.drawLine(x - r, y, x, y - r, edge);
}

template <typename CanvasT>
void drawGift(CanvasT &canvas, int x, int y, int r) {
  uint16_t box = rgb565(190, 69, 39);
  uint16_t ribbon = rgb565(251, 221, 96);
  canvas.fillRoundRect(x - r, y - r, r * 2, r * 2, 3, box);
  canvas.fillRect(x - 2, y - r, 4, r * 2, ribbon);
  canvas.fillRect(x - r, y - 2, r * 2, 4, ribbon);
}

template <typename CanvasT>
void drawBombIcon(CanvasT &canvas, int x, int y) {
  uint16_t shell = rgb565(52, 57, 64);
  uint16_t shine = rgb565(110, 118, 126);
  uint16_t fuse = rgb565(233, 190, 98);
  uint16_t spark = rgb565(255, 226, 92);
  canvas.fillCircle(x, y, 7, shell);
  canvas.fillCircle(x - 2, y - 2, 2, shine);
  canvas.drawLine(x + 2, y - 6, x + 7, y - 11, fuse);
  canvas.drawLine(x + 7, y - 11, x + 10, y - 8, fuse);
  canvas.drawPixel(x + 11, y - 8, spark);
  canvas.drawPixel(x + 10, y - 7, spark);
  canvas.drawPixel(x + 10, y - 9, spark);
}

template <typename CanvasT>
void drawExplosions(CanvasT &canvas) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_EXPLOSIONS; ++i) {
    if (!explosions[i].active) {
      continue;
    }

    float life = (float)(now - explosions[i].startMs) / (float)explosions[i].durationMs;
    if (life < 0.0f) {
      life = 0.0f;
    } else if (life > 1.0f) {
      life = 1.0f;
    }

    int x = (int)explosions[i].x;
    int y = (int)explosions[i].y;
    int outerR = (int)(explosions[i].radius + 8.0f + 18.0f * life);
    int midR = (int)(explosions[i].radius + 3.0f + 10.0f * life);
    int coreR = (int)(explosions[i].radius * (0.7f - 0.45f * life));
    if (coreR < 2) {
      coreR = 2;
    }

    uint16_t outer = rgb565(214, 72 + (uint8_t)(50 * (1.0f - life)), 20);
    uint16_t mid = rgb565(255, 166, 32);
    uint16_t core = rgb565(255, 235, 156);

    canvas.fillCircle(x, y, outerR, outer);
    canvas.fillCircle(x, y, midR, mid);
    canvas.fillCircle(x, y, coreR, core);

    for (int p = 0; p < 8; ++p) {
      float ang = (PI_F * 2.0f / 8.0f) * p + life * 0.8f;
      int px = (int)(x + cosf(ang) * (outerR + 5));
      int py = (int)(y + sinf(ang) * (outerR + 5));
      canvas.fillCircle(px, py, 2, mid);
    }
  }
}

template <typename CanvasT>
void drawScorePopups(CanvasT &canvas) {
  char buf[16];
  unsigned long now = millis();
  canvas.setTextDatum(MC_DATUM);
  for (int i = 0; i < MAX_SCORE_POPUPS; ++i) {
    if (!scorePopups[i].active) {
      continue;
    }

    float life = (float)(now - scorePopups[i].startMs) / (float)scorePopups[i].durationMs;
    if (life < 0.0f) {
      life = 0.0f;
    } else if (life > 1.0f) {
      life = 1.0f;
    }

    int x = (int)scorePopups[i].x;
    int y = (int)(scorePopups[i].y - 18.0f * life);
    uint16_t textColor = (scorePopups[i].delta >= 0) ? rgb565(96, 248, 112) : rgb565(255, 88, 72);
    uint16_t shadowColor = rgb565(18, 24, 18);
    snprintf(buf, sizeof(buf), "%+d", scorePopups[i].delta);

    canvas.setTextColor(shadowColor, shadowColor);
    canvas.drawString(buf, x + 1, y + 1, 4);
    canvas.drawString(buf, x, y + 1, 4);
    canvas.setTextColor(textColor, textColor);
    canvas.drawString(buf, x, y, 4);
    canvas.drawString(buf, x - 1, y, 4);
  }
  canvas.setTextDatum(TL_DATUM);
}

template <typename CanvasT>
void drawItem(CanvasT &canvas, const MineItem &item) {
  int x = (int)item.x;
  int y = (int)item.y;
  int r = (int)item.radius;
  if (item.type == ITEM_SMALL_GOLD) {
    drawGold(canvas, x, y, r, false);
  } else if (item.type == ITEM_BIG_GOLD) {
    drawGold(canvas, x, y, r, true);
  } else if (item.type == ITEM_STONE) {
    drawStone(canvas, x, y, r);
  } else if (item.type == ITEM_DIAMOND) {
    drawDiamond(canvas, x, y, r);
  } else {
    drawGift(canvas, x, y, r);
  }
}

template <typename CanvasT>
void drawItems(CanvasT &canvas) {
  for (int i = 0; i < itemCount; ++i) {
    if (!items[i].active) {
      continue;
    }
    if (hook.state == HOOK_PULLING && hook.targetIndex == i) {
      continue;
    }
    drawItem(canvas, items[i]);
  }
}

template <typename CanvasT>
void drawHook(CanvasT &canvas) {
  float angleRad = hook.angleDeg * DEG_TO_RAD_F;
  float dx = sinf(angleRad);
  float dy = cosf(angleRad);
  float tipX = HOOK_PIVOT_X + dx * hook.length;
  float tipY = HOOK_PIVOT_Y + dy * hook.length;
  int pivotX = (int)HOOK_PIVOT_X;
  int pivotY = (int)HOOK_PIVOT_Y;
  int hookX = (int)tipX;
  int hookY = (int)tipY;
  uint16_t metal = rgb565(198, 204, 210);
  uint16_t metalDark = rgb565(102, 110, 121);
  uint16_t ropeShadow = rgb565(166, 143, 92);
  float normalX = cosf(angleRad);
  float normalY = -sinf(angleRad);
  int headX = (int)(tipX - dx * 7.0f);
  int headY = (int)(tipY - dy * 7.0f);

  canvas.drawLine(pivotX, pivotY, hookX, hookY, ropeShadow);
  canvas.drawLine(pivotX + 1, pivotY, hookX + 1, hookY, ropeColor());
  canvas.drawLine(pivotX - 1, pivotY, hookX - 1, hookY, ropeColor());

  canvas.fillCircle(pivotX, pivotY, 3, metalDark);
  canvas.drawCircle(pivotX, pivotY, 5, accentColor());
  canvas.drawFastVLine(pivotX, pivotY - 7, 5, metalDark);

  canvas.fillCircle(headX, headY, 4, metal);
  canvas.drawCircle(headX, headY, 4, metalDark);
  canvas.drawLine(headX, headY, hookX, hookY, metalDark);

  canvas.drawLine(hookX, hookY, (int)(hookX - dx * 5.0f + normalX * 8.0f), (int)(hookY - dy * 5.0f + normalY * 8.0f), metal);
  canvas.drawLine(hookX, hookY, (int)(hookX - dx * 5.0f - normalX * 8.0f), (int)(hookY - dy * 5.0f - normalY * 8.0f), metal);
  canvas.drawLine(hookX, hookY, (int)(hookX + dx * 4.0f), (int)(hookY + dy * 4.0f), metalDark);
  canvas.drawPixel(hookX, hookY, TFT_WHITE);

  if (hook.targetIndex >= 0 && items[hook.targetIndex].active) {
    drawItem(canvas, items[hook.targetIndex]);
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  char buf[32];
  canvas.setTextDatum(TL_DATUM);
  canvas.fillRect(0, 0, SCREEN_W, HUD_H, rgb565(24, 24, 32));
  canvas.drawFastHLine(0, HUD_H - 1, SCREEN_W, accentColor());
  canvas.setTextColor(TFT_WHITE, rgb565(24, 24, 32));
  snprintf(buf, sizeof(buf), "LV %d", levelIndex);
  canvas.drawString(buf, 10, 6, 2);
  snprintf(buf, sizeof(buf), "S %d", score);
  canvas.drawString(buf, 92, 6, 2);
  snprintf(buf, sizeof(buf), "GOAL %d", targetScore);
  canvas.drawString(buf, 194, 6, 2);
  drawBombIcon(canvas, 352, 14);
  snprintf(buf, sizeof(buf), "x%d", bombCount);
  canvas.drawString(buf, 364, 6, 2);
  snprintf(buf, sizeof(buf), "%ds", timeLeft);
  canvas.drawRightString(buf, SCREEN_W - 12, 6, 2);
}

template <typename CanvasT>
void drawScene(CanvasT &canvas) {
  drawGround(canvas);
  drawItems(canvas);
  drawExplosions(canvas);
  drawMiner(canvas);
  drawHook(canvas);
  drawScorePopups(canvas);
  drawHud(canvas);
}

template <typename CanvasT>
void drawTitleScreen(CanvasT &canvas) {
  canvas.setTextDatum(MC_DATUM);
  canvas.fillRect(0, 0, SCREEN_W, SCREEN_H, rgb565(18, 24, 38));
  for (int y = 0; y < SCREEN_H; ++y) {
    uint8_t r = 18 + (uint8_t)(y / 10);
    uint8_t g = 24 + (uint8_t)(y / 12);
    uint8_t b = 38 + (uint8_t)(y / 6);
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(r, g, b));
  }

  canvas.fillRoundRect(34, 30, SCREEN_W - 68, SCREEN_H - 60, 18, rgb565(30, 24, 17));
  canvas.drawRoundRect(34, 30, SCREEN_W - 68, SCREEN_H - 60, 18, accentColor());
  canvas.setTextColor(accentColor(), rgb565(30, 24, 17));
  canvas.drawCentreString("GOLD MINER", SCREEN_W / 2, 56, 4);
  canvas.setTextColor(TFT_WHITE, rgb565(30, 24, 17));
  canvas.drawCentreString("Blue: fire hook / start", SCREEN_W / 2, 126, 2);
  canvas.drawCentreString("Green: bomb the hooked item", SCREEN_W / 2, 154, 2);
  canvas.drawCentreString("Bombs remove heavy junk instantly", SCREEN_W / 2, 188, 2);
  canvas.drawCentreString("Stone is slow and cheap", SCREEN_W / 2, 212, 2);
  drawGold(canvas, 96, 252, 20, true);
  drawDiamond(canvas, 196, 252, 16);
  drawStone(canvas, 284, 252, 18);
  drawBombIcon(canvas, 376, 252);
  canvas.setTextDatum(TL_DATUM);
}

template <typename CanvasT>
void drawMessageScreen(CanvasT &canvas, const char *title, const char *line1, const char *line2) {
  char scoreLine[32];
  canvas.setTextDatum(MC_DATUM);
  canvas.fillRect(0, 0, SCREEN_W, SCREEN_H, rgb565(14, 17, 24));
  canvas.fillRoundRect(48, 58, SCREEN_W - 96, SCREEN_H - 116, 16, rgb565(31, 34, 43));
  canvas.drawRoundRect(48, 58, SCREEN_W - 96, SCREEN_H - 116, 16, accentColor());
  canvas.setTextColor(accentColor(), rgb565(31, 34, 43));
  canvas.drawCentreString(title, SCREEN_W / 2, 90, 4);
  canvas.setTextColor(TFT_WHITE, rgb565(31, 34, 43));
  canvas.drawCentreString(line1, SCREEN_W / 2, 148, 2);
  canvas.drawCentreString(line2, SCREEN_W / 2, 176, 2);
  snprintf(scoreLine, sizeof(scoreLine), "Score: %d", score);
  canvas.drawCentreString(scoreLine, SCREEN_W / 2, 216, 2);
  canvas.setTextDatum(TL_DATUM);
}

template <typename CanvasT>
void drawGameOverRanking(CanvasT &canvas) {
  uint16_t bg = rgb565(16, 18, 30);
  uint16_t rowBg = rgb565(48, 54, 76);
  uint16_t title = rgb565(246, 212, 90);
  char buf[48];

  canvas.fillRect(0, 0, SCREEN_W, SCREEN_H, rgb565(10, 12, 18));
  canvas.fillRoundRect(52, 22, SCREEN_W - 104, SCREEN_H - 44, 16, bg);
  canvas.drawRoundRect(52, 22, SCREEN_W - 104, SCREEN_H - 44, 16, rgb565(232, 84, 84));
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(title, bg);
  canvas.drawString("SCORE RANKING", SCREEN_W / 2, 50, 4);
  canvas.setTextColor(TFT_WHITE, bg);
  snprintf(buf, sizeof(buf), "This Run %d", score);
  canvas.drawString(buf, SCREEN_W / 2, 82, 2);

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    int y = 112 + i * 28;
    uint16_t fillColor = (i == latestRank) ? rgb565(84, 66, 26) : rowBg;
    uint16_t textColor = (i == latestRank) ? rgb565(250, 224, 120) : TFT_WHITE;
    canvas.fillRoundRect(88, y - 8, 304, 24, 6, fillColor);
    canvas.setTextColor(textColor, fillColor);
    snprintf(buf, sizeof(buf), "%d. %d pts", i + 1, leaderboard[i]);
    canvas.drawString(buf, SCREEN_W / 2 - 28, y, 2);
    if (i == latestRank) {
      canvas.fillTriangle(346, y + 8, 362, y, 362, y + 16, rgb565(250, 210, 92));
      canvas.fillCircle(332, y + 8, 6, rgb565(250, 210, 92));
    }
  }

  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawString("Blue Retry", 146, 286, 2);
  canvas.drawString("Green Title", 334, 286, 2);
  canvas.setTextDatum(TL_DATUM);
}

void renderTitle() {
  if (frameBufferReady) {
    drawTitleScreen(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawTitleScreen(tft);
  }
}

void renderPlaying() {
  if (frameBufferReady) {
    drawScene(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawScene(tft);
  }
}

void renderLevelClear() {
  char line1[64];
  snprintf(line1, sizeof(line1), "Reached %d / %d", score, targetScore);
  if (frameBufferReady) {
    drawMessageScreen(frameBuffer, "LEVEL CLEAR", line1, "Press green for next level");
    frameBuffer.pushSprite(0, 0);
  } else {
    drawMessageScreen(tft, "LEVEL CLEAR", line1, "Press green for next level");
  }
}

void renderGameOver() {
  if (frameBufferReady) {
    drawGameOverRanking(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawGameOverRanking(tft);
  }
}

void updatePlaying() {
  unsigned long now = millis();
  updateExplosions();
  updateScorePopups();
  if (greenPressed) {
    detonateHeldItem();
  }
  if (now - lastSecondTickMs >= 1000) {
    int elapsedSeconds = (now - lastSecondTickMs) / 1000;
    lastSecondTickMs += elapsedSeconds * 1000UL;
    timeLeft -= elapsedSeconds;
    if (timeLeft > 0 && timeLeft <= 10 && timeLeft != lastCountdownCue) {
      lastCountdownCue = timeLeft;
      playSfxCountdown();
    }
    if (timeLeft <= 0) {
      timeLeft = 0;
      if (score >= targetScore) {
        playSfxClear();
        gameState = LEVEL_CLEAR;
        stateStartedMs = now;
      } else {
        playSfxGameOver();
        pulseMotor(120);
        recordScore();
        gameState = GAME_OVER;
        stateStartedMs = now;
      }
      digitalWrite(green_led_pin, HIGH);
      digitalWrite(blue_led_pin, HIGH);
      return;
    }
  }

  float dtScale = (now - lastUpdateMs) / 16.0f;
  if (dtScale < 0.6f) {
    dtScale = 0.6f;
  } else if (dtScale > 2.2f) {
    dtScale = 2.2f;
  }
  lastUpdateMs = now;

  updateHook(dtScale);
}

void setup() {
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  initFrameBuffer();
  setupHardware();
  renderTitle();
  lastRenderedState = TITLE;
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
  readInputs();

  if (gameState == TITLE) {
    if (bluePressed || greenPressed) {
      startNewGame();
    }
  } else if (gameState == PLAYING) {
    updatePlaying();
  } else if (gameState == LEVEL_CLEAR) {
    if (greenPressed || bluePressed) {
      levelIndex++;
      beginLevel();
    }
  } else if (gameState == GAME_OVER) {
    if (bluePressed) {
      startNewGame();
    } else if (greenPressed) {
      gameState = TITLE;
    }
  }

  if (gameState != lastRenderedState) {
    lastRenderedState = gameState;
  }

  if (gameState == TITLE) {
    renderTitle();
  } else if (gameState == PLAYING) {
    renderPlaying();
  } else if (gameState == LEVEL_CLEAR) {
    renderLevelClear();
  } else {
    renderGameOver();
  }

  delay(16);
}
