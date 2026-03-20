#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_system.h>
#include <driver/i2s.h>
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
TFT_eSprite frameBuffer = TFT_eSprite(&tft);
TFT_eSprite ballSprites[5] = {
  TFT_eSprite(&tft),
  TFT_eSprite(&tft),
  TFT_eSprite(&tft),
  TFT_eSprite(&tft),
  TFT_eSprite(&tft)
};

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const uint16_t UPDATE_MS = 16;

static const int PATH_POINTS = 9;
static const int MAX_CHAIN_BALLS = 96;
static const int BALL_R = 10;
static const float BALL_SPACING = 20.0f;
static const float SHOT_SPEED = 17.2f;
static const float AIM_DEADZONE = 260.0f;
static const uint16_t SHOT_COOLDOWN_MS = 140;
static const uint8_t COLOR_COUNT = 5;
static const uint8_t LEADERBOARD_SIZE = 5;
static const int BALL_SPRITE_SIZE = BALL_R * 2 + 10;
static const uint16_t BALL_SPRITE_KEY = 0x0123;
static const uint8_t INSERT_ANIM_FRAMES = 10;
static const uint16_t INSERT_ANIM_FRAME_MS = 100;
static const uint8_t POP_ANIM_FRAMES = 5;
static const uint16_t POP_ANIM_FRAME_MS = 100;
static const uint16_t RECONNECT_HOLD_MS = 400;
static const uint16_t RECONNECT_RETURN_MS = 500;

static const float shooterX = SCREEN_W * 0.5f;
static const float shooterY = SCREEN_H - 36.0f;

enum GameState {
  TITLE,
  PLAYING,
  LEADERBOARD
};

struct PathNode {
  float x;
  float y;
};

struct Shot {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  uint8_t color;
};

struct PopBall {
  float x;
  float y;
  uint8_t color;
};

PathNode pathPoints[PATH_POINTS] = {
  {36.0f, 36.0f},
  {444.0f, 36.0f},
  {444.0f, 250.0f},
  {72.0f, 250.0f},
  {72.0f, 74.0f},
  {406.0f, 74.0f},
  {406.0f, 214.0f},
  {138.0f, 214.0f},
  {240.0f, 154.0f}
};

float pathSegLen[PATH_POINTS - 1];
float pathTotalLen = 0.0f;

GameState gameState = TITLE;
GameState lastRenderedState = LEADERBOARD;
bool frameBufferReady = false;
bool ballSpritesReady[COLOR_COUNT] = {false, false, false, false, false};

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
int latestX = 2048;
int latestY = 2048;

uint8_t chainColors[MAX_CHAIN_BALLS];
int chainCount = 0;
float chainHeadProgress = 0.0f;
float chainSpeed = 0.8f;
uint8_t currentColor = 0;
uint8_t nextColor = 1;
Shot shot = {false, 0, 0, 0, 0, 0};
float aimDx = 0.0f;
float aimDy = -1.0f;
uint16_t score = 0;
uint16_t wave = 1;
bool scoreSubmitted = false;
int8_t latestRank = -1;
uint16_t leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
char endReason[24] = "";
bool insertAnimActive = false;
int insertAnimIndex = -1;
uint8_t insertAnimFrame = 0;
unsigned long insertAnimNextMs = 0;
float insertAnimStartX = 0.0f;
float insertAnimStartY = 0.0f;
bool pendingResolve = false;
int pendingResolveIndex = -1;
bool popAnimActive = false;
uint8_t popAnimFrame = 0;
unsigned long popAnimNextMs = 0;
PopBall popBalls[MAX_CHAIN_BALLS];
int popBallCount = 0;
bool reconnectAnimActive = false;
float reconnectOffset = 0.0f;
float reconnectMaxOffset = 0.0f;
int reconnectTailLimit = 0;
bool reconnectHoldActive = false;
unsigned long reconnectHoldUntil = 0;
bool reconnectReturnActive = false;
unsigned long reconnectReturnStartMs = 0;

unsigned long lastUpdateMs = 0;
unsigned long titlePulseMs = 0;
unsigned long boardPulseMs = 0;
unsigned long shotCooldownUntil = 0;
bool lastTitlePromptVisible = false;
bool lastBoardPromptVisible = false;
bool vibrating = false;
bool vibForward = true;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;
bool gameOverJinglePlayed = false;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];

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

const uint16_t ballColors[COLOR_COUNT] = {
  0xF9C4,
  0x07FF,
  0xFFE0,
  0xFC92,
  0x02E0
};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t lighten565(uint16_t color, uint8_t amount) {
  uint8_t r = (((color >> 11) & 0x1F) * 255) / 31;
  uint8_t g = (((color >> 5) & 0x3F) * 255) / 63;
  uint8_t b = ((color & 0x1F) * 255) / 31;
  r = (uint8_t)min(255, r + amount);
  g = (uint8_t)min(255, g + amount);
  b = (uint8_t)min(255, b + amount);
  return rgb565(r, g, b);
}

uint16_t scale565(uint16_t color, uint8_t scale) {
  uint8_t r = (((color >> 11) & 0x1F) * 255) / 31;
  uint8_t g = (((color >> 5) & 0x3F) * 255) / 63;
  uint8_t b = ((color & 0x1F) * 255) / 31;
  r = (uint8_t)((r * scale) / 255);
  g = (uint8_t)((g * scale) / 255);
  b = (uint8_t)((b * scale) / 255);
  return rgb565(r, g, b);
}

