#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

const int x_pin = 1;
const int y_pin = 2;
const int green_btn_pin = 11;
const int blue_btn_pin = 12;
const int motor_pin = 45;
const int motor_frequency = 15000;
const int motor_ch = 0;
const int motor_resolution = 8;
const int audio_lrc_pin = 13;
const int audio_bclk_pin = 14;
const int audio_din_pin = 4;

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBuffer = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int BANNER_H = 34;
static const int TABLE_X = 8;
static const int TABLE_Y = BANNER_H + 4;
static const int TABLE_W = SCREEN_W - 16;
static const int TABLE_H = SCREEN_H - TABLE_Y - 8;
static const int CUSHION = 12;
static const int FELT_X = TABLE_X + CUSHION;
static const int FELT_Y = TABLE_Y + CUSHION;
static const int FELT_W = TABLE_W - CUSHION * 2;
static const int FELT_H = TABLE_H - CUSHION * 2;
static const float BALL_R = 8.0f;
static const float POCKET_R = 14.0f;
static const int MAX_BALLS = 7;
static const int LEADERBOARD_SIZE = 5;
static const unsigned long STEP_MS = 16;
static const unsigned long GAME_OVER_SPLASH_MS = 1200;
static const int MAX_SHOTS = 12;
static const int SAMPLE_RATE = 16000;
static const int AUDIO_CHUNK_SAMPLES = 128;
static constexpr float FRICTION = 0.9895f;
static constexpr float MIN_SPEED = 0.05f;
static constexpr float MAX_SPEED = 7.2f;
static constexpr float CHARGE_RATE = 0.045f;
static constexpr float MAX_CHARGE = 1.0f;

enum GameState {
  TITLE,
  PLAYING,
  GAME_OVER_SPLASH,
  LEADERBOARD
};

struct Ball {
  bool active;
  bool cue;
  float x;
  float y;
  float vx;
  float vy;
  uint16_t color;
  uint8_t label;
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

Ball balls[MAX_BALLS];
int leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int latestRank = -1;
int score = 0;
int pocketedThisShot = 0;
int shotsUsed = 0;
int remainingTargets = 0;
float aimAngle = 0.0f;
float shotCharge = 0.0f;
bool charging = false;
bool scoreSubmitted = false;
bool frameBufferReady = false;
bool needCueRespawn = false;
bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
bool blueReleased = false;
int latestX = 2048;
int latestY = 2048;
unsigned long lastStepMs = 0;
unsigned long stateTimerMs = 0;
bool vibrating = false;
unsigned long vibEndMs = 0;
bool audioReady = false;
Voice sfxVoices[4];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t audioTaskHandle = nullptr;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];

float pocketX[6];
float pocketY[6];

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

uint16_t mix565(uint16_t a, uint16_t b, uint8_t amount) {
  uint8_t ar = (((a >> 11) & 0x1F) * 255) / 31;
  uint8_t ag = (((a >> 5) & 0x3F) * 255) / 63;
  uint8_t ab = ((a & 0x1F) * 255) / 31;
  uint8_t br = (((b >> 11) & 0x1F) * 255) / 31;
  uint8_t bg = (((b >> 5) & 0x3F) * 255) / 63;
  uint8_t bb = ((b & 0x1F) * 255) / 31;
  uint8_t r = (uint8_t)((ar * (255 - amount) + br * amount) / 255);
  uint8_t g = (uint8_t)((ag * (255 - amount) + bg * amount) / 255);
  uint8_t b2 = (uint8_t)((ab * (255 - amount) + bb * amount) / 255);
  return rgb565(r, g, b2);
}

template <typename CanvasT>
void setTextDefaults(CanvasT &canvas) {
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextPadding(0);
}

void stopMotor() {
  ledcWrite(motor_ch, 0);
}

