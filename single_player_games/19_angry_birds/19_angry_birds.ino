#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
const int AUDIO_LRC_PIN = 13;
const int AUDIO_BCLK_PIN = 14;
const int AUDIO_DIN_PIN = 4;

const uint32_t FRAME_MS = 16;
const int GROUND_Y = 286;
const int LEADERBOARD_SIZE = 5;
const int MAX_BLOCKS = 8;
const int MAX_PIGS = 4;
const int MAX_PARTICLES = 96;

const float SLING_X = 78.0f;
const float SLING_Y = 230.0f;
const float BIRD_R = 8.0f;
const float GRAVITY = 0.33f;
const float DRAG = 0.999f;
const int INITIAL_BIRDS = 5;
const float MAX_PULL = 62.0f;
const float LAUNCH_SCALE = 0.18f;
const int SAMPLE_RATE = 16000;
const int AUDIO_CHUNK_SAMPLES = 128;

struct Block {
  float x;
  float y;
  float w;
  float h;
  int hp;
  int hpMax;
  bool alive;
};

struct Pig {
  float x;
  float y;
  float r;
  bool alive;
};

struct Bird {
  float x;
  float y;
  float vx;
  float vy;
  bool active;
  bool loaded;
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

struct Voice {
  bool active;
  float freq;
  float phase;
  int remainSamples;
  int totalSamples;
  float volume;
};

enum GameState {
  STATE_TITLE,
  STATE_PLAYING,
  STATE_GAME_OVER,
  STATE_LEADERBOARD
};

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBufferLeft = TFT_eSprite(&tft);
TFT_eSprite frameBufferRight = TFT_eSprite(&tft);
TAMC_GT911 tp(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_W, SCREEN_H);

struct WindowCanvas {
  TFT_eSprite *sprite;
  int xOffset;

  void fillScreen(uint16_t color) { sprite->fillScreen(color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) { sprite->fillRect(x - xOffset, y, w, h, color); }
  void drawRect(int x, int y, int w, int h, uint16_t color) { sprite->drawRect(x - xOffset, y, w, h, color); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) { sprite->drawLine(x0 - xOffset, y0, x1 - xOffset, y1, color); }
  void drawFastHLine(int x, int y, int w, uint16_t color) { sprite->drawFastHLine(x - xOffset, y, w, color); }
  void drawFastVLine(int x, int y, int h, uint16_t color) { sprite->drawFastVLine(x - xOffset, y, h, color); }
  void fillCircle(int x, int y, int r, uint16_t color) { sprite->fillCircle(x - xOffset, y, r, color); }
  void drawCircle(int x, int y, int r, uint16_t color) { sprite->drawCircle(x - xOffset, y, r, color); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) { sprite->fillRoundRect(x - xOffset, y, w, h, r, color); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) { sprite->drawRoundRect(x - xOffset, y, w, h, r, color); }
  void setTextColor(uint16_t fg, uint16_t bg) { sprite->setTextColor(fg, bg); }
  int drawString(const char *text, int x, int y, int font) { return sprite->drawString(text, x - xOffset, y, font); }
  int drawCentreString(const char *text, int x, int y, int font) { return sprite->drawCentreString(text, x - xOffset, y, font); }
};

GameState gameState = STATE_TITLE;

Block blocks[MAX_BLOCKS];
Pig pigs[MAX_PIGS];
Bird bird;
Particle particles[MAX_PARTICLES];
Voice pigHitVoice;

int score = 0;
int birdsRemaining = INITIAL_BIRDS;
bool gameWon = false;
uint32_t reloadAtMs = 0;

int leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int latestRank = -1;

bool frameBufferReady = false;

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
bool touchDown = false;
bool touchPressed = false;
bool touchReleased = false;
int touchX = 0;
int touchY = 0;
bool slingDragging = false;

bool vibrating = false;
bool vibForward = true;
uint32_t vibEndMs = 0;
uint32_t vibLastToggleMs = 0;

uint32_t lastStepMs = 0;
uint32_t stateStartMs = 0;
bool audioReady = false;
TaskHandle_t audioTaskHandle = nullptr;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];

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

void stopMotor() {
  ledcWrite(FORWARD_CH, 0);
  ledcWrite(REVERSE_CH, 0);
}

void playPigHitSfx() {
  pigHitVoice.active = true;
  pigHitVoice.freq = 920.0f;
  pigHitVoice.phase = 0.0f;
  pigHitVoice.totalSamples = SAMPLE_RATE / 10;
  pigHitVoice.remainSamples = pigHitVoice.totalSamples;
  pigHitVoice.volume = 0.58f;
}

void audioTick() {
  if (!audioReady) return;

  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    float s = 0.0f;
    if (pigHitVoice.active && pigHitVoice.remainSamples > 0) {
      float progress = 1.0f - (float)pigHitVoice.remainSamples / (float)pigHitVoice.totalSamples;
      float env = 1.0f - progress;
      if (env < 0.0f) env = 0.0f;
      float vib = sinf(pigHitVoice.phase) + 0.35f * sinf(pigHitVoice.phase * 2.0f);
      s += vib * pigHitVoice.volume * env;
      pigHitVoice.phase += 2.0f * 3.1415926f * pigHitVoice.freq / (float)SAMPLE_RATE;
      if (pigHitVoice.phase > 6.2831852f) pigHitVoice.phase -= 6.2831852f;
      pigHitVoice.remainSamples--;
      if (pigHitVoice.remainSamples <= 0) pigHitVoice.active = false;
    }

    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    audioBuffer[i] = (int16_t)(s * 32767.0f);
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_NUM_1, audioBuffer, sizeof(audioBuffer), &bytesWritten, 0);
}