uint16_t mix565(uint16_t a, uint16_t b, uint8_t t) {
  uint8_t ar = (((a >> 11) & 0x1F) * 255) / 31;
  uint8_t ag = (((a >> 5) & 0x3F) * 255) / 63;
  uint8_t ab = ((a & 0x1F) * 255) / 31;
  uint8_t br = (((b >> 11) & 0x1F) * 255) / 31;
  uint8_t bg = (((b >> 5) & 0x3F) * 255) / 63;
  uint8_t bb = ((b & 0x1F) * 255) / 31;

  uint8_t r = (uint8_t)((ar * (255 - t) + br * t) / 255);
  uint8_t g = (uint8_t)((ag * (255 - t) + bg * t) / 255);
  uint8_t bch = (uint8_t)((ab * (255 - t) + bb * t) / 255);
  return rgb565(r, g, bch);
}

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startVibration(uint16_t durationMs = 120) {
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
  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 28) {
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

void setupAudio() {
  i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, nullptr);
  i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

void playToneI2S(float freq, uint16_t ms, float volume = 0.25f) {
  int totalSamples = (SAMPLE_RATE * (int)ms) / 1000;
  if (totalSamples <= 0) {
    return;
  }
  float phase = 0.0f;
  float phaseStep = (freq > 0.0f) ? (2.0f * PI * freq / SAMPLE_RATE) : 0.0f;
  int remaining = totalSamples;
  while (remaining > 0) {
    int n = min((int)AUDIO_CHUNK_SAMPLES, remaining);
    for (int i = 0; i < n; ++i) {
      float s = 0.0f;
      if (freq > 0.0f) {
        s = sinf(phase) * volume;
        phase += phaseStep;
        if (phase > 2.0f * PI) {
          phase -= 2.0f * PI;
        }
      }
      int32_t v = (int32_t)(s * 32767.0f);
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      audioBuffer[i] = (int16_t)v;
    }
    size_t written = 0;
    i2s_write(I2S_NUM_1, audioBuffer, n * sizeof(int16_t), &written, portMAX_DELAY);
    remaining -= n;
  }
}

void playGameOverJingle() {
  static const uint16_t melody[][2] = {
    {784, 120}, {659, 120}, {523, 150}, {392, 220}, {0, 40}, {330, 280}
  };
  for (int i = 0; i < (int)(sizeof(melody) / sizeof(melody[0])); ++i) {
    playToneI2S((float)melody[i][0], melody[i][1], 0.25f);
  }
  // Force a clean tail so DMA doesn't hold the last audible sample.
  playToneI2S(0.0f, 140, 0.0f);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

uint8_t randomColor() {
  return (uint8_t)(esp_random() % COLOR_COUNT);
}

void computePathLength() {
  pathTotalLen = 0.0f;
  for (int i = 0; i < PATH_POINTS - 1; ++i) {
    float dx = pathPoints[i + 1].x - pathPoints[i].x;
    float dy = pathPoints[i + 1].y - pathPoints[i].y;
    float len = sqrtf(dx * dx + dy * dy);
    pathSegLen[i] = len;
    pathTotalLen += len;
  }
}

void pathPos(float progress, float &x, float &y, float &tx, float &ty) {
  if (progress <= 0.0f) {
    float dx0 = pathPoints[1].x - pathPoints[0].x;
    float dy0 = pathPoints[1].y - pathPoints[0].y;
    float len0 = pathSegLen[0];
    if (len0 < 0.001f) {
      x = pathPoints[0].x;
      y = pathPoints[0].y;
      tx = 1.0f;
      ty = 0.0f;
      return;
    }
    tx = dx0 / len0;
    ty = dy0 / len0;
    x = pathPoints[0].x + tx * progress;
    y = pathPoints[0].y + ty * progress;
    return;
  }

  if (progress >= pathTotalLen) {
    float dx1 = pathPoints[PATH_POINTS - 1].x - pathPoints[PATH_POINTS - 2].x;
    float dy1 = pathPoints[PATH_POINTS - 1].y - pathPoints[PATH_POINTS - 2].y;
    float len1 = pathSegLen[PATH_POINTS - 2];
    if (len1 < 0.001f) {
      x = pathPoints[PATH_POINTS - 1].x;
      y = pathPoints[PATH_POINTS - 1].y;
      tx = 1.0f;
      ty = 0.0f;
      return;
    }
    tx = dx1 / len1;
    ty = dy1 / len1;
    float extra = progress - pathTotalLen;
    x = pathPoints[PATH_POINTS - 1].x + tx * extra;
    y = pathPoints[PATH_POINTS - 1].y + ty * extra;
    return;
  }

  float remain = progress;
  for (int i = 0; i < PATH_POINTS - 1; ++i) {
    float seg = pathSegLen[i];
    if (remain <= seg) {
      float nx = pathPoints[i + 1].x - pathPoints[i].x;
      float ny = pathPoints[i + 1].y - pathPoints[i].y;
      if (seg < 0.001f) {
        tx = 1.0f;
        ty = 0.0f;
        x = pathPoints[i].x;
        y = pathPoints[i].y;
      } else {
        tx = nx / seg;
        ty = ny / seg;
        x = pathPoints[i].x + tx * remain;
        y = pathPoints[i].y + ty * remain;
      }
      return;
    }
    remain -= seg;
  }

  x = pathPoints[PATH_POINTS - 1].x;
  y = pathPoints[PATH_POINTS - 1].y;
  tx = 1.0f;
  ty = 0.0f;
}

void initFrameBuffer() {
  frameBuffer.setColorDepth(16);
  frameBufferReady = frameBuffer.createSprite(SCREEN_W, SCREEN_H);
  if (!frameBufferReady) {
    frameBuffer.deleteSprite();
    frameBuffer.setColorDepth(8);
    frameBufferReady = frameBuffer.createSprite(SCREEN_W, SCREEN_H);
  }
}

void initBallSprites() {
  for (int i = 0; i < COLOR_COUNT; ++i) {
    ballSpritesReady[i] = false;
    ballSprites[i].deleteSprite();
    ballSprites[i].setColorDepth(16);
    if (!ballSprites[i].createSprite(BALL_SPRITE_SIZE, BALL_SPRITE_SIZE)) {
      continue;
    }

    const int cx = BALL_SPRITE_SIZE / 2;
    const int cy = BALL_SPRITE_SIZE / 2;
    const uint16_t base = ballColors[i];
    const uint16_t rimDark = scale565(base, 88);

    ballSprites[i].fillSprite(BALL_SPRITE_KEY);

    // Clean physically-inspired shading with one directional light.
    const float lx = -0.58f;
    const float ly = -0.58f;
    const float lz = 0.58f;
    for (int py = cy - BALL_R; py <= cy + BALL_R; ++py) {
      for (int px = cx - BALL_R; px <= cx + BALL_R; ++px) {
        float nx = (float)(px - cx) / (float)BALL_R;
        float ny = (float)(py - cy) / (float)BALL_R;
        float r2 = nx * nx + ny * ny;
        if (r2 > 1.0f) {
          continue;
        }

        float nz = sqrtf(1.0f - r2);
        float ndotl = nx * lx + ny * ly + nz * lz;
        if (ndotl < 0.0f) {
          ndotl = 0.0f;
        }

        float ambient = 0.56f;
        float diffuse = 0.74f * ndotl;
        float edgeLift = (1.0f - nz) * 0.10f;
        float shade = ambient + diffuse + edgeLift;

        // Slight bottom-right darkening gives clearer depth.
        float occlusion = 1.0f - max(0.0f, (nx + ny) * 0.18f);
        shade *= occlusion;
        if (shade < 0.42f) shade = 0.42f;
        if (shade > 1.38f) shade = 1.38f;

        uint8_t s = (uint8_t)(shade * 190.0f);
        ballSprites[i].drawPixel(px, py, scale565(base, s));
      }
    }

    ballSprites[i].drawCircle(cx, cy, BALL_R, rimDark);

    // Keep a single glossy blob, fully inside the sphere.
    ballSprites[i].fillCircle(cx - 4, cy - 4, 4, lighten565(base, 126));
    ballSprites[i].fillCircle(cx - 4, cy - 4, 2, lighten565(base, 142));

    ballSpritesReady[i] = true;
  }
}

void readButtons() {
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);

  bool gNow = digitalRead(green_btn_pin) == LOW;
  bool bNow = digitalRead(blue_btn_pin) == LOW;
  greenPressed = (!greenDown && gNow);
  bluePressed = (!blueDown && bNow);
  greenDown = gNow;
  blueDown = bNow;

  digitalWrite(green_led_pin, greenDown ? LOW : HIGH);
  digitalWrite(blue_led_pin, blueDown ? LOW : HIGH);
}

void pickShooterColors() {
  currentColor = randomColor();
  nextColor = randomColor();
}

void setupWave(bool resetScore) {
  if (resetScore) {
    score = 0;
    wave = 1;
  }
  chainCount = min(MAX_CHAIN_BALLS, 24 + (int)wave * 3);
  for (int i = 0; i < chainCount; ++i) {
    uint8_t c = randomColor();
    if (i >= 2 && chainColors[i - 1] == c && chainColors[i - 2] == c) {
      c = (uint8_t)((c + 1 + (esp_random() % (COLOR_COUNT - 1))) % COLOR_COUNT);
    }
    chainColors[i] = c;
  }
  chainHeadProgress = 140.0f;
  chainSpeed = (0.80f + min(1.40f, wave * 0.12f)) * 2.0f;
  shot.active = false;
  shotCooldownUntil = 0;
  insertAnimActive = false;
  insertAnimIndex = -1;
  insertAnimFrame = 0;
  pendingResolve = false;
  pendingResolveIndex = -1;
  popAnimActive = false;
  popAnimFrame = 0;
  popBallCount = 0;
  reconnectAnimActive = false;
  reconnectOffset = 0.0f;
  reconnectMaxOffset = 0.0f;
  reconnectTailLimit = 0;
  reconnectHoldActive = false;
  reconnectHoldUntil = 0;
  reconnectReturnActive = false;
  reconnectReturnStartMs = 0;
}

void beginRun() {
  pickShooterColors();
  setupWave(true);
  scoreSubmitted = false;
  gameOverJinglePlayed = false;
  latestRank = -1;
  snprintf(endReason, sizeof(endReason), "Chain reached the core");
  aimDx = 0.0f;
  aimDy = -1.0f;
  gameState = PLAYING;
  lastRenderedState = TITLE;
  lastUpdateMs = millis();
}

void submitScore() {
  if (scoreSubmitted) {
    return;
  }
  scoreSubmitted = true;
  latestRank = LEADERBOARD_SIZE - 1;

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    if (score > leaderboard[i]) {
      latestRank = i;
      break;
    }
  }

  for (int i = LEADERBOARD_SIZE - 1; i > latestRank; --i) {
    leaderboard[i] = leaderboard[i - 1];
  }
  leaderboard[latestRank] = score;
}