void triggerVibration(uint16_t durationMs) {
  vibrating = true;
  vibEndMs = millis() + durationMs;
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
  ledcWrite(motor_ch, 150);
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
  if (preferredChannel >= 0 && preferredChannel < 4) {
    assignVoice(sfxVoices[preferredChannel], freq, durationSamples, volume, wave);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }

  for (int i = 0; i < 4; ++i) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }

  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxGameOver() {
  startVoice(659.0f, 90, 0.07f, 1, 0);
  startVoice(523.0f, 120, 0.07f, 1, 1);
  startVoice(392.0f, 220, 0.08f, 1, 2);
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
    s = sinf(voice.phase * 6.2831853f) * voice.volume;
  } else {
    s = ((float)random(-1000, 1001) / 1000.0f) * voice.volume;
  }

  voice.phase += voice.freq / SAMPLE_RATE;
  if (voice.phase >= 1.0f) voice.phase -= 1.0f;
  voice.remainSamples--;
  if (voice.remainSamples <= 0) voice.active = false;
  return s;
}

void audioTick() {
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    float mix = 0.0f;
    taskENTER_CRITICAL(&audioMux);
    for (int v = 0; v < 4; ++v) {
      mix += sampleVoice(sfxVoices[v]);
    }
    taskEXIT_CRITICAL(&audioMux);

    if (mix > 0.75f) mix = 0.75f;
    if (mix < -0.75f) mix = -0.75f;
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

void initFrameBuffer() {
  frameBuffer.setColorDepth(16);
  frameBufferReady = frameBuffer.createSprite(SCREEN_W, SCREEN_H);
  if (!frameBufferReady) {
    frameBuffer.deleteSprite();
    frameBuffer.setColorDepth(8);
    frameBufferReady = frameBuffer.createSprite(SCREEN_W, SCREEN_H);
  }
}

void updateButtons() {
  bool greenNow = digitalRead(green_btn_pin) == LOW;
  bool blueNow = digitalRead(blue_btn_pin) == LOW;
  greenPressed = greenNow && !greenDown;
  bluePressed = blueNow && !blueDown;
  blueReleased = !blueNow && blueDown;
  greenDown = greenNow;
  blueDown = blueNow;
}

void readJoystick() {
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);
}

float joystickAxis(int raw) {
  float v = ((float)raw - 2048.0f) / 2048.0f;
  if (fabsf(v) < 0.10f) return 0.0f;
  if (v > 1.0f) v = 1.0f;
  if (v < -1.0f) v = -1.0f;
  return v;
}

bool allBallsStopped() {
  for (int i = 0; i < MAX_BALLS; ++i) {
    if (!balls[i].active) continue;
    if (fabsf(balls[i].vx) > MIN_SPEED || fabsf(balls[i].vy) > MIN_SPEED) {
      return false;
    }
  }
  return true;
}

Ball *cueBall() {
  return &balls[0];
}

void resetBalls() {
  uint16_t colors[MAX_BALLS] = {
    TFT_WHITE,
    rgb565(244, 210, 64),
    rgb565(74, 142, 255),
    rgb565(230, 84, 74),
    rgb565(166, 86, 220),
    rgb565(255, 146, 42),
    rgb565(26, 26, 36)
  };

  for (int i = 0; i < MAX_BALLS; ++i) {
    balls[i].active = true;
    balls[i].cue = (i == 0);
    balls[i].vx = 0.0f;
    balls[i].vy = 0.0f;
    balls[i].color = colors[i];
    balls[i].label = i;
  }

  balls[0].x = FELT_X + FELT_W * 0.24f;
  balls[0].y = FELT_Y + FELT_H * 0.5f;

  float rackX = FELT_X + FELT_W * 0.72f;
  float rackY = FELT_Y + FELT_H * 0.5f;
  float dx = BALL_R * 2.1f;
  float dy = BALL_R * 1.25f;

  balls[1].x = rackX;
  balls[1].y = rackY;

  balls[2].x = rackX + dx;
  balls[2].y = rackY - dy;
  balls[3].x = rackX + dx;
  balls[3].y = rackY + dy;

  balls[4].x = rackX + dx * 2.0f;
  balls[4].y = rackY - dy * 2.0f;
  balls[5].x = rackX + dx * 2.0f;
  balls[5].y = rackY;
  balls[6].x = rackX + dx * 2.0f;
  balls[6].y = rackY + dy * 2.0f;
}