void audioTaskMain(void *param) {
  (void)param;
  while (true) {
    audioTick();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void initAudio() {
  pigHitVoice = {false, 0.0f, 0.0f, 0, 0, 0.0f};
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2sOutConfig, 0, nullptr);
  if (err != ESP_OK) {
    audioReady = false;
    return;
  }
  err = i2s_set_pin(I2S_NUM_1, &i2sOutPinConfig);
  if (err != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_1);
    audioReady = false;
    return;
  }
  i2s_zero_dma_buffer(I2S_NUM_1);
  audioReady = true;
  xTaskCreatePinnedToCore(audioTaskMain, "audio_task", 4096, nullptr, 2, &audioTaskHandle, 0);
}

void startVibration(uint16_t durationMs = 110) {
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

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 22) {
    vibLastToggleMs = now;
    vibForward = !vibForward;
  }

  if (vibForward) {
    ledcWrite(FORWARD_CH, 170);
    ledcWrite(REVERSE_CH, 0);
  } else {
    ledcWrite(FORWARD_CH, 0);
    ledcWrite(REVERSE_CH, 170);
  }
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
  touchDown = newTouchDown;
  if (touchDown) {
    touchX = tp.points[0].x;
    touchY = tp.points[0].y;
  }
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

void setupLevel() {
  for (int i = 0; i < MAX_BLOCKS; ++i) {
    blocks[i].alive = false;
  }
  for (int i = 0; i < MAX_PIGS; ++i) {
    pigs[i].alive = false;
  }

  blocks[0] = {312, 252, 20, 34, 42, 42, true};
  blocks[1] = {350, 252, 20, 34, 42, 42, true};
  blocks[2] = {388, 252, 20, 34, 42, 42, true};
  blocks[3] = {330, 218, 20, 34, 38, 38, true};
  blocks[4] = {368, 218, 20, 34, 38, 38, true};
  blocks[5] = {330, 196, 58, 16, 32, 32, true};
  blocks[6] = {292, 270, 134, 12, 58, 58, true};

  pigs[0] = {340, 246, 12, true};
  pigs[1] = {379, 246, 12, true};
  pigs[2] = {359, 211, 12, true};

  bird.x = SLING_X;
  bird.y = SLING_Y;
  bird.vx = 0;
  bird.vy = 0;
  bird.active = false;
  bird.loaded = true;

  birdsRemaining = INITIAL_BIRDS;
  reloadAtMs = 0;
  slingDragging = false;
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    particles[i].active = false;
  }
}

void beginGame() {
  score = 0;
  gameWon = false;
  setupLevel();
  randomSeed((uint32_t)micros() ^ ((uint32_t)touchX << 4) ^ ((uint32_t)touchY << 13));
  gameState = STATE_PLAYING;
  lastStepMs = millis();
  stateStartMs = millis();
}