void removeRange(int left, int right) {
  int count = right - left + 1;
  if (count <= 0 || left < 0 || right >= chainCount) {
    return;
  }
  for (int i = left; i + count < chainCount; ++i) {
    chainColors[i] = chainColors[i + count];
  }
  chainCount -= count;
}

bool startPopForMatchAt(int origin) {
  if (origin < 0 || origin >= chainCount) {
    return false;
  }
  uint8_t color = chainColors[origin];
  int left = origin;
  int right = origin;
  while (left > 0 && chainColors[left - 1] == color) {
    --left;
  }
  while (right + 1 < chainCount && chainColors[right + 1] == color) {
    ++right;
  }
  int groupSize = right - left + 1;
  if (groupSize < 3) {
    return false;
  }

  popBallCount = 0;
  for (int i = left; i <= right && popBallCount < MAX_CHAIN_BALLS; ++i) {
    float progress = chainHeadProgress - (float)(chainCount - 1 - i) * BALL_SPACING;
    float bx, by, tx, ty;
    pathPos(progress, bx, by, tx, ty);
    popBalls[popBallCount].x = bx;
    popBalls[popBallCount].y = by;
    popBalls[popBallCount].color = chainColors[i];
    popBallCount++;
  }

  score += (uint16_t)(groupSize * 12 + 8);
  removeRange(left, right);
  startVibration((uint16_t)min(240, 90 + groupSize * 18));

  reconnectAnimActive = (left > 0);
  reconnectTailLimit = left;
  reconnectMaxOffset = min(52.0f, groupSize * (BALL_SPACING * 0.75f));
  reconnectOffset = 0.0f;
  reconnectHoldActive = false;
  reconnectHoldUntil = 0;
  reconnectReturnActive = false;
  reconnectReturnStartMs = 0;

  popAnimActive = true;
  popAnimFrame = 0;
  popAnimNextMs = millis() + POP_ANIM_FRAME_MS;

  if (chainCount > 0) {
    pendingResolve = true;
    pendingResolveIndex = max(0, left - 1);
    if (pendingResolveIndex >= chainCount) {
      pendingResolveIndex = chainCount - 1;
    }
  } else {
    pendingResolve = false;
    pendingResolveIndex = -1;
  }
  return true;
}