void initPockets() {
  pocketX[0] = FELT_X;
  pocketY[0] = FELT_Y;
  pocketX[1] = FELT_X + FELT_W * 0.5f;
  pocketY[1] = FELT_Y - 1.0f;
  pocketX[2] = FELT_X + FELT_W;
  pocketY[2] = FELT_Y;
  pocketX[3] = FELT_X;
  pocketY[3] = FELT_Y + FELT_H;
  pocketX[4] = FELT_X + FELT_W * 0.5f;
  pocketY[4] = FELT_Y + FELT_H + 1.0f;
  pocketX[5] = FELT_X + FELT_W;
  pocketY[5] = FELT_Y + FELT_H;
}

void beginGame() {
  score = 0;
  pocketedThisShot = 0;
  shotsUsed = 0;
  remainingTargets = MAX_BALLS - 1;
  aimAngle = 0.0f;
  shotCharge = 0.0f;
  charging = false;
  scoreSubmitted = false;
  needCueRespawn = false;
  latestRank = -1;
  resetBalls();
  gameState = PLAYING;
  lastStepMs = millis();
}

void submitScore() {
  if (scoreSubmitted) return;

  int insertAt = LEADERBOARD_SIZE - 1;
  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    if (score > leaderboard[i]) {
      insertAt = i;
      break;
    }
  }

  for (int i = LEADERBOARD_SIZE - 1; i > insertAt; --i) {
    leaderboard[i] = leaderboard[i - 1];
  }
  leaderboard[insertAt] = score;
  latestRank = insertAt;
  scoreSubmitted = true;
}

void enterGameOver() {
  submitScore();
  playSfxGameOver();
  gameState = GAME_OVER_SPLASH;
  stateTimerMs = millis();
}

void respawnCueBall() {
  Ball *cue = cueBall();
  cue->active = true;
  cue->x = FELT_X + FELT_W * 0.24f;
  cue->y = FELT_Y + FELT_H * 0.5f;
  cue->vx = 0.0f;
  cue->vy = 0.0f;
  needCueRespawn = false;
}

void resolveWallCollision(Ball &b) {
  float left = FELT_X + BALL_R;
  float right = FELT_X + FELT_W - BALL_R;
  float top = FELT_Y + BALL_R;
  float bottom = FELT_Y + FELT_H - BALL_R;

  if (b.x < left) {
    b.x = left;
    b.vx = -b.vx * 0.92f;
  } else if (b.x > right) {
    b.x = right;
    b.vx = -b.vx * 0.92f;
  }

  if (b.y < top) {
    b.y = top;
    b.vy = -b.vy * 0.92f;
  } else if (b.y > bottom) {
    b.y = bottom;
    b.vy = -b.vy * 0.92f;
  }
}

void handlePocket(Ball &b) {
  for (int i = 0; i < 6; ++i) {
    float dx = b.x - pocketX[i];
    float dy = b.y - pocketY[i];
    if (dx * dx + dy * dy > POCKET_R * POCKET_R) continue;

    if (b.cue) {
      b.active = false;
      b.vx = 0.0f;
      b.vy = 0.0f;
      score -= 15;
      if (score < 0) score = 0;
      needCueRespawn = true;
      triggerVibration(110);
    } else {
      b.active = false;
      b.vx = 0.0f;
      b.vy = 0.0f;
      ++pocketedThisShot;
      --remainingTargets;
      int bonus = (b.label == 6) ? 45 : 25;
      score += bonus + (pocketedThisShot - 1) * 10;
      triggerVibration(110);
    }
    return;
  }
}