int pigsAliveCount() {
  int cnt = 0;
  for (int i = 0; i < MAX_PIGS; ++i) {
    if (pigs[i].alive) cnt++;
  }
  return cnt;
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

void enterGameOver(bool won) {
  gameWon = won;
  if (won) {
    score += birdsRemaining * 300;
    startVibration(280);
  }
  submitScoreToLeaderboard();
  gameState = STATE_GAME_OVER;
  stateStartMs = millis();
}

bool circleRectOverlap(float cx, float cy, float cr, const Block &b, float &nx, float &ny) {
  float closestX = clampf(cx, b.x, b.x + b.w);
  float closestY = clampf(cy, b.y, b.y + b.h);
  nx = cx - closestX;
  ny = cy - closestY;
  return (nx * nx + ny * ny) <= cr * cr;
}

void spawnBlockFragments(float x, float y, float w, float h) {
  int spawnCount = 16;
  float cx = x + w * 0.5f;
  float cy = y + h * 0.5f;
  for (int i = 0; i < MAX_PARTICLES && spawnCount > 0; ++i) {
    if (particles[i].active) continue;
    particles[i].active = true;
    particles[i].x = cx + (float)random(-8, 9);
    particles[i].y = cy + (float)random(-8, 9);
    particles[i].vx = (float)random(-170, 171) * 0.01f;
    particles[i].vy = (float)random(-220, -30) * 0.01f;
    particles[i].life = (uint8_t)random(18, 34);
    particles[i].maxLife = particles[i].life;
    particles[i].size = (uint8_t)random(2, 5);
    particles[i].color = rgb565((uint8_t)random(150, 195), (uint8_t)random(98, 148), (uint8_t)random(48, 96));
    spawnCount--;
  }
}

void updateParticles() {
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    if (!particles[i].active) continue;
    particles[i].x += particles[i].vx;
    particles[i].y += particles[i].vy;
    particles[i].vy += 0.18f;
    particles[i].vx *= 0.985f;
    if (particles[i].y >= GROUND_Y - 2) {
      particles[i].y = GROUND_Y - 2;
      particles[i].vy *= -0.18f;
      particles[i].vx *= 0.75f;
    }

    if (particles[i].life > 0) particles[i].life--;
    if (particles[i].life == 0) particles[i].active = false;
  }
}

void resolveBirdWorldBounds() {
  if (bird.y + BIRD_R >= GROUND_Y) {
    bird.y = GROUND_Y - BIRD_R;
    if (fabsf(bird.vy) > 1.1f) {
      bird.vy *= -0.35f;
      bird.vx *= 0.88f;
    } else {
      bird.vy = 0.0f;
      bird.vx *= 0.80f;
    }
  }

  if (bird.x - BIRD_R < 0.0f) {
    bird.x = BIRD_R;
    bird.vx *= -0.25f;
  }
  if (bird.x + BIRD_R > SCREEN_W) {
    bird.x = SCREEN_W - BIRD_R;
    bird.vx *= -0.25f;
  }
}

void updateBirdFlight() {
  if (!bird.active) return;

  bird.vy += GRAVITY;
  bird.vx *= DRAG;
  bird.vy *= DRAG;
  bird.x += bird.vx;
  bird.y += bird.vy;

  resolveBirdWorldBounds();

  for (int i = 0; i < MAX_BLOCKS; ++i) {
    if (!blocks[i].alive) continue;

    float nx = 0.0f;
    float ny = 0.0f;
    if (!circleRectOverlap(bird.x, bird.y, BIRD_R, blocks[i], nx, ny)) continue;

    float speed = sqrtf(bird.vx * bird.vx + bird.vy * bird.vy);
    int damage = (int)(speed * 8.5f);
    if (damage < 1) damage = 1;

    blocks[i].hp -= damage;
    if (blocks[i].hp <= 0) {
      float bx = blocks[i].x;
      float by = blocks[i].y;
      float bw = blocks[i].w;
      float bh = blocks[i].h;
      blocks[i].alive = false;
      score += 120;
      startVibration(80);
      spawnBlockFragments(bx, by, bw, bh);
    }

    if (fabsf(nx) > fabsf(ny)) {
      bird.vx *= -0.52f;
      bird.vy *= 0.87f;
    } else {
      bird.vy *= -0.52f;
      bird.vx *= 0.87f;
    }

    bird.x += (nx > 0 ? 1.2f : -1.2f);
    bird.y += (ny > 0 ? 1.2f : -1.2f);
  }

  for (int i = 0; i < MAX_PIGS; ++i) {
    if (!pigs[i].alive) continue;

    float dx = bird.x - pigs[i].x;
    float dy = bird.y - pigs[i].y;
    float rr = BIRD_R + pigs[i].r;
    if (dx * dx + dy * dy > rr * rr) continue;

    pigs[i].alive = false;
    score += 500;
    bird.vx *= 0.72f;
    bird.vy = -fabsf(bird.vy) * 0.45f;
    startVibration(140);
    playPigHitSfx();
  }

  if (bird.y > SCREEN_H + 36) {
    bird.active = false;
    reloadAtMs = millis() + 500;
    return;
  }

  float speed = fabsf(bird.vx) + fabsf(bird.vy);
  if (bird.y + BIRD_R >= GROUND_Y - 1 && speed < 0.45f) {
    bird.active = false;
    reloadAtMs = millis() + 460;
  }
}

