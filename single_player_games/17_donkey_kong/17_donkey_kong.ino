#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int FRAME_W = SCREEN_W / 2;

const int JOY_X_PIN = 1;
const int JOY_Y_PIN = 2;
const int GREEN_BTN_PIN = 11;
const int BLUE_BTN_PIN = 12;
const int REVERSE_PIN = 45;
const int FORWARD_PIN = 46;
const int MOTOR_FREQUENCY = 15000;
const int REVERSE_CH = 0;
const int FORWARD_CH = 1;
const int MOTOR_RESOLUTION = 8;
const int AUDIO_LRC_PIN = 13;
const int AUDIO_BCLK_PIN = 14;
const int AUDIO_DIN_PIN = 4;

const int FLOOR_COUNT = 4;
const int LADDER_COUNT = 6;
const int MAX_BARRELS = 14;
const int LEADERBOARD_SIZE = 5;
const float WORLD_Y_OFFSET = 18.0f;

const float PLATFORM_X_MIN = 24.0f;
const float PLATFORM_X_MAX = 456.0f;

const float PLAYER_W = 16.0f;
const float PLAYER_H = 20.0f;
const float PLAYER_MOVE_SPEED = 2.5f;
const float PLAYER_CLIMB_SPEED = 1.9f;
const float PLAYER_JUMP_SPEED = 4.6f;
const float GRAVITY = 0.30f;

const float BARREL_RADIUS = 6.0f;
const float BARREL_SPEED_BASE = 1.30f;

const uint32_t FRAME_MS = 16;
const int SAMPLE_RATE = 16000;
const int AUDIO_CHUNK_SAMPLES = 128;
const int SFX_VOICE_COUNT = 4;

struct Platform {
  float yBase;
  float slope;
};

struct Ladder {
  int fromFloor;
  float x;
};

struct Barrel {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  int floor;
  bool falling;
  bool scoredJump;
};

struct Player {
  float x;
  float y;
  float vy;
  bool onGround;
  bool onLadder;
  int ladderIndex;
  int floor;
  uint32_t invulnerableUntil;
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

enum GameState {
  STATE_TITLE,
  STATE_PLAYING,
  STATE_KONG_FALL,
  STATE_GAME_OVER,
  STATE_LEADERBOARD
};

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBufferLeft = TFT_eSprite(&tft);
TFT_eSprite frameBufferRight = TFT_eSprite(&tft);

struct WindowCanvas {
  TFT_eSprite *sprite;
  int xOffset;