void resolveBallCollision(Ball &a, Ball &b) {
  float dx = b.x - a.x;
  float dy = b.y - a.y;
  float distSq = dx * dx + dy * dy;
  float minDist = BALL_R * 2.0f;
  if (distSq <= 0.0001f || distSq >= minDist * minDist) return;

  float dist = sqrtf(distSq);
  float nx = dx / dist;
  float ny = dy / dist;
  float overlap = minDist - dist;
  a.x -= nx * overlap * 0.5f;
  a.y -= ny * overlap * 0.5f;
  b.x += nx * overlap * 0.5f;
  b.y += ny * overlap * 0.5f;

  float relVx = a.vx - b.vx;
  float relVy = a.vy - b.vy;
  float speedAlongNormal = relVx * nx + relVy * ny;
  if (speedAlongNormal <= 0.0f) return;

  float impulse = speedAlongNormal;
  a.vx -= impulse * nx;
  a.vy -= impulse * ny;
  b.vx += impulse * nx;
  b.vy += impulse * ny;

  a.vx *= 0.985f;
  a.vy *= 0.985f;
  b.vx *= 0.985f;
  b.vy *= 0.985f;
}

void takeShot() {
  Ball *cue = cueBall();
  if (!cue->active || !allBallsStopped()) return;
  if (shotCharge < 0.08f) return;

  cue->vx = cosf(aimAngle) * MAX_SPEED * shotCharge;
  cue->vy = sinf(aimAngle) * MAX_SPEED * shotCharge;
  shotCharge = 0.0f;
  charging = false;
  pocketedThisShot = 0;
  ++shotsUsed;
}

void updateAimFromJoystick() {
  float ax = joystickAxis(latestX);
  float ay = joystickAxis(latestY);
  if (fabsf(ax) > 0.01f || fabsf(ay) > 0.01f) {
    aimAngle = atan2f(ay, ax);
  }
}

void updatePhysics() {
  for (int i = 0; i < MAX_BALLS; ++i) {
    Ball &b = balls[i];
    if (!b.active) continue;
    b.x += b.vx;
    b.y += b.vy;
    b.vx *= FRICTION;
    b.vy *= FRICTION;
    if (fabsf(b.vx) < MIN_SPEED) b.vx = 0.0f;
    if (fabsf(b.vy) < MIN_SPEED) b.vy = 0.0f;
    resolveWallCollision(b);
    handlePocket(b);
  }

  for (int i = 0; i < MAX_BALLS; ++i) {
    if (!balls[i].active) continue;
    for (int j = i + 1; j < MAX_BALLS; ++j) {
      if (!balls[j].active) continue;
      resolveBallCollision(balls[i], balls[j]);
    }
  }

  if (allBallsStopped() && needCueRespawn) {
    respawnCueBall();
  }

  if (remainingTargets <= 0) {
    score += max(0, (MAX_SHOTS - shotsUsed) * 6);
    enterGameOver();
    return;
  }

  if (allBallsStopped() && shotsUsed >= MAX_SHOTS) {
    enterGameOver();
  }
}

template <typename CanvasT>
void drawBall(CanvasT &canvas, const Ball &b) {
  if (!b.active) return;
  uint16_t shadow = rgb565(10, 20, 16);
  uint16_t shade = mix565(b.color, TFT_BLACK, 96);
  uint16_t mid = mix565(b.color, TFT_WHITE, 48);
  canvas.fillCircle((int)b.x + 2, (int)b.y + 2, (int)BALL_R + 1, shadow);
  canvas.fillCircle((int)b.x, (int)b.y, (int)BALL_R, shade);
  canvas.fillCircle((int)b.x - 1, (int)b.y - 1, (int)BALL_R - 2, b.color);
  canvas.fillCircle((int)b.x - 2, (int)b.y - 2, (int)BALL_R - 5, mid);
  canvas.fillCircle((int)b.x - 3, (int)b.y - 3, 2, TFT_WHITE);
}

template <typename CanvasT>
void drawPocketedBallIcon(CanvasT &canvas, int cx, int cy, uint16_t color) {
  canvas.fillCircle(cx + 1, cy + 1, 5, rgb565(10, 14, 18));
  canvas.fillCircle(cx, cy, 5, color);
  canvas.fillCircle(cx - 2, cy - 2, 1, TFT_WHITE);
}