void updateSlingControl() {
  if (!bird.loaded || bird.active) return;

  if (touchPressed) {
    float dx0 = touchX - bird.x;
    float dy0 = touchY - bird.y;
    if ((dx0 * dx0 + dy0 * dy0) <= 28.0f * 28.0f) {
      slingDragging = true;
    }
  }

  if (slingDragging && touchDown) {
    float dx = touchX - SLING_X;
    float dy = touchY - SLING_Y;
    if (dx > 8.0f) dx = 8.0f;

    float len = sqrtf(dx * dx + dy * dy);
    if (len > MAX_PULL && len > 0.001f) {
      float s = MAX_PULL / len;
      dx *= s;
      dy *= s;
    }

    bird.x = SLING_X + dx;
    bird.y = SLING_Y + dy;
  }

  if (slingDragging && touchReleased) {
    float pullX = SLING_X - bird.x;
    float pullY = SLING_Y - bird.y;
    float pullLen = sqrtf(pullX * pullX + pullY * pullY);
    if (pullLen >= 8.0f && birdsRemaining > 0) {
      bird.vx = pullX * LAUNCH_SCALE;
      bird.vy = pullY * LAUNCH_SCALE;
      bird.active = true;
      bird.loaded = false;
      birdsRemaining--;
      startVibration(45);
    } else {
      bird.x = SLING_X;
      bird.y = SLING_Y;
    }
    slingDragging = false;
  }

  if (!slingDragging) {
    bird.x = SLING_X;
    bird.y = SLING_Y;
  }
}

void updatePlaying() {
  updateSlingControl();
  updateBirdFlight();
  updateParticles();

  if (!bird.active && !bird.loaded && birdsRemaining > 0 && millis() >= reloadAtMs) {
    bird.loaded = true;
    bird.x = SLING_X;
    bird.y = SLING_Y;
    bird.vx = 0;
    bird.vy = 0;
  }

  if (pigsAliveCount() == 0) {
    enterGameOver(true);
    return;
  }

  if (birdsRemaining == 0 && !bird.active && !bird.loaded) {
    enterGameOver(false);
    return;
  }

  if (greenPressed) {
    gameState = STATE_TITLE;
    stateStartMs = millis();
  }
}

template <typename CanvasT>
void drawSky(CanvasT &canvas) {
  uint16_t top = rgb565(92, 170, 246);
  uint16_t mid = rgb565(154, 210, 255);
  uint16_t bottom = rgb565(228, 244, 255);
  for (int y = 0; y < SCREEN_H; ++y) {
    float t = (float)y / (float)(SCREEN_H - 1);
    uint16_t color = (t < 0.55f) ? lerp565(top, mid, t / 0.55f) : lerp565(mid, bottom, (t - 0.55f) / 0.45f);
    canvas.drawFastHLine(0, y, SCREEN_W, color);
  }

  canvas.fillCircle(76, 54, 28, rgb565(255, 229, 132));
  canvas.fillCircle(74, 52, 22, rgb565(255, 246, 176));
  canvas.fillCircle(82, 48, 8, rgb565(255, 255, 214));

  canvas.fillCircle(338, 52, 22, rgb565(198, 216, 236));
  canvas.fillCircle(360, 46, 26, rgb565(206, 224, 242));
  canvas.fillCircle(388, 50, 22, rgb565(196, 214, 236));
  canvas.fillCircle(350, 44, 18, rgb565(250, 254, 255));
  canvas.fillCircle(374, 42, 20, rgb565(252, 255, 255));
  canvas.fillCircle(398, 45, 16, rgb565(248, 253, 255));

  canvas.fillCircle(176, 86, 14, rgb565(204, 222, 242));
  canvas.fillCircle(194, 82, 16, rgb565(252, 255, 255));
  canvas.fillCircle(214, 86, 12, rgb565(248, 253, 255));
}