void tryResolvePendingPop() {
  if (pendingResolve && pendingResolveIndex >= 0 && pendingResolveIndex < chainCount) {
    if (!startPopForMatchAt(pendingResolveIndex)) {
      pendingResolve = false;
      pendingResolveIndex = -1;
    }
  } else {
    pendingResolve = false;
    pendingResolveIndex = -1;
  }
}

void startInsertAnimation(int index, float startX, float startY) {
  if (chainCount <= 0) {
    insertAnimActive = false;
    insertAnimIndex = -1;
    return;
  }
  insertAnimActive = true;
  insertAnimIndex = constrain(index, 0, chainCount - 1);
  insertAnimFrame = 0;
  insertAnimNextMs = millis() + INSERT_ANIM_FRAME_MS;
  insertAnimStartX = startX;
  insertAnimStartY = startY;
}

void endRun(const char *reason) {
  snprintf(endReason, sizeof(endReason), "%s", reason);
  submitScore();
  startVibration(220);
  if (!gameOverJinglePlayed) {
    gameOverJinglePlayed = true;
    playGameOverJingle();
  }
  boardPulseMs = millis();
  gameState = LEADERBOARD;
  lastRenderedState = PLAYING;
}

void updateAimFromJoystick() {
  float dx = (float)latestX - 2048.0f;
  float dy = (float)latestY - 2048.0f;
  float mag = sqrtf(dx * dx + dy * dy);
  if (mag > AIM_DEADZONE) {
    aimDx = dx / mag;
    aimDy = dy / mag;
  }
}

void shootNow() {
  shot.active = true;
  shot.x = shooterX + aimDx * (BALL_R + 8.0f);
  shot.y = shooterY + aimDy * (BALL_R + 8.0f);
  shot.vx = aimDx * SHOT_SPEED;
  shot.vy = aimDy * SHOT_SPEED;
  shot.color = currentColor;
  currentColor = nextColor;
  nextColor = randomColor();
}

int findHitBallIndex() {
  int bestIndex = -1;
  float bestDist2 = 999999.0f;
  for (int i = 0; i < chainCount; ++i) {
    float progress = chainHeadProgress - (float)(chainCount - 1 - i) * BALL_SPACING;
    if (progress < -BALL_SPACING || progress > pathTotalLen + BALL_SPACING) {
      continue;
    }
    float bx, by, tx, ty;
    pathPos(progress, bx, by, tx, ty);
    float dx = shot.x - bx;
    float dy = shot.y - by;
    float d2 = dx * dx + dy * dy;
    float hitR = (BALL_R * 2.0f - 1.5f);
    if (d2 <= hitR * hitR && d2 < bestDist2) {
      bestDist2 = d2;
      bestIndex = i;
    }
  }
  return bestIndex;
}

void insertBallAtHit(int hitIndex) {
  float progress = chainHeadProgress - (float)(chainCount - 1 - hitIndex) * BALL_SPACING;
  float bx, by, tx, ty;
  pathPos(progress, bx, by, tx, ty);
  float relx = shot.x - bx;
  float rely = shot.y - by;
  float side = relx * tx + rely * ty;
  int insertIndex = (side >= 0.0f) ? (hitIndex + 1) : hitIndex;

  if (chainCount >= MAX_CHAIN_BALLS) {
    return;
  }
  for (int i = chainCount; i > insertIndex; --i) {
    chainColors[i] = chainColors[i - 1];
  }
  chainColors[insertIndex] = shot.color;
  chainCount++;
  startInsertAnimation(insertIndex, shot.x, shot.y);
  pendingResolve = true;
  pendingResolveIndex = insertIndex;
}