template <typename CanvasT>
void drawTitleBall(CanvasT &canvas, int x, int y, int r, uint16_t color) {
  canvas.fillCircle(x + 3, y + 3, r + 1, rgb565(8, 12, 14));
  canvas.fillCircle(x, y, r, mix565(color, TFT_BLACK, 100));
  canvas.fillCircle(x - 1, y - 1, r - 2, color);
  canvas.fillCircle(x - 3, y - 3, r / 3, mix565(color, TFT_WHITE, 120));
  canvas.fillCircle(x - 5, y - 5, 2, TFT_WHITE);
}

template <typename CanvasT>
void drawBanner(CanvasT &canvas) {
  uint16_t bg = rgb565(18, 26, 36);
  uint16_t panel = rgb565(36, 50, 62);
  uint16_t accent = rgb565(246, 212, 92);
  char buf[48];

  canvas.fillRect(0, 0, SCREEN_W, BANNER_H, bg);
  canvas.fillRect(8, 6, 128, 22, panel);
  canvas.fillRect(146, 6, 126, 22, panel);
  canvas.fillRect(282, 6, 190, 22, panel);

  canvas.setTextColor(accent, panel);
  snprintf(buf, sizeof(buf), "Score %d", score);
  canvas.drawString(buf, 18, 11, 2);

  canvas.setTextColor(TFT_WHITE, panel);
  snprintf(buf, sizeof(buf), "Shots %d/%d", shotsUsed, MAX_SHOTS);
  canvas.drawString(buf, 156, 11, 2);

  int iconX = 295;
  for (int i = 1; i < MAX_BALLS; ++i) {
    if (balls[i].active) continue;
    drawPocketedBallIcon(canvas, iconX, 17, balls[i].color);
    iconX += 15;
  }
}

template <typename CanvasT>
void drawTable(CanvasT &canvas) {
  uint16_t wood = rgb565(110, 68, 32);
  uint16_t woodDark = rgb565(70, 40, 18);
  uint16_t felt = rgb565(18, 106, 68);
  uint16_t feltShade = rgb565(10, 76, 48);
  uint16_t rail = rgb565(154, 114, 66);

  canvas.fillScreen(rgb565(6, 10, 14));
  drawBanner(canvas);
  canvas.fillRoundRect(TABLE_X, TABLE_Y, TABLE_W, TABLE_H, 12, wood);
  canvas.drawRoundRect(TABLE_X, TABLE_Y, TABLE_W, TABLE_H, 12, woodDark);
  canvas.fillRoundRect(TABLE_X + 4, TABLE_Y + 4, TABLE_W - 8, TABLE_H - 8, 10, rail);
  canvas.fillRoundRect(FELT_X, FELT_Y, FELT_W, FELT_H, 10, felt);

  for (int i = 0; i < 8; ++i) {
    int y = FELT_Y + 10 + i * 32;
    canvas.drawFastHLine(FELT_X + 12, y, FELT_W - 24, feltShade);
  }
  for (int i = 0; i < 6; ++i) {
    canvas.fillCircle((int)pocketX[i], (int)pocketY[i], (int)POCKET_R, rgb565(18, 18, 18));
  }

  uint16_t line = rgb565(198, 236, 226);
  int footX = FELT_X + FELT_W * 3 / 4;
  canvas.drawFastVLine(footX, FELT_Y + 18, FELT_H - 36, line);
  canvas.drawCircle(FELT_X + FELT_W / 4, FELT_Y + FELT_H / 2, 36, line);
}