template <typename CanvasT>
void drawGround(CanvasT &canvas) {
  canvas.fillRect(0, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y, rgb565(98, 168, 76));
  canvas.fillRect(0, GROUND_Y - 8, SCREEN_W, 8, rgb565(118, 188, 86));
}

template <typename CanvasT>
void drawSlingshot(CanvasT &canvas) {
  canvas.fillRect((int)SLING_X - 14, (int)SLING_Y - 6, 8, 64, rgb565(122, 80, 46));
  canvas.fillRect((int)SLING_X + 6, (int)SLING_Y - 6, 8, 64, rgb565(122, 80, 46));
  canvas.fillRoundRect((int)SLING_X - 14, (int)SLING_Y - 14, 28, 12, 4, rgb565(168, 106, 56));
}

template <typename CanvasT>
void drawBlocksAndPigs(CanvasT &canvas) {
  for (int i = 0; i < MAX_BLOCKS; ++i) {
    if (!blocks[i].alive) continue;
    float t = (float)blocks[i].hp / (float)blocks[i].hpMax;
    int x = (int)blocks[i].x;
    int y = (int)blocks[i].y;
    int w = (int)blocks[i].w;
    int h = (int)blocks[i].h;

    canvas.fillRect(x + 3, y + 3, w, h, rgb565(68, 52, 30));

    uint8_t g = (uint8_t)(98 + t * 78);
    uint8_t b = (uint8_t)(56 + t * 58);
    uint16_t mainC = rgb565(174, g, b);
    uint16_t hiC = rgb565(214, (uint8_t)(g + 16), (uint8_t)(b + 10));
    uint16_t loC = rgb565(132, (uint8_t)(g - 20), (uint8_t)(b - 14));

    canvas.fillRect(x, y, w, h, mainC);
    canvas.fillRect(x, y, w, 4, hiC);
    canvas.fillRect(x, y + h - 4, w, 4, loC);
    canvas.drawRect(x, y, w, h, rgb565(84, 58, 28));

    int seamStep = (w > h) ? 18 : 12;
    for (int sx = x + seamStep; sx < x + w - 4; sx += seamStep) {
      canvas.drawFastVLine(sx, y + 2, h - 4, rgb565(122, 88, 48));
    }
    if (h >= 20) {
      canvas.drawFastHLine(x + 2, y + h / 2, w - 4, rgb565(122, 88, 48));
    }
  }

  for (int i = 0; i < MAX_PIGS; ++i) {
    if (!pigs[i].alive) continue;
    int px = (int)pigs[i].x;
    int py = (int)pigs[i].y;
    int pr = (int)pigs[i].r;
    canvas.fillCircle(px + 2, py + 3, pr, rgb565(44, 86, 38));
    canvas.fillCircle(px, py, pr, rgb565(114, 206, 78));
    canvas.fillCircle(px - 2, py - 3, pr - 3, rgb565(162, 236, 130));
    canvas.drawCircle(px, py, pr, rgb565(34, 108, 32));
    canvas.fillCircle(px - 4, py - 2, 2, TFT_BLACK);
    canvas.fillCircle(px + 4, py - 2, 2, TFT_BLACK);
    canvas.fillCircle(px, py + 4, 4, rgb565(94, 178, 72));
    canvas.fillCircle(px, py + 4, 2, rgb565(66, 146, 50));
    canvas.fillCircle(px - 5, py - 7, 2, rgb565(238, 255, 230));
  }
}

template <typename CanvasT>
void drawBird(CanvasT &canvas) {
  int bx = (int)bird.x;
  int by = (int)bird.y;
  int br = (int)BIRD_R;
  canvas.fillCircle(bx, by, br, rgb565(212, 48, 42));
  canvas.fillCircle(bx - 2, by - 3, br - 3, rgb565(246, 102, 90));
  canvas.drawCircle(bx, by, br, rgb565(118, 20, 18));
  canvas.fillCircle(bx + 2, by - 2, 2, TFT_WHITE);
  canvas.fillRect(bx + 6, by - 1, 5, 3, rgb565(255, 202, 62));
  canvas.fillRect(bx - 9, by - 3, 4, 2, rgb565(52, 18, 16));
}