void updatePlaying() {
  updateAimFromJoystick();

  if (greenPressed) {
    uint8_t temp = currentColor;
    currentColor = nextColor;
    nextColor = temp;
  }

  unsigned long now = millis();
  if (bluePressed && !shot.active && !insertAnimActive && !popAnimActive &&
      !reconnectHoldActive && !reconnectReturnActive &&
      now >= shotCooldownUntil) {
    shootNow();
    shotCooldownUntil = now + SHOT_COOLDOWN_MS;
  }

  if (!popAnimActive && !reconnectHoldActive && !reconnectReturnActive) {
    chainHeadProgress += chainSpeed;
    if (chainHeadProgress >= pathTotalLen - 4.0f) {
      endRun("Core overrun");
      return;
    }
  }

  if (shot.active) {
    shot.x += shot.vx;
    shot.y += shot.vy;

    if (shot.x < -16 || shot.x > SCREEN_W + 16 || shot.y < -16 || shot.y > SCREEN_H + 16) {
      shot.active = false;
    } else {
      int hitIndex = findHitBallIndex();
      if (hitIndex >= 0) {
        insertBallAtHit(hitIndex);
        shot.active = false;
      }
    }
  }

  if (insertAnimActive) {
    while (now >= insertAnimNextMs) {
      insertAnimFrame++;
      insertAnimNextMs += INSERT_ANIM_FRAME_MS;
      if (insertAnimFrame >= INSERT_ANIM_FRAMES) {
        insertAnimActive = false;
        insertAnimIndex = -1;
        if (pendingResolve) {
          if (pendingResolveIndex >= chainCount) {
            pendingResolveIndex = chainCount - 1;
          }
          if (!popAnimActive) {
            tryResolvePendingPop();
          }
        }
        break;
      } else if (insertAnimIndex >= chainCount) {
        insertAnimIndex = chainCount - 1;
      }
    }
  }

  if (popAnimActive) {
    if (POP_ANIM_FRAMES > 1) {
      float t = (float)popAnimFrame / (float)(POP_ANIM_FRAMES - 1);
      if (reconnectAnimActive) {
        reconnectOffset = -reconnectMaxOffset * t;
      }
    } else {
      reconnectOffset = 0.0f;
    }

    while (now >= popAnimNextMs) {
      popAnimFrame++;
      popAnimNextMs += POP_ANIM_FRAME_MS;
      if (popAnimFrame >= POP_ANIM_FRAMES) {
        popAnimActive = false;
        popBallCount = 0;
        if (reconnectAnimActive && reconnectTailLimit > 0 && reconnectMaxOffset > 0.0f) {
          reconnectOffset = -reconnectMaxOffset;
          reconnectHoldActive = true;
          reconnectHoldUntil = now + RECONNECT_HOLD_MS;
        } else {
          reconnectAnimActive = false;
          reconnectOffset = 0.0f;
          reconnectMaxOffset = 0.0f;
          reconnectTailLimit = 0;
          tryResolvePendingPop();
        }
        break;
      }
    }
  }

  if (reconnectHoldActive && now >= reconnectHoldUntil) {
    reconnectHoldActive = false;
    reconnectReturnActive = true;
    reconnectReturnStartMs = now;
  }

  if (reconnectReturnActive) {
    unsigned long elapsed = now - reconnectReturnStartMs;
    float t = (float)elapsed / (float)RECONNECT_RETURN_MS;
    if (t >= 1.0f) {
      reconnectReturnActive = false;
      reconnectAnimActive = false;
      reconnectOffset = 0.0f;
      reconnectMaxOffset = 0.0f;
      reconnectTailLimit = 0;
      tryResolvePendingPop();
    } else {
      float ease = t * t * (3.0f - 2.0f * t);
      reconnectOffset = -reconnectMaxOffset * (1.0f - ease);
    }
  }

  if (chainCount <= 0 && !popAnimActive && !insertAnimActive &&
      !reconnectHoldActive && !reconnectReturnActive) {
    score += (uint16_t)(120 + wave * 20);
    wave++;
    setupWave(false);
  }
}
template <typename CanvasT>
void fillBackgroundSplit(CanvasT &canvas, uint16_t color) {
  int halfW = SCREEN_W / 2;
  canvas.fillRect(0, 0, halfW, SCREEN_H, color);
  canvas.fillRect(halfW, 0, SCREEN_W - halfW, SCREEN_H, color);
}