template <typename CanvasT>
void drawAimGuide(CanvasT &canvas) {
  Ball *cue = cueBall();
  if (!cue->active || !allBallsStopped()) return;

  int lineLen = 54 + (int)(shotCharge * 60.0f);
  int x0 = (int)cue->x;
  int y0 = (int)cue->y;
  int x1 = x0 + (int)(cosf(aimAngle) * lineLen);
  int y1 = y0 + (int)(sinf(aimAngle) * lineLen);
  canvas.drawLine(x0, y0, x1, y1, rgb565(250, 242, 212));
  canvas.fillCircle(x1, y1, 3, rgb565(250, 242, 212));

  int backLen = 16 + (int)(shotCharge * 28.0f);
  int cx = x0 - (int)(cosf(aimAngle) * backLen);
  int cy = y0 - (int)(sinf(aimAngle) * backLen);
  canvas.drawLine(cx, cy, x0 - (int)(cosf(aimAngle) * 6), y0 - (int)(sinf(aimAngle) * 6), rgb565(170, 112, 54));

  float px = (float)x1;
  float py = (float)y1;
  float dx = cosf(aimAngle);
  float dy = sinf(aimAngle);
  float left = FELT_X + BALL_R;
  float right = FELT_X + FELT_W - BALL_R;
  float top = FELT_Y + BALL_R;
  float bottom = FELT_Y + FELT_H - BALL_R;
  uint16_t guide = TFT_CYAN;

  for (int dot = 0; dot < 26; ++dot) {
    float nearestBallT = 9999.0f;
    for (int i = 1; i < MAX_BALLS; ++i) {
      if (!balls[i].active) continue;
      float ox = balls[i].x - px;
      float oy = balls[i].y - py;
      float proj = ox * dx + oy * dy;
      if (proj <= 0.0f) continue;
      float perpSq = ox * ox + oy * oy - proj * proj;
      float hitRadius = BALL_R * 2.0f;
      if (perpSq > hitRadius * hitRadius) continue;
      float thc = sqrtf(hitRadius * hitRadius - perpSq);
      float hitT = proj - thc;
      if (hitT > 0.0f && hitT < nearestBallT) nearestBallT = hitT;
    }

    float wallT = 9999.0f;
    bool hitVertical = false;
    if (dx > 0.001f) {
      float t = (right - px) / dx;
      if (t > 0.0f && t < wallT) {
        wallT = t;
        hitVertical = true;
      }
    } else if (dx < -0.001f) {
      float t = (left - px) / dx;
      if (t > 0.0f && t < wallT) {
        wallT = t;
        hitVertical = true;
      }
    }
    if (dy > 0.001f) {
      float t = (bottom - py) / dy;
      if (t > 0.0f && t < wallT) {
        wallT = t;
        hitVertical = false;
      }
    } else if (dy < -0.001f) {
      float t = (top - py) / dy;
      if (t > 0.0f && t < wallT) {
        wallT = t;
        hitVertical = false;
      }
    }

    float stepLen = 14.0f;
    bool hitBall = nearestBallT < stepLen;
    bool hitWall = wallT < stepLen;
    if (hitBall) stepLen = nearestBallT;
    if (hitWall && wallT < stepLen) stepLen = wallT;
    if (stepLen <= 1.2f) break;

    float dotX = px + dx * stepLen;
    float dotY = py + dy * stepLen;
    canvas.fillCircle((int)dotX, (int)dotY, 1, guide);

    if (hitBall) {
      canvas.drawCircle((int)dotX, (int)dotY, 4, guide);
      break;
    }

    if (hitWall) {
      px = dotX;
      py = dotY;
      if (hitVertical) {
        dx = -dx;
      } else {
        dy = -dy;
      }
      px += dx * 3.0f;
      py += dy * 3.0f;
    } else {
      px = dotX;
      py = dotY;
    }
  }
}