template <typename CanvasT>
void drawAimPreview(CanvasT &canvas) {
  if (!bird.loaded || bird.active) return;

  float pullX = SLING_X - bird.x;
  float pullY = SLING_Y - bird.y;
  float pullLen = sqrtf(pullX * pullX + pullY * pullY);

  if (slingDragging && pullLen > 4.0f) {
    float vx = pullX * LAUNCH_SCALE;
    float vy = pullY * LAUNCH_SCALE;
    float px = bird.x;
    float py = bird.y;
    for (int i = 0; i < 15; ++i) {
      vy += GRAVITY;
      vx *= DRAG;
      vy *= DRAG;
      px += vx;
      py += vy;
      if (py > GROUND_Y) break;
      canvas.fillCircle((int)px, (int)py, 2, rgb565(255, 120, 196));
    }
  }

  int anchorLx = (int)SLING_X - 8;
  int anchorRx = (int)SLING_X + 8;
  int anchorY = (int)SLING_Y - 2;
  int bx = (int)bird.x;
  int by = (int)bird.y;
  canvas.drawLine(anchorLx, anchorY, bx, by, rgb565(84, 42, 26));
  canvas.drawLine(anchorRx, anchorY, bx, by, rgb565(84, 42, 26));
}

template <typename CanvasT>
void drawParticles(CanvasT &canvas) {
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    if (!particles[i].active) continue;
    float t = (float)particles[i].life / (float)particles[i].maxLife;
    uint16_t c = lerp565(rgb565(64, 54, 42), particles[i].color, t);
    int x = (int)particles[i].x;
    int y = (int)particles[i].y;
    int s = particles[i].size;
    canvas.fillRect(x, y, s, s, c);
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.fillRoundRect(10, 8, 220, 34, 8, rgb565(34, 52, 78));
  canvas.setTextColor(rgb565(224, 238, 255), rgb565(34, 52, 78));

  char buf[64];
  snprintf(buf, sizeof(buf), "Score %d", score);
  canvas.drawString(buf, 20, 18, 2);

  canvas.drawString("Birds", 120, 18, 2);
  int reserveBirds = birdsRemaining - (bird.loaded ? 1 : 0);
  if (reserveBirds < 0) reserveBirds = 0;
  int maxReserveSlots = INITIAL_BIRDS - 1;
  if (reserveBirds > maxReserveSlots) reserveBirds = maxReserveSlots;
  for (int i = 0; i < reserveBirds; ++i) {
    int bx = 168 + i * 10;
    int by = 24;
    canvas.fillCircle(bx, by, 4, rgb565(226, 62, 56));
    canvas.drawCircle(bx, by, 4, rgb565(122, 24, 20));
    canvas.fillRect(bx + 3, by - 1, 3, 2, rgb565(255, 204, 78));
  }

  canvas.fillRoundRect(244, 8, 226, 34, 8, rgb565(44, 64, 46));
  canvas.setTextColor(rgb565(210, 252, 202), rgb565(44, 64, 46));
  snprintf(buf, sizeof(buf), "Pigs left %d", pigsAliveCount());
  canvas.drawString(buf, 258, 18, 2);
}