template <typename CanvasT>
void drawPath(CanvasT &canvas) {
  uint16_t steelDark = rgb565(48, 56, 64);
  uint16_t steelMid = rgb565(108, 122, 132);
  uint16_t steelLite = rgb565(168, 186, 198);
  const int tubeR = 6;

  auto bandColor = [&](int off) -> uint16_t {
    float u = (float)off / (float)tubeR;   // -1..1
    float rim = fabsf(u);
    float highlight = max(0.0f, -u) * 0.32f;  // top-left side highlight
    float shade = 0.42f + (1.0f - rim) * 0.42f + highlight;
    if (shade > 1.20f) shade = 1.20f;
    if (shade < 0.32f) shade = 0.32f;
    return scale565(steelMid, (uint8_t)(shade * 180.0f));
  };

  // Draw each segment as a continuous "tube strip" with layered bands.
  for (int i = 0; i < PATH_POINTS - 1; ++i) {
    float x0 = pathPoints[i].x;
    float y0 = pathPoints[i].y;
    float x1 = pathPoints[i + 1].x;
    float y1 = pathPoints[i + 1].y;
    float dx = x1 - x0;
    float dy = y1 - y0;
    float segLen = sqrtf(dx * dx + dy * dy);
    if (segLen < 0.001f) {
      continue;
    }
    float nx = -dy / segLen;
    float ny = dx / segLen;

    // Outer shadow shell.
    canvas.drawLine((int)(x0 + nx * (tubeR + 1)), (int)(y0 + ny * (tubeR + 1)),
                    (int)(x1 + nx * (tubeR + 1)), (int)(y1 + ny * (tubeR + 1)),
                    scale565(steelDark, 74));
    canvas.drawLine((int)(x0 - nx * (tubeR + 1)), (int)(y0 - ny * (tubeR + 1)),
                    (int)(x1 - nx * (tubeR + 1)), (int)(y1 - ny * (tubeR + 1)),
                    scale565(steelDark, 90));

    // Tube body bands (continuous, not dotted).
    for (int off = -tubeR; off <= tubeR; ++off) {
      uint16_t c = bandColor(off);
      canvas.drawLine((int)(x0 + nx * off), (int)(y0 + ny * off),
                      (int)(x1 + nx * off), (int)(y1 + ny * off), c);
    }
  }

  // Round joints so bends stay "one pipe" instead of segmented pieces.
  for (int i = 0; i < PATH_POINTS; ++i) {
    int cxj = (int)pathPoints[i].x;
    int cyj = (int)pathPoints[i].y;
    for (int r = tubeR; r >= 1; --r) {
      uint8_t t = (uint8_t)((255 * (tubeR - r)) / tubeR);
      uint16_t c = mix565(steelLite, steelMid, t);
      canvas.fillCircle(cxj, cyj, r, c);
    }
    canvas.drawCircle(cxj, cyj, tubeR, scale565(steelDark, 86));
  }

  // Longitudinal specular and shadow lines to reinforce cylindrical metal.
  for (int i = 0; i < PATH_POINTS - 1; ++i) {
    float x0 = pathPoints[i].x;
    float y0 = pathPoints[i].y;
    float x1 = pathPoints[i + 1].x;
    float y1 = pathPoints[i + 1].y;
    float dx = x1 - x0;
    float dy = y1 - y0;
    float segLen = sqrtf(dx * dx + dy * dy);
    if (segLen < 0.001f) {
      continue;
    }
    float nx = -dy / segLen;
    float ny = dx / segLen;
    canvas.drawLine((int)(x0 - nx * 2.0f), (int)(y0 - ny * 2.0f),
                    (int)(x1 - nx * 2.0f), (int)(y1 - ny * 2.0f),
                    lighten565(steelLite, 36));
    canvas.drawLine((int)(x0 + nx * 3.0f), (int)(y0 + ny * 3.0f),
                    (int)(x1 + nx * 3.0f), (int)(y1 + ny * 3.0f),
                    scale565(steelDark, 82));
  }

  // Core endpoint as metallic sink ring.
  int cx = (int)pathPoints[PATH_POINTS - 1].x;
  int cy = (int)pathPoints[PATH_POINTS - 1].y;
  canvas.fillCircle(cx, cy, 14, rgb565(34, 40, 46));
  canvas.fillCircle(cx + 1, cy + 1, 10, rgb565(18, 22, 28));
  canvas.drawCircle(cx, cy, 16, lighten565(steelLite, 24));
  canvas.drawCircle(cx, cy, 13, scale565(steelDark, 82));
  canvas.drawCircle(cx - 2, cy - 2, 9, lighten565(steelMid, 18));
}

template <typename CanvasT>
void drawBall(CanvasT &canvas, int x, int y, uint8_t colorIndex) {
  uint8_t idx = colorIndex % COLOR_COUNT;
  if (ballSpritesReady[idx]) {
    int x0 = x - BALL_SPRITE_SIZE / 2;
    int y0 = y - BALL_SPRITE_SIZE / 2;
    for (int sy = 0; sy < BALL_SPRITE_SIZE; ++sy) {
      for (int sx = 0; sx < BALL_SPRITE_SIZE; ++sx) {
        uint16_t px = ballSprites[idx].readPixel(sx, sy);
        if (px != BALL_SPRITE_KEY) {
          canvas.drawPixel(x0 + sx, y0 + sy, px);
        }
      }
    }
    return;
  }

  uint16_t c = ballColors[idx];
  canvas.fillCircle(x, y, BALL_R, c);
  canvas.fillCircle(x - 3, y - 3, BALL_R / 3, lighten565(c, 75));
  canvas.drawCircle(x, y, BALL_R, scale565(c, 150));
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  uint16_t band = rgb565(12, 22, 26);
  uint16_t edge = rgb565(92, 142, 130);

  canvas.fillRoundRect(8, 292, 180, 24, 8, band);
  canvas.drawRoundRect(8, 292, 180, 24, 8, edge);
  canvas.fillRoundRect(292, 292, 180, 24, 8, band);
  canvas.drawRoundRect(292, 292, 180, 24, 8, edge);

  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(rgb565(190, 230, 218), band);
  canvas.setTextSize(1);
  canvas.drawString("SCORE", 18, 298, 2);
  canvas.setTextColor(TFT_WHITE, band);
  canvas.drawNumber(score, 84, 298, 2);

  canvas.setTextColor(rgb565(190, 230, 218), band);
  canvas.drawString("WAVE", 302, 298, 2);
  canvas.setTextColor(TFT_WHITE, band);
  canvas.drawNumber(wave, 360, 298, 2);

  canvas.setTextColor(rgb565(190, 230, 218), band);
  canvas.drawString("NEXT", 394, 298, 2);
  drawBall(canvas, 456, 304, nextColor);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  uint16_t bg = rgb565(8, 14, 18);
  uint16_t panel = rgb565(18, 34, 40);
  bool showPrompt = ((millis() - titlePulseMs) / 350) % 2 == 0;

  fillBackgroundSplit(canvas, bg);
  for (int y = 0; y < SCREEN_H; y += 24) {
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(15, 28, 34));
  }

  canvas.fillRoundRect(46, 30, 388, 234, 18, panel);
  canvas.drawRoundRect(46, 30, 388, 234, 18, rgb565(85, 145, 126));

  const int idolX = 362;
  const int idolY = 106;
  canvas.fillCircle(idolX, idolY, 46, rgb565(18, 56, 64));
  canvas.fillCircle(idolX, idolY, 34, rgb565(34, 92, 94));
  canvas.fillCircle(idolX, idolY, 23, rgb565(54, 132, 118));
  canvas.fillCircle(idolX, idolY, 11, rgb565(222, 232, 172));
  canvas.fillCircle(idolX - 4, idolY - 4, 5, rgb565(248, 252, 210));
  canvas.fillCircle(idolX + 4, idolY + 5, 3, rgb565(120, 164, 132));
  canvas.fillCircle(idolX - 44, idolY + 4, 10, rgb565(46, 86, 80));
  canvas.fillCircle(idolX + 44, idolY + 4, 10, rgb565(46, 86, 80));
  for (int i = 0; i < 4; ++i) {
    drawBall(canvas, idolX - 78 + i * 24, idolY + 10 + ((i & 1) ? 4 : -4), i);
  }
  canvas.drawCircle(idolX, idolY, 48, rgb565(88, 146, 130));

  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(rgb565(255, 230, 150), panel);
  canvas.setTextSize(4);
  canvas.drawString("ZUMA", SCREEN_W / 2, 78);
  canvas.setTextSize(2);
  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawString("Joystick: Aim", SCREEN_W / 2, 132);
  canvas.drawString("Blue: Shoot / Start", SCREEN_W / 2, 164);
  canvas.drawString("Green: Swap Ball", SCREEN_W / 2, 194);
  canvas.setTextColor(rgb565(190, 226, 235), panel);
  canvas.drawString("Match 3+ to clear", SCREEN_W / 2, 228);

  if (showPrompt) {
    canvas.setTextColor(rgb565(255, 210, 100), bg);
    canvas.drawString("PRESS BLUE", SCREEN_W / 2, 290);
  }
  lastTitlePromptVisible = showPrompt;
}