  void fillScreen(uint16_t color) { sprite->fillScreen(color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) { sprite->fillRect(x - xOffset, y, w, h, color); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) { sprite->drawLine(x0 - xOffset, y0, x1 - xOffset, y1, color); }
  void drawFastHLine(int x, int y, int w, uint16_t color) { sprite->drawFastHLine(x - xOffset, y, w, color); }
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
  int drawCentreString(const char *text, int x, int y, int font) { return sprite->drawCentreString(text, x - xOffset, y, font); }
};

Platform platforms[FLOOR_COUNT];
Ladder ladders[LADDER_COUNT];
Barrel barrels[MAX_BARRELS];
Player player;

GameState gameState = STATE_TITLE;

int score = 0;
int levelNo = 1;
int lives = 3;
int leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int latestRank = -1;

bool frameBufferReady = false;

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
int joyXRaw = 2048;
int joyYRaw = 2048;
bool playerFacingRight = true;
bool vibrating = false;
bool vibForward = true;
uint32_t vibEndMs = 0;
uint32_t vibLastToggleMs = 0;
bool audioReady = false;
Voice sfxVoices[SFX_VOICE_COUNT];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t audioTaskHandle = nullptr;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
float bgmPhase = 0.0f;
int bgmIndex = 0;
int bgmRemainSamples = 0;
int bgmMode = 0;
float kongFallX = 0.0f;
float kongFallY = 0.0f;
float kongFallVy = 0.0f;
float kongFallTargetY = 0.0f;
bool kongFallLanded = false;
uint32_t kongFallLandedMs = 0;

uint32_t lastStepMs = 0;
uint32_t lastSpawnMs = 0;
uint32_t stateStartMs = 0;

const Note bgmLoop[] = {
  {262, 180}, {330, 180}, {392, 180}, {330, 180},
  {262, 180}, {330, 180}, {440, 260}, {0, 70},
  {294, 180}, {349, 180}, {392, 180}, {349, 180},
  {294, 180}, {262, 180}, {220, 260}, {0, 70}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

const Note funnyBgmLoop[] = {
  {196, 90}, {0, 35}, {233, 90}, {0, 35},
  {175, 90}, {196, 90}, {147, 120}, {0, 40},
  {196, 90}, {262, 90}, {330, 120}, {0, 45},
  {220, 90}, {175, 90}, {147, 140}, {0, 60}
};
const int FUNNY_BGM_NOTE_COUNT = sizeof(funnyBgmLoop) / sizeof(funnyBgmLoop[0]);

const i2s_config_t i2sOutConfig = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 256
};

const i2s_pin_config_t i2sOutPinConfig = {
  .bck_io_num = AUDIO_BCLK_PIN,
  .ws_io_num = AUDIO_LRC_PIN,
  .data_out_num = AUDIO_DIN_PIN,
  .data_in_num = -1
};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void stopMotor() {
  ledcWrite(FORWARD_CH, 0);
  ledcWrite(REVERSE_CH, 0);
}

void startVibration(uint16_t durationMs = 170) {
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

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 24) {
    vibLastToggleMs = now;
    vibForward = !vibForward;
    const int duty = 190;
    if (vibForward) {
      ledcWrite(FORWARD_CH, duty);
      ledcWrite(REVERSE_CH, 0);
    } else {
      ledcWrite(FORWARD_CH, 0);
      ledcWrite(REVERSE_CH, duty);
    }
  }
}

void assignVoice(Voice &v, float freq, int durationSamples, float volume, uint8_t wave) {
  v.active = true;
  v.freq = freq;
  v.phase = 0.0f;
  v.remainSamples = durationSamples;
  v.volume = volume;
  v.wave = wave;
}

void startVoice(float freq, int ms, float volume, uint8_t wave, int preferred = -1) {
  if (!audioReady) return;
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  taskENTER_CRITICAL(&audioMux);
  if (preferred >= 0 && preferred < SFX_VOICE_COUNT) {
    assignVoice(sfxVoices[preferred], freq, durationSamples, volume, wave);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }
  for (int i = 0; i < SFX_VOICE_COUNT; ++i) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }
  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxJump() {
  startVoice(720.0f, 60, 0.055f, 0);
  startVoice(980.0f, 45, 0.045f, 1, 1);
}

void playSfxHit() {
  startVoice(160.0f, 140, 0.12f, 2);
  startVoice(110.0f, 160, 0.10f, 2, 1);
}

void playSfxWin() {
  startVoice(523.0f, 90, 0.10f, 1);
  startVoice(659.0f, 90, 0.10f, 1, 1);
  startVoice(784.0f, 140, 0.10f, 1, 2);
}

void playSfxKongLand() {
  startVoice(90.0f, 180, 0.12f, 2);
  startVoice(65.0f, 210, 0.10f, 2, 1);
}

float waveSample(Voice &v) {
  float s = 0.0f;
  if (v.wave == 0) {
    s = (v.phase < 0.5f) ? 1.0f : -1.0f;
  } else if (v.wave == 1) {
    s = sinf(v.phase * 2.0f * PI);
  } else {
    s = (float)random(-1000, 1000) / 1000.0f;
  }
  v.phase += v.freq / SAMPLE_RATE;
  if (v.phase >= 1.0f) v.phase -= 1.0f;
  return s * v.volume;
}

float bgmSample() {
  if (gameState != STATE_TITLE && gameState != STATE_PLAYING && gameState != STATE_KONG_FALL) {
    return 0.0f;
  }
  const Note *loop = (bgmMode == 1) ? funnyBgmLoop : bgmLoop;
  int noteCount = (bgmMode == 1) ? FUNNY_BGM_NOTE_COUNT : BGM_NOTE_COUNT;
  if (bgmRemainSamples <= 0) {
    bgmIndex = (bgmIndex + 1) % noteCount;
    bgmRemainSamples = (SAMPLE_RATE * loop[bgmIndex].ms) / 1000;
  }
  uint16_t freq = loop[bgmIndex].freq;
  bgmRemainSamples--;
  if (freq == 0) return 0.0f;
  float sample = (bgmPhase < 0.5f) ? 0.0055f : -0.0055f;
  bgmPhase += (float)freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) bgmPhase -= 1.0f;
  return sample;
}

void setBgmMode(int mode) {
  if (mode == bgmMode) return;
  bgmMode = mode;
  bgmIndex = 0;
  bgmPhase = 0.0f;
  if (bgmMode == 1) {
    bgmRemainSamples = (SAMPLE_RATE * funnyBgmLoop[0].ms) / 1000;
  } else {
    bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  }
}

void audioTick() {
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    float mix = bgmSample();
    taskENTER_CRITICAL(&audioMux);
    for (int v = 0; v < SFX_VOICE_COUNT; ++v) {
      if (!sfxVoices[v].active) continue;
      mix += waveSample(sfxVoices[v]);
      sfxVoices[v].remainSamples--;
      if (sfxVoices[v].remainSamples <= 0) {
        sfxVoices[v].active = false;
      }
    }
    taskEXIT_CRITICAL(&audioMux);
    mix = clampf(mix, -0.70f, 0.70f);
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

void initAudio() {
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2sOutConfig, 0, nullptr);
  if (err != ESP_OK) return;
  err = i2s_set_pin(I2S_NUM_1, &i2sOutPinConfig);
  if (err != ESP_OK) return;
  i2s_zero_dma_buffer(I2S_NUM_1);
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  audioReady = true;
  xTaskCreatePinnedToCore(audioTaskMain, "audio_task", 4096, nullptr, 2, &audioTaskHandle, 0);
}