template <typename CanvasT>
void drawPlaying(CanvasT &canvas) {
  drawSky(canvas);
  drawGround(canvas);
  drawSlingshot(canvas);
  drawBlocksAndPigs(canvas);
  drawParticles(canvas);
  drawAimPreview(canvas);
  if (bird.active || bird.loaded) {
    drawBird(canvas);
  }
  drawHud(canvas);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  canvas.fillScreen(rgb565(18, 28, 46));
  for (int y = 0; y < SCREEN_H; y += 14) {
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(24, 38, 62));
  }

  canvas.fillRoundRect(52, 40, SCREEN_W - 104, 208, 16, rgb565(34, 50, 78));
  canvas.drawRoundRect(52, 40, SCREEN_W - 104, 208, 16, rgb565(116, 178, 255));

  canvas.setTextColor(rgb565(255, 196, 86), rgb565(34, 50, 78));
  canvas.drawCentreString("ANGRY BIRDS", SCREEN_W / 2, 74, 4);

  canvas.setTextColor(rgb565(232, 244, 255), rgb565(34, 50, 78));
  canvas.drawCentreString("Factory Arcade Edition", SCREEN_W / 2, 112, 2);
  canvas.drawCentreString("Touch bird and drag to pull", SCREEN_W / 2, 146, 2);
  canvas.drawCentreString("Release finger to launch", SCREEN_W / 2, 170, 2);
  canvas.drawCentreString("Green: Quit round", SCREEN_W / 2, 194, 2);

  const int birdX = 116;
  const int birdY = 196;
  canvas.fillCircle(birdX + 4, birdY + 8, 20, rgb565(26, 24, 30));
  canvas.fillCircle(birdX, birdY, 18, rgb565(220, 58, 52));
  canvas.fillCircle(birdX - 6, birdY - 6, 6, rgb565(252, 136, 126));
  canvas.fillCircle(birdX - 5, birdY - 2, 4, TFT_WHITE);
  canvas.fillCircle(birdX + 6, birdY - 3, 4, TFT_WHITE);
  canvas.fillCircle(birdX - 4, birdY - 2, 2, TFT_BLACK);
  canvas.fillCircle(birdX + 7, birdY - 2, 2, TFT_BLACK);
  canvas.drawLine(birdX - 10, birdY - 10, birdX + 1, birdY - 8, TFT_BLACK);
  canvas.drawLine(birdX - 9, birdY - 12, birdX + 2, birdY - 10, TFT_BLACK);
  canvas.fillRoundRect(birdX + 10, birdY + 1, 10, 5, 2, rgb565(252, 204, 92));
  canvas.fillRoundRect(birdX + 18, birdY + 2, 6, 3, 1, rgb565(236, 148, 62));
  canvas.fillRect(birdX - 2, birdY - 18, 4, 6, rgb565(58, 20, 20));

  int slingX = 350;
  int slingY = 210;
  canvas.fillRoundRect(slingX - 18, slingY - 24, 10, 36, 4, rgb565(142, 98, 56));
  canvas.fillRoundRect(slingX + 8, slingY - 24, 10, 36, 4, rgb565(142, 98, 56));
  canvas.fillRoundRect(slingX - 24, slingY + 10, 48, 8, 4, rgb565(104, 72, 40));
  canvas.drawLine(slingX - 13, slingY - 20, slingX + 13, slingY - 20, rgb565(224, 186, 140));
  canvas.drawLine(slingX - 11, slingY - 18, slingX + 11, slingY - 18, rgb565(224, 186, 140));
  canvas.fillCircle(slingX + 30, slingY - 18, 8, rgb565(116, 222, 116));
  canvas.fillCircle(slingX + 27, slingY - 21, 3, rgb565(190, 248, 186));
  canvas.fillCircle(slingX + 29, slingY - 20, 1, TFT_BLACK);

  uint16_t blink = ((millis() / 420) % 2) ? rgb565(255, 244, 152) : rgb565(132, 136, 78);
  canvas.setTextColor(blink, rgb565(34, 50, 78));
  canvas.drawCentreString("TOUCH SCREEN TO START", SCREEN_W / 2, 226, 2);
}

template <typename CanvasT>
void drawGameOver(CanvasT &canvas) {
  uint16_t bg = gameWon ? rgb565(14, 34, 24) : rgb565(40, 16, 18);
  canvas.fillScreen(bg);

  canvas.setTextColor(gameWon ? rgb565(164, 255, 186) : rgb565(255, 134, 126), bg);
  canvas.drawCentreString(gameWon ? "LEVEL CLEAR" : "GAME OVER", SCREEN_W / 2, 106, 4);

  canvas.setTextColor(TFT_WHITE, bg);
  char line[64];
  snprintf(line, sizeof(line), "SCORE  %d", score);
  canvas.drawCentreString(line, SCREEN_W / 2, 158, 2);
  canvas.drawCentreString("Entering leaderboard...", SCREEN_W / 2, 184, 2);
}

template <typename CanvasT>
void drawLeaderboard(CanvasT &canvas) {
  uint16_t bg = rgb565(18, 22, 36);
  uint16_t panel = rgb565(28, 36, 58);
  canvas.fillScreen(bg);

  canvas.fillRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, panel);
  canvas.drawRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, rgb565(126, 166, 234));

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
    canvas.drawString(line, 120, y, 2);

    if (isLatest) {
      canvas.drawString("< NOW", 300, y, 2);
    }
  }

  canvas.setTextColor(rgb565(172, 192, 224), panel);
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

  initFrameBuffer();
  initAudio();
  setupLevel();

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
      if (gameState != STATE_PLAYING) {
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