template <typename CanvasT>
void drawPlaying(CanvasT &canvas) {
  uint16_t bg = rgb565(10, 14, 18);
  fillBackgroundSplit(canvas, bg);
  drawPath(canvas);

  float insertAnimAmount = 0.0f;
  if (insertAnimActive && INSERT_ANIM_FRAMES > 1) {
    float t = (float)insertAnimFrame / (float)(INSERT_ANIM_FRAMES - 1);
    float ease = t * t * (3.0f - 2.0f * t);
    insertAnimAmount = 1.0f - ease;
  }

  for (int i = 0; i < chainCount; ++i) {
    float progress = chainHeadProgress - (float)(chainCount - 1 - i) * BALL_SPACING;
    if (reconnectAnimActive && i < reconnectTailLimit) {
      progress += reconnectOffset;
    }
    if (insertAnimAmount > 0.0f && insertAnimIndex >= 0) {
      int d = i - insertAnimIndex;
      if (d == 0) {
        progress -= 6.0f * insertAnimAmount;
      } else if (d == 1) {
        progress += 6.0f * insertAnimAmount;
      } else if (d == -1) {
        progress -= 6.0f * insertAnimAmount;
      } else if (d == 2) {
        progress += 3.0f * insertAnimAmount;
      } else if (d == -2) {
        progress -= 3.0f * insertAnimAmount;
      }
    }
    if (progress < -BALL_SPACING || progress > pathTotalLen + BALL_SPACING) {
      continue;
    }
    float bx, by, tx, ty;
    pathPos(progress, bx, by, tx, ty);
    if (insertAnimAmount > 0.0f && insertAnimIndex == i) {
      float t = 1.0f - insertAnimAmount;
      bx = insertAnimStartX + (bx - insertAnimStartX) * t;
      by = insertAnimStartY + (by - insertAnimStartY) * t;
    }
    drawBall(canvas, (int)bx, (int)by, chainColors[i]);
  }

  if (shot.active) {
    drawBall(canvas, (int)shot.x, (int)shot.y, shot.color);
  }

  if (popAnimActive && popBallCount > 0 && POP_ANIM_FRAMES > 1) {
    float t = (float)popAnimFrame / (float)POP_ANIM_FRAMES;
    int coreRadius = max(2, (int)((BALL_R - 2) * (1.0f - t * 0.75f)));
    int ringRadius = max(coreRadius + 2, (int)(BALL_R - 1 + t * 8.0f));
    for (int i = 0; i < popBallCount; ++i) {
      uint16_t c = ballColors[popBalls[i].color % COLOR_COUNT];
      uint16_t glow = lighten565(c, 90);
      int x = (int)popBalls[i].x;
      int y = (int)popBalls[i].y;
      canvas.drawCircle(x, y, ringRadius, glow);
      canvas.drawCircle(x, y, max(2, ringRadius - 1), TFT_WHITE);
      canvas.fillCircle(x, y, coreRadius, scale565(c, 180));
      canvas.fillCircle(x - (int)(t * 6.0f), y - (int)(t * 5.0f), max(1, coreRadius / 3), TFT_WHITE);
      for (int p = 0; p < 4; ++p) {
        float angle = p * (PI * 0.5f) + t;
        int px = x + (int)(cosf(angle) * (6.0f + t * 11.0f));
        int py = y + (int)(sinf(angle) * (6.0f + t * 11.0f));
        canvas.fillCircle(px, py, 2, glow);
      }
    }
  }

  canvas.fillCircle((int)shooterX, (int)shooterY, 15, rgb565(56, 86, 82));
  canvas.drawCircle((int)shooterX, (int)shooterY, 19, rgb565(160, 206, 186));
  canvas.drawLine((int)shooterX, (int)shooterY, (int)(shooterX + aimDx * 30.0f), (int)(shooterY + aimDy * 30.0f), rgb565(240, 240, 180));
  drawBall(canvas, (int)shooterX, (int)shooterY, currentColor);

  drawHud(canvas);
}