float joyAxisFromRaw(int raw) {
  float n = ((float)raw - 2048.0f) / 2048.0f;
  if (fabsf(n) < 0.16f) return 0.0f;
  return clampf(n, -1.0f, 1.0f);
}

float platformY(int floorIndex, float x) {
  float px = clampf(x, PLATFORM_X_MIN, PLATFORM_X_MAX);
  return platforms[floorIndex].yBase + platforms[floorIndex].slope * (px - PLATFORM_X_MIN);
}

float barrelDownhillDir(int floorIndex) {
  return (platforms[floorIndex].slope >= 0.0f) ? 1.0f : -1.0f;
}

float topPeakX() {
  return (platforms[0].slope >= 0.0f) ? (PLATFORM_X_MIN + 22.0f) : (PLATFORM_X_MAX - 22.0f);
}

float goalX() {
  float dir = barrelDownhillDir(0);
  return clampf(topPeakX() + dir * 26.0f, PLATFORM_X_MIN + 10.0f, PLATFORM_X_MAX - 10.0f);
}

float ladderTopY(const Ladder &l) {
  return platformY(l.fromFloor, l.x) - PLAYER_H;
}

float ladderBottomY(const Ladder &l) {
  return platformY(l.fromFloor + 1, l.x) - PLAYER_H;
}

void clearBarrels() {
  for (int i = 0; i < MAX_BARRELS; ++i) {
    barrels[i].active = false;
  }
}

void resetPlayerToStart() {
  player.x = PLATFORM_X_MIN + 14.0f;
  player.y = platformY(FLOOR_COUNT - 1, player.x) - PLAYER_H;
  player.vy = 0.0f;
  player.onGround = true;
  player.onLadder = false;
  player.ladderIndex = -1;
  player.floor = FLOOR_COUNT - 1;
}

void setupMap() {
  platforms[0] = {62.0f + WORLD_Y_OFFSET, 0.055f};
  platforms[1] = {126.0f + WORLD_Y_OFFSET, -0.050f};
  platforms[2] = {188.0f + WORLD_Y_OFFSET, 0.045f};
  platforms[3] = {252.0f + WORLD_Y_OFFSET, -0.040f};

  ladders[0] = {0, 116.0f};
  ladders[1] = {0, 336.0f};
  ladders[2] = {1, 198.0f};
  ladders[3] = {1, 402.0f};
  ladders[4] = {2, 82.0f};
  ladders[5] = {2, 292.0f};
}

void beginGame() {
  score = 0;
  levelNo = 1;
  lives = 3;
  latestRank = -1;
  clearBarrels();
  resetPlayerToStart();
  gameState = STATE_PLAYING;
  lastStepMs = millis();
  lastSpawnMs = millis();
}

void beginNextLevel() {
  setBgmMode(0);
  levelNo++;
  score += 200;
  clearBarrels();
  resetPlayerToStart();
  lastSpawnMs = millis();
}