template <typename CanvasT>
void drawPlaying(CanvasT &canvas) {
  drawTable(canvas);
  for (int i = 0; i < MAX_BALLS; ++i) {
    drawBall(canvas, balls[i]);
  }
  drawAimGuide(canvas);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  uint16_t bg = rgb565(10, 18, 24);
  uint16_t panel = rgb565(22, 62, 48);
  uint16_t gold = rgb565(246, 212, 92);
  uint16_t felt = rgb565(18, 106, 68);
  uint16_t rail = rgb565(160, 118, 68);
  uint16_t railDark = rgb565(90, 54, 24);
  uint16_t line = rgb565(210, 236, 228);
  const int titleTableX = 62;
  const int titleTableY = 60;
  const int titleTableW = SCREEN_W - 124;
  const int titleTableH = 132;
  canvas.fillScreen(bg);
  canvas.fillRoundRect(28, 24, SCREEN_W - 56, SCREEN_H - 48, 18, panel);
  canvas.fillRoundRect(42, 40, SCREEN_W - 84, 172, 18, railDark);
  canvas.fillRoundRect(50, 48, SCREEN_W - 100, 156, 14, rail);
  canvas.fillRoundRect(titleTableX, titleTableY, titleTableW, titleTableH, 12, felt);

  canvas.drawFastVLine(360, 74, 120, line);
  canvas.drawCircle(172, 134, 26, line);

  canvas.fillCircle(titleTableX, titleTableY, 12, rgb565(18, 18, 18));
  canvas.fillCircle(titleTableX + titleTableW / 2, titleTableY - 1, 12, rgb565(18, 18, 18));
  canvas.fillCircle(titleTableX + titleTableW, titleTableY, 12, rgb565(18, 18, 18));
  canvas.fillCircle(titleTableX, titleTableY + titleTableH, 12, rgb565(18, 18, 18));
  canvas.fillCircle(titleTableX + titleTableW / 2, titleTableY + titleTableH + 1, 12, rgb565(18, 18, 18));
  canvas.fillCircle(titleTableX + titleTableW, titleTableY + titleTableH, 12, rgb565(18, 18, 18));

  drawTitleBall(canvas, 168, 128, 10, TFT_WHITE);
  drawTitleBall(canvas, 286, 126, 10, rgb565(244, 210, 64));
  drawTitleBall(canvas, 316, 152, 10, rgb565(230, 84, 74));

  uint16_t cue = rgb565(182, 128, 70);
  uint16_t cueTip = rgb565(76, 142, 224);
  canvas.drawLine(108, 176, 252, 145, cue);
  canvas.drawLine(108, 177, 252, 146, cue);
  canvas.drawLine(109, 178, 253, 147, cue);
  canvas.drawLine(247, 146, 256, 144, cueTip);
  canvas.drawLine(247, 147, 256, 145, cueTip);

  canvas.setTextColor(gold);
  canvas.drawCentreString("BILLIARDS", SCREEN_W / 2, 216, 6);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawCentreString("Clear the rack before shots run out", SCREEN_W / 2, 250, 2);
  canvas.drawCentreString("Blue Start   Green Reset Scores", SCREEN_W / 2, 270, 2);
}

template <typename CanvasT>
void drawGameOverSplash(CanvasT &canvas) {
  uint16_t bg = rgb565(36, 14, 10);
  canvas.fillRect(0, 0, 320, SCREEN_H, bg);
  canvas.fillRect(320, 0, SCREEN_W - 320, SCREEN_H, bg);
  canvas.setTextColor(rgb565(255, 210, 186), bg);
  canvas.drawCentreString("RACK OVER", SCREEN_W / 2, 112, 6);
  canvas.setTextColor(rgb565(255, 236, 198), bg);
  canvas.drawCentreString("Score submitted to the board", SCREEN_W / 2, 172, 2);
  char buf[32];
  snprintf(buf, sizeof(buf), "Score %d", score);
  canvas.drawCentreString(buf, SCREEN_W / 2, 204, 4);
}