template <typename CanvasT>
void drawLeaderboard(CanvasT &canvas) {
  static const uint16_t rowColors[LEADERBOARD_SIZE] = {
    0xFEA0,
    0x07FF,
    0xAFE5,
    0xF9C4,
    0xFFE0
  };
  bool showPrompt = ((millis() - boardPulseMs) / 350) % 2 == 0;
  uint16_t bg = rgb565(14, 16, 30);
  uint16_t panel = rgb565(24, 30, 50);
  uint16_t rowBg = rgb565(48, 58, 84);

  fillBackgroundSplit(canvas, bg);
  canvas.fillRoundRect(24, 20, 180, 280, 16, panel);
  canvas.fillRoundRect(214, 20, 242, 280, 16, panel);
  canvas.drawRoundRect(24, 20, 180, 280, 16, rgb565(110, 132, 214));
  canvas.drawRoundRect(214, 20, 242, 280, 16, rgb565(110, 132, 214));

  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(rgb565(255, 165, 165), panel);
  canvas.setTextSize(3);
  canvas.drawString("GAME OVER", 114, 58);
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawString(endReason, 114, 94);
  canvas.setTextColor(rgb565(255, 220, 120), panel);
  canvas.setTextSize(2);
  canvas.drawString("SCORE", 114, 136);
  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawNumber(score, 114, 168, 4);
  canvas.setTextSize(1);
  canvas.setTextColor(rgb565(180, 208, 242), panel);
  canvas.drawString("Wave reached", 114, 218);
  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawNumber(wave, 114, 238, 2);

  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(rgb565(255, 222, 124), panel);
  canvas.setTextSize(2);
  canvas.drawString("LEADERBOARD", 228, 34);
  canvas.setTextColor(rgb565(190, 204, 255), panel);
  canvas.drawString("RANK", 234, 66);
  canvas.drawString("SCORE", 336, 66);

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    int y = 98 + i * 36;
    uint16_t rowColor = rowColors[i];
    uint16_t fill = (i == latestRank) ? scale565(rowColor, 160) : rowBg;
    uint16_t text = (i == latestRank) ? lighten565(rowColor, 100) : TFT_WHITE;
    char rankLabel[4];
    snprintf(rankLabel, sizeof(rankLabel), "%d.", i + 1);

    canvas.fillRoundRect(228, y - 4, 212, 27, 8, fill);
    canvas.setTextColor(text, fill);
    canvas.setTextSize(2);
    canvas.drawString(rankLabel, 242, y);
    canvas.setTextDatum(TR_DATUM);
    canvas.drawNumber(leaderboard[i], 430, y);
    canvas.setTextDatum(TL_DATUM);

    if (i == latestRank) {
      canvas.fillTriangle(218, y + 9, 232, y + 1, 232, y + 17, rowColor);
      canvas.fillCircle(438, y + 9, 5, lighten565(rowColor, 120));
    }
  }

  if (showPrompt) {
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextSize(2);
    canvas.setTextColor(rgb565(255, 220, 120), bg);
    canvas.drawString("BLUE TO RESTART", SCREEN_W / 2, 292);
  }
  lastBoardPromptVisible = showPrompt;
}

void renderTitle() {
  if (frameBufferReady) {
    drawTitle(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawTitle(tft);
  }
}

void renderPlaying() {
  if (frameBufferReady) {
    drawPlaying(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawPlaying(tft);
  }
}

void renderLeaderboard() {
  if (frameBufferReady) {
    drawLeaderboard(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawLeaderboard(tft);
  }
}

void setup() {
  randomSeed(esp_random());

  pinMode(x_pin, INPUT);
  pinMode(y_pin, INPUT);
  pinMode(green_btn_pin, INPUT_PULLUP);
  pinMode(blue_btn_pin, INPUT_PULLUP);
  pinMode(green_led_pin, OUTPUT);
  pinMode(blue_led_pin, OUTPUT);
  digitalWrite(green_led_pin, HIGH);
  digitalWrite(blue_led_pin, HIGH);

  ledcSetup(reverse_ch, motor_frequency, motor_resolution);
  ledcSetup(forward_ch, motor_frequency, motor_resolution);
  ledcAttachPin(reverse_pin, reverse_ch);
  ledcAttachPin(forward_pin, forward_ch);
  stopMotor();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);
  setupAudio();

  computePathLength();
  initFrameBuffer();
  initBallSprites();

  titlePulseMs = millis();
  renderTitle();
  lastRenderedState = TITLE;
}

void loop() {
  readButtons();
  unsigned long now = millis();

  if (gameState == TITLE) {
    if (bluePressed) {
      beginRun();
    }
  } else if (gameState == PLAYING) {
    if (now - lastUpdateMs >= UPDATE_MS) {
      lastUpdateMs = now;
      updatePlaying();
    }
  } else if (gameState == LEADERBOARD) {
    if (bluePressed) {
      titlePulseMs = millis();
      gameState = TITLE;
      lastRenderedState = LEADERBOARD;
    }
  }

  updateVibration();

  if (gameState == TITLE) {
    bool promptVisible = ((now - titlePulseMs) / 350) % 2 == 0;
    if (lastRenderedState != TITLE || promptVisible != lastTitlePromptVisible) {
      renderTitle();
      lastRenderedState = TITLE;
    }
  } else if (gameState == PLAYING) {
    renderPlaying();
    lastRenderedState = PLAYING;
  } else if (gameState == LEADERBOARD) {
    bool promptVisible = ((now - boardPulseMs) / 350) % 2 == 0;
    if (lastRenderedState != LEADERBOARD || promptVisible != lastBoardPromptVisible) {
      renderLeaderboard();
      lastRenderedState = LEADERBOARD;
    }
  }

  delay(1);
}