void startKongFallSequence() {
  playSfxWin();
  clearBarrels();
  player.x = goalX() - PLAYER_W * 0.5f;
  player.y = platformY(0, goalX()) - PLAYER_H;
  player.vy = 0.0f;
  player.onGround = true;
  player.onLadder = false;
  player.floor = 0;

  kongFallX = topPeakX();
  kongFallY = platformY(0, kongFallX) - 28.0f;
  kongFallTargetY = platformY(FLOOR_COUNT - 1, kongFallX) - 28.0f;
  kongFallVy = 0.0f;
  kongFallLanded = false;
  kongFallLandedMs = 0;
  setBgmMode(1);
  gameState = STATE_KONG_FALL;
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

void enterGameOver() {
  submitScoreToLeaderboard();
  gameState = STATE_GAME_OVER;
  stateStartMs = millis();
}

void initFrameBuffer() {
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

void readInput() {
  bool newGreen = digitalRead(GREEN_BTN_PIN) == LOW;
  bool newBlue = digitalRead(BLUE_BTN_PIN) == LOW;
  greenPressed = newGreen && !greenDown;
  bluePressed = newBlue && !blueDown;
  greenDown = newGreen;
  blueDown = newBlue;
  joyXRaw = analogRead(JOY_X_PIN);
  joyYRaw = analogRead(JOY_Y_PIN);
}

int findClimbableLadder(float px, float py) {
  float feetY = py + PLAYER_H;
  for (int i = 0; i < LADDER_COUNT; ++i) {
    const Ladder &l = ladders[i];
    if (fabsf(px - l.x) > 10.0f) continue;
    float top = ladderTopY(l);
    float bottom = ladderBottomY(l);
    if (feetY >= top - 6.0f && feetY <= bottom + PLAYER_H + 6.0f) {
      return i;
    }
  }
  return -1;
}

void spawnBarrel() {
  for (int i = 0; i < MAX_BARRELS; ++i) {
    if (barrels[i].active) continue;
    barrels[i].active = true;
    barrels[i].floor = 0;
    float dir = barrelDownhillDir(0);
    barrels[i].x = (dir > 0.0f) ? (PLATFORM_X_MIN + 8.0f) : (PLATFORM_X_MAX - 8.0f);
    barrels[i].y = platformY(0, barrels[i].x) - BARREL_RADIUS;
    barrels[i].falling = false;
    barrels[i].vy = 0.0f;
    barrels[i].vx = dir * (BARREL_SPEED_BASE + levelNo * 0.16f);
    barrels[i].scoredJump = false;
    return;
  }
}

void hurtPlayer(bool fromBarrel) {
  uint32_t now = millis();
  if (now < player.invulnerableUntil) return;

  if (fromBarrel) {
    playSfxHit();
    startVibration(220);
  }

  lives--;
  player.invulnerableUntil = now + 1400;

  if (lives <= 0) {
    enterGameOver();
    return;
  }

  resetPlayerToStart();
}

void updatePlayer() {
  float xAxis = joyAxisFromRaw(joyXRaw);
  float yAxis = joyAxisFromRaw(joyYRaw);

  int ladderIdx = findClimbableLadder(player.x, player.y);

  if (!player.onLadder && fabsf(yAxis) > 0.35f && ladderIdx >= 0) {
    player.onLadder = true;
    player.onGround = false;
    player.ladderIndex = ladderIdx;
    player.vy = 0.0f;
    player.x = ladders[ladderIdx].x;
  }

  if (player.onLadder) {
    const Ladder &l = ladders[player.ladderIndex];
    float top = ladderTopY(l);
    float bottom = ladderBottomY(l);

    player.x = l.x;
    player.y += yAxis * PLAYER_CLIMB_SPEED;
    player.y = clampf(player.y, top, bottom);

    if (player.y <= top + 0.5f && yAxis <= 0.0f) {
      player.onLadder = false;
      player.onGround = true;
      player.floor = l.fromFloor;
      player.y = top;
    } else if (player.y >= bottom - 0.5f && yAxis >= 0.0f) {
      player.onLadder = false;
      player.onGround = true;
      player.floor = l.fromFloor + 1;
      player.y = bottom;
    }

    if (bluePressed) {
      playSfxJump();
      player.onLadder = false;
      player.vy = -PLAYER_JUMP_SPEED * 0.7f;
    }
    return;
  }

  player.x += xAxis * PLAYER_MOVE_SPEED;
  player.x = clampf(player.x, PLATFORM_X_MIN, PLATFORM_X_MAX);

  if (bluePressed && player.onGround) {
    playSfxJump();
    player.vy = -PLAYER_JUMP_SPEED;
    player.onGround = false;
  }

  player.vy += GRAVITY;
  if (player.vy > 6.0f) player.vy = 6.0f;
  player.y += player.vy;

  player.onGround = false;

  float feetY = player.y + PLAYER_H;
  for (int f = 0; f < FLOOR_COUNT; ++f) {
    float py = platformY(f, player.x);
    if (feetY >= py - 4.0f && feetY <= py + 10.0f && player.vy >= 0.0f) {
      player.y = py - PLAYER_H;
      player.vy = 0.0f;
      player.onGround = true;
      player.floor = f;
      break;
    }
  }

  if (player.y > SCREEN_H + 20.0f) {
    hurtPlayer(false);
  }
}

void updateBarrels() {
  uint32_t now = millis();
  uint32_t spawnInterval = (uint32_t)max(260, 900 - levelNo * 75);
  if (now - lastSpawnMs >= spawnInterval) {
    lastSpawnMs = now;
    spawnBarrel();
  }

  for (int i = 0; i < MAX_BARRELS; ++i) {
    Barrel &b = barrels[i];
    if (!b.active) continue;

    if (b.falling) {
      b.vy += 0.22f;
      b.y += b.vy;

      int targetFloor = b.floor + 1;
      if (targetFloor >= FLOOR_COUNT) {
        b.active = false;
        continue;
      }

      float landY = platformY(targetFloor, b.x) - BARREL_RADIUS;
      if (b.y >= landY) {
        b.floor = targetFloor;
        b.falling = false;
        b.vy = 0.0f;
        b.vx = barrelDownhillDir(b.floor) * (BARREL_SPEED_BASE + levelNo * 0.16f);
        b.y = landY;
        b.scoredJump = false;
      }
    } else {
      b.x += b.vx;
      b.y = platformY(b.floor, b.x) - BARREL_RADIUS;

      bool atEdge = (b.x <= PLATFORM_X_MIN + 3.0f || b.x >= PLATFORM_X_MAX - 3.0f);
      if (atEdge) {
        if (b.floor >= FLOOR_COUNT - 1) {
          b.active = false;
          continue;
        }
        b.falling = true;
        b.vy = 0.2f;
      } else if (b.floor < FLOOR_COUNT - 1) {
        for (int k = 0; k < LADDER_COUNT; ++k) {
          if (ladders[k].fromFloor != b.floor) continue;
          if (fabsf(b.x - ladders[k].x) < 2.8f) {
            int dropChance = 18 + levelNo * 3;
            if (random(0, 1000) < dropChance) {
              b.falling = true;
              b.vy = 0.2f;
              break;
            }
          }
        }
      }
    }

    float dx = (player.x + PLAYER_W * 0.5f) - b.x;
    float dy = (player.y + PLAYER_H * 0.5f) - b.y;
    float hitR = 9.0f + BARREL_RADIUS;
    if (dx * dx + dy * dy <= hitR * hitR) {
      hurtPlayer(true);
    }

    if (!b.falling && !b.scoredJump) {
      float playerFeet = player.y + PLAYER_H;
      if (fabsf((player.x + PLAYER_W * 0.5f) - b.x) < 13.0f && playerFeet < b.y - 7.0f && player.vy > 0.6f) {
        score += 15;
        b.scoredJump = true;
      }
    }
  }
}

void checkGoal() {
  float gx = goalX();
  if (player.floor == 0 && fabsf(player.x - gx) < 16.0f && player.onGround) {
    score += 120;
    startKongFallSequence();
  }
}

void updateKongFall() {
  if (!kongFallLanded) {
    kongFallVy += 0.34f;
    if (kongFallVy > 7.5f) kongFallVy = 7.5f;
    kongFallY += kongFallVy;
    if (kongFallY >= kongFallTargetY) {
      kongFallY = kongFallTargetY;
      kongFallLanded = true;
      kongFallLandedMs = millis();
      playSfxKongLand();
      startVibration(320);
    }
    return;
  }

  if (millis() - kongFallLandedMs >= 1250) {
    beginNextLevel();
    gameState = STATE_PLAYING;
  }
}

template <typename CanvasT>
void drawKong(CanvasT &canvas, int x, int y, bool frameB) {
  uint16_t fur = rgb565(112, 66, 34);
  uint16_t furDark = rgb565(72, 40, 22);
  uint16_t skin = rgb565(238, 196, 142);

  canvas.fillRoundRect(x - 12, y + 6, 24, 14, 4, fur);
  canvas.fillRoundRect(x - 9, y, 18, 10, 3, fur);
  canvas.fillRect(x - 6, y + 4, 12, 5, skin);
  canvas.fillRect(x - 8, y + 10, 2, 8, furDark);
  canvas.fillRect(x + 6, y + 10, 2, 8, furDark);

  if (frameB) {
    canvas.fillRect(x - 17, y + 8, 5, 9, fur);
    canvas.fillRect(x + 12, y + 4, 5, 9, fur);
  } else {
    canvas.fillRect(x - 17, y + 4, 5, 9, fur);
    canvas.fillRect(x + 12, y + 8, 5, 9, fur);
  }

  canvas.fillRect(x - 8, y + 20, 5, 6, fur);
  canvas.fillRect(x + 3, y + 20, 5, 6, fur);
  canvas.fillCircle(x - 3, y + 5, 1, TFT_BLACK);
  canvas.fillCircle(x + 3, y + 5, 1, TFT_BLACK);
  canvas.drawLine(x - 2, y + 7, x + 2, y + 7, furDark);
}

template <typename CanvasT>
void drawKongGround(CanvasT &canvas, int x, int y, bool frameB) {
  uint16_t fur = rgb565(112, 66, 34);
  uint16_t furDark = rgb565(72, 40, 22);
  uint16_t skin = rgb565(238, 196, 142);
  int wobble = frameB ? 1 : -1;

  canvas.fillRoundRect(x - 14, y + 10 + wobble, 28, 11, 5, fur);
  canvas.fillRoundRect(x - 10, y + 6 + wobble, 20, 8, 3, fur);
  canvas.fillRect(x - 7, y + 8 + wobble, 14, 4, skin);
  canvas.fillRect(x - 10, y + 14 + wobble, 4, 6, furDark);
  canvas.fillRect(x + 6, y + 14 + wobble, 4, 6, furDark);
  canvas.fillCircle(x - 3, y + 9 + wobble, 1, TFT_BLACK);
  canvas.fillCircle(x + 3, y + 9 + wobble, 1, TFT_BLACK);
  canvas.drawLine(x - 3, y + 11 + wobble, x + 3, y + 12 + wobble, furDark);
}

template <typename CanvasT>
void drawKid(CanvasT &canvas, int px, int py, bool facingRight, bool runFrameB, bool climbing, bool climbFrameB) {
  uint16_t skin = rgb565(250, 224, 178);
  uint16_t hair = rgb565(62, 36, 16);
  uint16_t shirt = rgb565(84, 170, 255);
  uint16_t shirtDark = rgb565(42, 120, 205);
  uint16_t shorts = rgb565(38, 86, 196);
  uint16_t shoe = rgb565(210, 210, 220);

  int dir = facingRight ? 1 : -1;
  int headX = px + 8;

  canvas.fillRect(headX - 4, py + 1, 8, 6, skin);
  canvas.fillRect(headX - 4, py, 8, 2, hair);
  canvas.fillRect(headX - 5, py + 2, 1, 2, hair);
  canvas.fillRect(headX + 4, py + 2, 1, 2, hair);
  canvas.fillRect(headX + dir * 2, py + 3, 1, 1, TFT_BLACK);

  canvas.fillRect(px + 4, py + 7, 8, 7, shirt);
  canvas.fillRect(px + 4, py + 14, 8, 4, shorts);
  canvas.drawLine(px + 4, py + 10, px + 11, py + 10, shirtDark);

  if (climbing) {
    if (climbFrameB) {
      canvas.fillRect(px + 2, py + 8, 2, 4, skin);
      canvas.fillRect(px + 12, py + 12, 2, 4, skin);
      canvas.fillRect(px + 5, py + 18, 2, 3, shoe);
      canvas.fillRect(px + 9, py + 15, 2, 3, shoe);
    } else {
      canvas.fillRect(px + 2, py + 12, 2, 4, skin);
      canvas.fillRect(px + 12, py + 8, 2, 4, skin);
      canvas.fillRect(px + 5, py + 15, 2, 3, shoe);
      canvas.fillRect(px + 9, py + 18, 2, 3, shoe);
    }
  } else {
    if (runFrameB) {
      canvas.fillRect(px + 2, py + 10, 2, 4, skin);
      canvas.fillRect(px + 12, py + 8, 2, 4, skin);
      canvas.fillRect(px + 4, py + 18, 2, 3, shoe);
      canvas.fillRect(px + 10, py + 16, 2, 3, shoe);
    } else {
      canvas.fillRect(px + 2, py + 8, 2, 4, skin);
      canvas.fillRect(px + 12, py + 10, 2, 4, skin);
      canvas.fillRect(px + 4, py + 16, 2, 3, shoe);
      canvas.fillRect(px + 10, py + 18, 2, 3, shoe);
    }
  }
}

template <typename CanvasT>
void drawMap(CanvasT &canvas, bool drawStaticKong = true) {
  canvas.fillScreen(rgb565(16, 12, 24));

  for (int y = 0; y < SCREEN_H; y += 24) {
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(22, 16, 34));
  }

  for (int f = 0; f < FLOOR_COUNT; ++f) {
    uint16_t railColor = rgb565(208, 62, 56);
    uint16_t beamColor = rgb565(140, 40, 34);
    for (int x = (int)PLATFORM_X_MIN; x < (int)PLATFORM_X_MAX; x += 10) {
      int y0 = (int)platformY(f, (float)x);
      int y1 = (int)platformY(f, (float)(x + 10));
      canvas.drawLine(x, y0, x + 10, y1, railColor);
      canvas.drawLine(x, y0 + 4, x + 10, y1 + 4, beamColor);
    }
  }

  for (int i = 0; i < LADDER_COUNT; ++i) {
    const Ladder &l = ladders[i];
    int x = (int)l.x;
    int topY = (int)(platformY(l.fromFloor, l.x) + 4.0f);
    int bottomY = (int)(platformY(l.fromFloor + 1, l.x));

    canvas.drawLine(x - 4, topY, x - 4, bottomY, rgb565(130, 206, 250));
    canvas.drawLine(x + 4, topY, x + 4, bottomY, rgb565(130, 206, 250));
    for (int ry = topY + 4; ry < bottomY; ry += 8) {
      canvas.drawLine(x - 4, ry, x + 4, ry, rgb565(170, 236, 255));
    }
  }

  float peakX = topPeakX();
  float dir = barrelDownhillDir(0);
  if (drawStaticKong) {
    int kongX = (int)peakX;
    int kongY = (int)(platformY(0, peakX) - 28.0f);
    bool kongFrameB = ((millis() / 260) % 2) != 0;
    drawKong(canvas, kongX, kongY, kongFrameB);
  }

  int flagX = (int)goalX();
  int flagY = (int)(platformY(0, flagX) - 24.0f);
  int flagTipX = (dir > 0.0f) ? (flagX + 16) : (flagX - 16);
  canvas.fillRect(flagX - 2, flagY, 3, 22, rgb565(230, 230, 80));
  canvas.fillTriangle(flagX + 1, flagY, flagTipX, flagY + 6, flagX + 1, flagY + 12, rgb565(255, 120, 140));
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.fillRect(0, 0, SCREEN_W, 28, rgb565(34, 28, 58));
  canvas.setTextColor(TFT_WHITE, rgb565(34, 28, 58));

  char line[96];
  snprintf(line, sizeof(line), "SCORE %d   LEVEL %d   LIFE %d", score, levelNo, lives);
  canvas.drawString(line, 10, 6, 2);
}

template <typename CanvasT>
void drawPlayer(CanvasT &canvas) {
  bool blink = (millis() < player.invulnerableUntil) && (((millis() / 90) % 2) == 0);
  if (blink) return;

  int px = (int)player.x;
  int py = (int)player.y;

  float xAxis = joyAxisFromRaw(joyXRaw);
  float yAxis = joyAxisFromRaw(joyYRaw);
  if (xAxis > 0.25f) playerFacingRight = true;
  if (xAxis < -0.25f) playerFacingRight = false;

  bool climbing = player.onLadder;
  bool runActive = (!climbing && player.onGround && fabsf(xAxis) > 0.2f);
  bool climbActive = (climbing && fabsf(yAxis) > 0.18f);
  bool runFrameB = runActive ? (((millis() / 130) % 2) != 0) : false;
  bool climbFrameB = climbActive ? (((millis() / 150) % 2) != 0) : false;

  drawKid(canvas, px, py, playerFacingRight, runFrameB, climbing, climbFrameB);
}

template <typename CanvasT>
void drawBarrels(CanvasT &canvas) {
  for (int i = 0; i < MAX_BARRELS; ++i) {
    if (!barrels[i].active) continue;
    int bx = (int)barrels[i].x;
    int by = (int)barrels[i].y;
    canvas.fillCircle(bx, by, (int)BARREL_RADIUS, rgb565(196, 122, 44));
    canvas.drawCircle(bx, by, (int)BARREL_RADIUS, rgb565(96, 46, 16));
    canvas.drawLine(bx - 4, by, bx + 4, by, rgb565(96, 46, 16));
  }
}

template <typename CanvasT>
void drawPlaying(CanvasT &canvas) {
  drawMap(canvas);
  drawBarrels(canvas);
  drawPlayer(canvas);
  drawHud(canvas);
}

template <typename CanvasT>
void drawKongFallScene(CanvasT &canvas) {
  drawMap(canvas, false);
  drawPlayer(canvas);
  drawHud(canvas);

  bool frameB = ((millis() / 120) % 2) != 0;
  int kx = (int)kongFallX;
  int ky = (int)kongFallY;
  if (kongFallLanded) {
    drawKongGround(canvas, kx, ky, frameB);
  } else {
    drawKong(canvas, kx, ky, frameB);
  }
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  canvas.fillScreen(rgb565(12, 10, 20));
  for (int i = 0; i < 42; ++i) {
    int x = random(0, SCREEN_W);
    int y = random(0, SCREEN_H);
    canvas.fillCircle(x, y, 1, rgb565(90, 90, 140));
  }

  canvas.fillRoundRect(58, 38, SCREEN_W - 116, 230, 16, rgb565(34, 24, 44));
  canvas.drawRoundRect(58, 38, SCREEN_W - 116, 230, 16, rgb565(164, 104, 90));

  canvas.setTextColor(rgb565(255, 196, 122), rgb565(34, 24, 44));
  canvas.drawCentreString("BARREL RESCUE", SCREEN_W / 2, 72, 4);

  canvas.setTextColor(TFT_WHITE, rgb565(34, 24, 44));
  canvas.drawCentreString("Donkey Kong style", SCREEN_W / 2, 108, 2);
  canvas.drawCentreString("Joystick: Move / Climb", SCREEN_W / 2, 142, 2);
  canvas.drawCentreString("Blue: Jump", SCREEN_W / 2, 166, 2);
  canvas.drawCentreString("Reach top-right flag", SCREEN_W / 2, 190, 2);

  const int barrelX = 366;
  const int barrelY = 202;
  canvas.fillCircle(barrelX + 4, barrelY + 18, 26, rgb565(28, 16, 20));
  canvas.fillRoundRect(barrelX - 30, barrelY - 22, 60, 44, 16, rgb565(112, 58, 28));
  canvas.fillRoundRect(barrelX - 24, barrelY - 18, 48, 36, 14, rgb565(160, 86, 42));
  canvas.fillRoundRect(barrelX - 30, barrelY - 12, 60, 6, 3, rgb565(74, 78, 92));
  canvas.fillRoundRect(barrelX - 30, barrelY + 7, 60, 6, 3, rgb565(74, 78, 92));
  canvas.drawFastHLine(barrelX - 22, barrelY - 2, 44, rgb565(214, 140, 84));
  canvas.drawFastHLine(barrelX - 18, barrelY + 2, 36, rgb565(196, 118, 66));
  canvas.fillCircle(barrelX - 10, barrelY - 6, 5, rgb565(228, 168, 120));
  canvas.fillCircle(barrelX - 13, barrelY - 9, 2, rgb565(250, 220, 176));

  uint16_t blinkColor = ((millis() / 420) % 2) ? rgb565(250, 244, 166) : rgb565(130, 124, 86);
  canvas.setTextColor(blinkColor, rgb565(34, 24, 44));
  canvas.drawCentreString("PRESS BLUE TO START", SCREEN_W / 2, 230, 2);
}

template <typename CanvasT>
void drawGameOver(CanvasT &canvas) {
  canvas.fillScreen(rgb565(20, 12, 14));
  canvas.setTextColor(rgb565(255, 116, 116), rgb565(20, 12, 14));
  canvas.drawCentreString("GAME OVER", SCREEN_W / 2, 112, 4);

  canvas.setTextColor(TFT_WHITE, rgb565(20, 12, 14));
  char buf[48];
  snprintf(buf, sizeof(buf), "SCORE  %d", score);
  canvas.drawCentreString(buf, SCREEN_W / 2, 162, 2);
  canvas.drawCentreString("Entering leaderboard...", SCREEN_W / 2, 188, 2);
}

template <typename CanvasT>
void drawLeaderboard(CanvasT &canvas) {
  uint16_t bg = rgb565(14, 18, 28);
  canvas.fillScreen(bg);

  canvas.fillRoundRect(54, 24, SCREEN_W - 108, SCREEN_H - 48, 16, rgb565(24, 32, 50));
  canvas.drawRoundRect(54, 24, SCREEN_W - 108, SCREEN_H - 48, 16, rgb565(124, 152, 220));

  canvas.setTextColor(rgb565(220, 236, 255), rgb565(24, 32, 50));
  canvas.drawCentreString("LEADERBOARD", SCREEN_W / 2, 42, 4);

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    bool isLatest = (i == latestRank);
    int y = 92 + i * 38;
    uint16_t rowBg = isLatest ? rgb565(84, 66, 24) : rgb565(36, 46, 72);
    uint16_t textColor = isLatest ? rgb565(255, 236, 156) : rgb565(210, 222, 255);

    canvas.fillRoundRect(92, y - 8, 296, 26, 6, rowBg);
    canvas.setTextColor(textColor, rowBg);

    char line[48];
    snprintf(line, sizeof(line), "%d. %d", i + 1, leaderboard[i]);
    canvas.drawString(line, 120, y, 2);

    if (isLatest) {
      canvas.drawString("< YOU", 300, y, 2);
    }
  }

  canvas.setTextColor(rgb565(166, 184, 220), rgb565(24, 32, 50));
  canvas.drawCentreString("Blue: Back to title", SCREEN_W / 2, SCREEN_H - 42, 2);
}