template <typename CanvasT>
void drawLeaderboard(CanvasT &canvas) {
  uint16_t bg = rgb565(16, 18, 30);
  uint16_t rowBg = rgb565(48, 54, 76);
  uint16_t title = rgb565(246, 212, 90);
  char buf[48];

  canvas.fillRect(0, 0, 320, SCREEN_H, bg);
  canvas.fillRect(320, 0, SCREEN_W - 320, SCREEN_H, bg);
  canvas.fillRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, bg);
  canvas.drawRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, rgb565(120, 150, 220));
  canvas.setTextColor(title, bg);
  canvas.drawCentreString("POOL BOARD", SCREEN_W / 2, 48, 4);
  canvas.setTextColor(TFT_WHITE, bg);
  snprintf(buf, sizeof(buf), "This Run %d   Cleared %d", score, (MAX_BALLS - 1) - remainingTargets);
  canvas.drawCentreString(buf, SCREEN_W / 2, 80, 2);

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    int y = 112 + i * 28;
    uint16_t fillColor = (i == latestRank) ? rgb565(84, 66, 26) : rowBg;
    uint16_t textColor = (i == latestRank) ? rgb565(250, 224, 120) : TFT_WHITE;
    canvas.fillRoundRect(92, y - 8, 296, 24, 6, fillColor);
    canvas.setTextColor(textColor, fillColor);
    snprintf(buf, sizeof(buf), "%d. %d pts", i + 1, leaderboard[i]);
    canvas.drawString(buf, 148, y - 4, 2);
    if (i == latestRank) {
      canvas.fillTriangle(344, y + 4, 360, y - 4, 360, y + 12, rgb565(250, 210, 92));
      canvas.fillCircle(328, y + 4, 6, rgb565(250, 210, 92));
    }
  }

  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawString("Blue Retry", 130, 286, 2);
  canvas.drawString("Green Title", 310, 286, 2);
}

template <typename CanvasT>
void renderState(CanvasT &canvas) {
  setTextDefaults(canvas);
  switch (gameState) {
    case TITLE:
      drawTitle(canvas);
      break;
    case PLAYING:
      drawPlaying(canvas);
      break;
    case GAME_OVER_SPLASH:
      drawGameOverSplash(canvas);
      break;
    case LEADERBOARD:
      drawLeaderboard(canvas);
      break;
  }
}

void render() {
  if (frameBufferReady) {
    renderState(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    renderState(tft);
  }
}

void updateTitle() {
  if (greenPressed) {
    for (int i = 0; i < LEADERBOARD_SIZE; ++i) leaderboard[i] = 0;
    latestRank = -1;
  }
  if (bluePressed) beginGame();
}

void updatePlaying() {
  readJoystick();
  if (allBallsStopped()) {
    updateAimFromJoystick();
    if (bluePressed && !needCueRespawn) {
      charging = true;
      shotCharge = 0.08f;
    }
    if (charging && blueDown) {
      shotCharge += CHARGE_RATE;
      if (shotCharge > MAX_CHARGE) shotCharge = MAX_CHARGE;
    }
    if (charging && blueReleased) {
      takeShot();
    }
  } else {
    charging = false;
    shotCharge = 0.0f;
  }

  unsigned long now = millis();
  while (now - lastStepMs >= STEP_MS && gameState == PLAYING) {
    lastStepMs += STEP_MS;
    updatePhysics();
  }

  if (greenPressed) {
    gameState = TITLE;
  }
}

void updateGameOverSplash() {
  if (millis() - stateTimerMs >= GAME_OVER_SPLASH_MS) {
    gameState = LEADERBOARD;
  }
}

void updateLeaderboardState() {
  if (bluePressed) beginGame();
  if (greenPressed) gameState = TITLE;
}

void setup() {
  pinMode(green_btn_pin, INPUT_PULLUP);
  pinMode(blue_btn_pin, INPUT_PULLUP);
  analogReadResolution(12);
  ledcSetup(motor_ch, motor_frequency, motor_resolution);
  ledcAttachPin(motor_pin, motor_ch);
  stopMotor();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);
  tft.setTextDatum(TL_DATUM);

  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, nullptr);
  if (err == ESP_OK) {
    err = i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);
    if (err == ESP_OK) {
      i2s_zero_dma_buffer(I2S_NUM_1);
      audioReady = true;
      xTaskCreatePinnedToCore(audioTaskMain, "audio_task", 4096, nullptr, 2, &audioTaskHandle, 0);
    }
  }

  initFrameBuffer();
  initPockets();
  resetBalls();
}

void loop() {
  updateButtons();

  switch (gameState) {
    case TITLE:
      updateTitle();
      break;
    case PLAYING:
      updatePlaying();
      break;
    case GAME_OVER_SPLASH:
      updateGameOverSplash();
      break;
    case LEADERBOARD:
      updateLeaderboardState();
      break;
  }

  updateVibration();
  render();
  delay(1);
}