void renderFrame() {
  if (frameBufferReady) {
    WindowCanvas left = {&frameBufferLeft, 0};
    WindowCanvas right = {&frameBufferRight, FRAME_W};

    if (gameState == STATE_TITLE) {
      drawTitle(left);
      drawTitle(right);
    } else if (gameState == STATE_PLAYING) {
      drawPlaying(left);
      drawPlaying(right);
    } else if (gameState == STATE_KONG_FALL) {
      drawKongFallScene(left);
      drawKongFallScene(right);
    } else if (gameState == STATE_GAME_OVER) {
      drawGameOver(left);
      drawGameOver(right);
    } else {
      drawLeaderboard(left);
      drawLeaderboard(right);
    }
    frameBufferLeft.pushSprite(0, 0);
    frameBufferRight.pushSprite(FRAME_W, 0);
    return;
  }

  if (gameState == STATE_TITLE) {
    drawTitle(tft);
  } else if (gameState == STATE_PLAYING) {
    drawPlaying(tft);
  } else if (gameState == STATE_KONG_FALL) {
    drawKongFallScene(tft);
  } else if (gameState == STATE_GAME_OVER) {
    drawGameOver(tft);
  } else {
    drawLeaderboard(tft);
  }
}

void updatePlaying() {
  updatePlayer();
  updateBarrels();
  checkGoal();
}

void setup() {
  pinMode(GREEN_BTN_PIN, INPUT_PULLUP);
  pinMode(BLUE_BTN_PIN, INPUT_PULLUP);
  ledcSetup(REVERSE_CH, MOTOR_FREQUENCY, MOTOR_RESOLUTION);
  ledcSetup(FORWARD_CH, MOTOR_FREQUENCY, MOTOR_RESOLUTION);
  ledcAttachPin(REVERSE_PIN, REVERSE_CH);
  ledcAttachPin(FORWARD_PIN, FORWARD_CH);
  stopMotor();

  analogReadResolution(12);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.setSwapBytes(false);
  tft.fillScreen(TFT_BLACK);

  setupMap();
  initFrameBuffer();
  initAudio();

  randomSeed((uint32_t)micros() ^ (uint32_t)analogRead(JOY_X_PIN) ^ ((uint32_t)analogRead(JOY_Y_PIN) << 10));

  stateStartMs = millis();
}

void loop() {
  readInput();
  updateVibration();
  setBgmMode(gameState == STATE_KONG_FALL ? 1 : 0);

  if (gameState == STATE_TITLE) {
    if (bluePressed) {
      beginGame();
    }
  } else if (gameState == STATE_PLAYING) {
    uint32_t now = millis();
    int guard = 0;
    while ((now - lastStepMs) >= FRAME_MS && guard < 4) {
      lastStepMs += FRAME_MS;
      updatePlaying();
      guard++;
      if (gameState != STATE_PLAYING) {
        break;
      }
    }
  } else if (gameState == STATE_KONG_FALL) {
    uint32_t now = millis();
    int guard = 0;
    while ((now - lastStepMs) >= FRAME_MS && guard < 4) {
      lastStepMs += FRAME_MS;
      updateKongFall();
      guard++;
      if (gameState != STATE_KONG_FALL) {
        break;
      }
    }
  } else if (gameState == STATE_GAME_OVER) {
    if (millis() - stateStartMs >= 1200) {
      gameState = STATE_LEADERBOARD;
      stateStartMs = millis();
    }
  } else if (gameState == STATE_LEADERBOARD) {
    if (bluePressed) {
      gameState = STATE_TITLE;
      stateStartMs = millis();
    }
  }

  renderFrame();

  delay(1);
}
