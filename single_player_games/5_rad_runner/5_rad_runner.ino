#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>

const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int JOY_X_PIN = 1;
const int JOY_Y_PIN = 2;
const int GREEN_BTN_PIN = 11;
const int BLUE_BTN_PIN = 12;
const int ROAD_HORIZON_Y = 86;
const uint32_t FRAME_MS = 33;
const int LEADERBOARD_SIZE = 5;
const float BASE_SPEED = 1.15f;
const int reverse_pin = 45;
const int forward_pin = 46;
const int motor_frequency = 15000;
const int reverse_ch = 0;
const int forward_ch = 1;
const int motor_resolution = 8;
#define MAX98357_LRC 13
#define MAX98357_BCLK 14
#define MAX98357_DIN 4
const int SAMPLE_RATE = 16000;
const int AUDIO_CHUNK_SAMPLES = 256;

struct TrafficCar {
  float lane;
  float depth;
  float speedBias;
  uint16_t color;
};

struct ScreenCarRect {
  int x;
  int y;
  int w;
  int h;
};

struct ScoreEntry {
  float km;
  uint16_t color;
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

enum GameMode {
  MODE_TITLE,
  MODE_PLAYING,
  MODE_EXPLODING,
  MODE_LEADERBOARD
};

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBuffer = TFT_eSprite(&tft);

bool frameBufferReady = false;
uint32_t lastFrameMs = 0;
float distanceMeters = 0.0f;
float speedValue = 0.55f;
float roadCurve = 0.0f;
float roadCurveTarget = 0.0f;
float roadCurveBlend = 0.0f;
float playerOffset = 0.0f;
uint32_t scoreValue = 0;
int flashFrames = 0;
TrafficCar trafficCars[5];
GameMode gameMode = MODE_TITLE;
GameMode lastRenderedMode = MODE_LEADERBOARD;
uint32_t explosionStartMs = 0;
int explosionFrame = 0;
float lastRunKm = 0.0f;
int leaderboardHighlight = -1;
ScoreEntry leaderboard[LEADERBOARD_SIZE];
bool lastBluePressed = false;
bool lastGreenPressed = false;
bool vibrating = false;
bool vibForward = true;
uint32_t vibEndMs = 0;
uint32_t vibLastToggleMs = 0;
Voice sfxVoices[3];
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
  .dma_buf_len = AUDIO_CHUNK_SAMPLES
};

const i2s_pin_config_t i2sOut_pin_config = {
  .bck_io_num = MAX98357_BCLK,
  .ws_io_num = MAX98357_LRC,
  .data_out_num = MAX98357_DIN,
  .data_in_num = -1
};

const Note bgmLoop[] = {
  {82, 520}, {0, 40}, {98, 520}, {0, 40},
  {74, 520}, {0, 40}, {98, 520}, {0, 40},
  {82, 680}, {0, 60}, {110, 520}, {0, 60}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

uint16_t mix565(uint16_t colorA, uint16_t colorB, uint8_t amount) {
  uint8_t r1 = (colorA >> 11) & 0x1F;
  uint8_t g1 = (colorA >> 5) & 0x3F;
  uint8_t b1 = colorA & 0x1F;
  uint8_t r2 = (colorB >> 11) & 0x1F;
  uint8_t g2 = (colorB >> 5) & 0x3F;
  uint8_t b2 = colorB & 0x1F;

  uint8_t r = (r1 * (255 - amount) + r2 * amount) / 255;
  uint8_t g = (g1 * (255 - amount) + g2 * amount) / 255;
  uint8_t b = (b1 * (255 - amount) + b2 * amount) / 255;
  return (r << 11) | (g << 5) | b;
}

uint16_t lighten565(uint16_t color, uint8_t amount) {
  return mix565(color, TFT_WHITE, amount);
}

uint16_t darken565(uint16_t color, uint8_t amount) {
  return mix565(color, TFT_BLACK, amount);
}

ScreenCarRect getTrafficScreenRect(const TrafficCar& car) {
  float p = constrain((car.depth - 0.10f) / 0.95f, 0.0f, 1.0f);
  float z = p * p;
  int center = SCREEN_W / 2 + (int)(roadCurve * z * 110.0f) - (int)(playerOffset * z * 44.0f);
  int roadHalf = 32 + (int)(p * 154.0f);

  ScreenCarRect rect;
  rect.x = center + (int)(car.lane * roadHalf * 0.58f);
  rect.y = ROAD_HORIZON_Y + 8 + (int)(z * (SCREEN_H - ROAD_HORIZON_Y - 24));
  rect.w = 12 + (int)(p * 30.0f);
  rect.h = 8 + (int)(p * 26.0f);
  return rect;
}

bool isBluePressed() {
  return !digitalRead(BLUE_BTN_PIN);
}

bool isGreenPressed() {
  return !digitalRead(GREEN_BTN_PIN);
}

bool bluePressedEdge() {
  bool now = isBluePressed();
  bool pressed = now && !lastBluePressed;
  lastBluePressed = now;
  return pressed;
}

bool greenPressedEdge() {
  bool now = isGreenPressed();
  bool pressed = now && !lastGreenPressed;
  lastGreenPressed = now;
  return pressed;
}

void resetInputLatches() {
  lastBluePressed = isBluePressed();
  lastGreenPressed = isGreenPressed();
}

void seedLeaderboard() {
  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    leaderboard[i].km = 0.0f;
    leaderboard[i].color = tft.color565(90, 98, 126);
  }
}

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startVibration(uint16_t durationMs = 180) {
  vibrating = true;
  vibForward = true;
  vibEndMs = millis() + durationMs;
  vibLastToggleMs = 0;
}

void updateVibration() {
  if (!vibrating) {
    return;
  }

  uint32_t now = millis();
  if (now >= vibEndMs) {
    vibrating = false;
    stopMotor();
    return;
  }

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 35) {
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

void assignVoice(Voice &voice, float freq, int durationSamples, float volume, uint8_t wave) {
  voice.active = true;
  voice.freq = freq;
  voice.phase = 0.0f;
  voice.remainSamples = durationSamples;
  voice.volume = volume;
  voice.wave = wave;
}

void startVoice(float freq, int ms, float volume, uint8_t wave, int channel = -1) {
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  taskENTER_CRITICAL(&audioMux);
  if (channel >= 0 && channel < 3) {
    assignVoice(sfxVoices[channel], freq, durationSamples, volume, wave);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }

  for (int i = 0; i < 3; ++i) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }

  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave);
  taskEXIT_CRITICAL(&audioMux);
}

void playCrashSfx() {
  startVoice(120.0f, 180, 0.18f, 2, 0);
  startVoice(210.0f, 140, 0.16f, 0, 1);
  startVoice(90.0f, 260, 0.12f, 0, 2);
}

float bgmSample() {
  if (gameMode != MODE_PLAYING) {
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

  float s = (bgmPhase < 0.5f) ? 0.0125f : -0.0125f;
  bgmPhase += (float)freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  return s;
}

float waveSample(Voice &voice) {
  float s = 0.0f;
  if (voice.wave == 0) {
    s = (voice.phase < 0.5f) ? 1.0f : -1.0f;
  } else if (voice.wave == 1) {
    s = sinf(voice.phase * 2.0f * PI);
  } else {
    s = ((float)random(-1000, 1000)) / 1000.0f;
  }

  voice.phase += voice.freq / SAMPLE_RATE;
  if (voice.phase >= 1.0f) {
    voice.phase -= 1.0f;
  }
  return s * voice.volume;
}

void audioTick() {
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    float mix = bgmSample();
    taskENTER_CRITICAL(&audioMux);
    for (int v = 0; v < 3; ++v) {
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

    mix = constrain(mix, -0.6f, 0.6f);
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

uint16_t randomTrafficColor() {
  const uint16_t palette[] = {
    TFT_RED,
    TFT_BLUE,
    TFT_YELLOW,
    TFT_CYAN,
    TFT_MAGENTA
  };
  return palette[random(0, 5)];
}

void respawnTrafficCar(int index, float depth) {
  const float lanes[] = {-0.72f, 0.0f, 0.72f};
  trafficCars[index].lane = lanes[random(0, 3)] + random(-12, 13) / 100.0f;
  trafficCars[index].depth = depth;
  trafficCars[index].speedBias = 0.65f + random(0, 70) / 100.0f;
  trafficCars[index].color = randomTrafficColor();
}

void resetGame() {
  lastFrameMs = millis();
  distanceMeters = 0.0f;
  speedValue = BASE_SPEED;
  roadCurve = 0.0f;
  roadCurveTarget = 0.0f;
  roadCurveBlend = 0.0f;
  playerOffset = 0.0f;
  scoreValue = 0;
  flashFrames = 0;
  for (int i = 0; i < 5; ++i) {
    if (i < 2) {
      respawnTrafficCar(i, 0.22f + i * 0.22f);
    } else {
      trafficCars[i].depth = 1.6f + i * 0.2f;
      trafficCars[i].lane = 0.0f;
      trafficCars[i].speedBias = 0.7f;
      trafficCars[i].color = TFT_DARKGREY;
    }
  }
}

void beginRun() {
  resetGame();
  gameMode = MODE_PLAYING;
  lastRenderedMode = MODE_LEADERBOARD;
  leaderboardHighlight = -1;
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  stopMotor();
  resetInputLatches();
}

void recordScore(float km) {
  leaderboardHighlight = -1;
  ScoreEntry incoming;
  incoming.km = km;
  incoming.color = tft.color565(255, 132, 48);

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    if (km > leaderboard[i].km) {
      for (int j = LEADERBOARD_SIZE - 1; j > i; --j) {
        leaderboard[j] = leaderboard[j - 1];
      }
      leaderboard[i] = incoming;
      leaderboardHighlight = i;
      return;
    }
  }

  leaderboard[LEADERBOARD_SIZE - 1] = incoming;
  leaderboardHighlight = LEADERBOARD_SIZE - 1;
}

void triggerCrash() {
  flashFrames = 6;
  lastRunKm = distanceMeters / 1000.0f;
  recordScore(lastRunKm);
  playCrashSfx();
  startVibration(220);
  explosionStartMs = millis();
  explosionFrame = 0;
  gameMode = MODE_EXPLODING;
  lastRenderedMode = MODE_PLAYING;
}

void initFrameBuffer() {
  if (!frameBufferReady) {
    frameBuffer.setColorDepth(16);
    frameBufferReady = frameBuffer.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  }
}

void updateGame() {
  if (gameMode != MODE_PLAYING) {
    return;
  }

  uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) {
    return;
  }
  lastFrameMs = now;

  int xValue = analogRead(JOY_X_PIN);
  int yValue = analogRead(JOY_Y_PIN);

  float steer = (xValue - 2048.0f) / 2048.0f;
  speedValue = BASE_SPEED + min(1.55f, distanceMeters / 3200.0f);

  roadCurveBlend += speedValue * 0.028f;
  if (roadCurveBlend >= 1.0f) {
    roadCurveBlend = 0.0f;
    roadCurveTarget = random(-150, 151) / 100.0f;
  }
  roadCurve += (roadCurveTarget - roadCurve) * 0.04f;

  playerOffset += steer * (0.05f + speedValue * 0.02f);
  playerOffset -= roadCurve * speedValue * 0.01f;
  playerOffset = constrain(playerOffset, -0.82f, 0.82f);

  distanceMeters += speedValue * 5.2f;
  scoreValue = (uint32_t)(distanceMeters * 13.0f);

  int playerX = SCREEN_W / 2 + (int)(playerOffset * 110.0f);
  int playerY = SCREEN_H - 34;
  const int playerW = 54;
  const int playerH = 40;
  int activeCars = 2 + min(3, (int)(distanceMeters / 3200.0f));

  for (int i = 0; i < activeCars; ++i) {
    float densityBoost = min(0.028f, distanceMeters / 220000.0f);
    trafficCars[i].depth += speedValue * (0.008f + densityBoost + trafficCars[i].speedBias * 0.0035f);
    if (trafficCars[i].depth > 1.08f) {
      float minDepth = 0.02f;
      float maxDepth = min(0.24f, 0.10f + distanceMeters / 50000.0f);
      float spawnDepth = minDepth + random(0, 100) / 100.0f * (maxDepth - minDepth);
      respawnTrafficCar(i, spawnDepth);
      continue;
    }

    ScreenCarRect rect = getTrafficScreenRect(trafficCars[i]);
    bool overlapX = abs(rect.x - playerX) < (rect.w + playerW) / 2 - 4;
    bool overlapY = abs(rect.y - playerY) < (rect.h + playerH) / 2 - 3;
    if (rect.y > SCREEN_H - 96 && overlapX && overlapY) {
      triggerCrash();
      return;
    }
  }

  for (int i = activeCars; i < 5; ++i) {
    trafficCars[i].depth = 1.6f + i * 0.2f;
  }

  if (flashFrames > 0) {
    --flashFrames;
  }
}

template <typename CanvasT>
void drawPlayerCar(CanvasT& canvas, int x, int y) {
  uint16_t body = tft.color565(214, 28, 40);
  uint16_t hood = tft.color565(235, 54, 64);
  uint16_t shine = tft.color565(255, 220, 220);
  uint16_t shadow = tft.color565(90, 0, 10);
  uint16_t glass = tft.color565(180, 232, 255);
  uint16_t glassDark = tft.color565(54, 104, 142);
  uint16_t bumper = tft.color565(250, 236, 180);

  canvas.fillTriangle(x - 27, y + 18, x + 27, y + 18, x, y - 20, shadow);
  canvas.fillTriangle(x - 24, y + 14, x + 24, y + 14, x, y - 18, body);
  canvas.fillTriangle(x - 18, y + 4, x + 18, y + 4, x, y - 16, hood);
  canvas.fillRect(x - 22, y + 4, 44, 14, body);
  canvas.fillRoundRect(x - 16, y - 5, 32, 12, 4, glassDark);
  canvas.fillRoundRect(x - 14, y - 4, 28, 10, 4, glass);
  canvas.fillRect(x - 10, y - 2, 20, 3, shine);
  canvas.drawFastHLine(x - 16, y + 8, 32, lighten565(body, 80));
  canvas.drawFastHLine(x - 20, y + 15, 40, darken565(body, 120));
  canvas.fillRect(x - 21, y + 18, 10, 5, TFT_BLACK);
  canvas.fillRect(x + 11, y + 18, 10, 5, TFT_BLACK);
  canvas.fillRect(x - 15, y + 18, 4, 2, darken565(TFT_BLACK, 0));
  canvas.fillRect(x + 11, y + 18, 4, 2, darken565(TFT_BLACK, 0));
  canvas.fillRect(x - 12, y + 17, 24, 3, bumper);
  canvas.drawFastHLine(x - 8, y + 21, 16, shine);
}

template <typename CanvasT>
void drawTrafficCar(CanvasT& canvas, int x, int y, int w, int h, uint16_t color) {
  int halfW = w / 2;
  int halfH = h / 2;
  int roofW = max(6, w / 2);
  int roofH = max(4, h / 3);
  uint16_t roofColor = lighten565(color, 45);
  uint16_t glass = tft.color565(170, 222, 255);
  uint16_t sideShadow = darken565(color, 90);
  uint16_t frontShine = lighten565(color, 110);

  canvas.fillRoundRect(x - halfW, y - halfH, w, h, max(2, w / 6), color);
  canvas.fillRoundRect(x - roofW / 2, y - halfH + 1, roofW, roofH, 2, roofColor);
  canvas.fillRoundRect(x - roofW / 2 + 1, y - halfH + 2, roofW - 2, max(2, roofH - 3), 2, glass);
  canvas.fillRect(x - halfW + 1, y + halfH - max(3, h / 5), w - 2, max(2, h / 6), sideShadow);
  canvas.drawFastHLine(x - halfW + 2, y - halfH + 2, w - 4, frontShine);
  canvas.fillRect(x - halfW, y + halfH - 4, max(3, w / 4), 4, TFT_BLACK);
  canvas.fillRect(x + halfW - max(3, w / 4), y + halfH - 4, max(3, w / 4), 4, TFT_BLACK);
  canvas.fillRect(x - max(2, w / 6), y + halfH - 2, max(4, w / 3), 2, lighten565(TFT_YELLOW, 80));
}

template <typename CanvasT>
void drawExplosion(CanvasT& canvas, int x, int y, int frame) {
  int radius = 10 + frame * 7;
  uint16_t outer = tft.color565(255, 120, 10);
  uint16_t mid = tft.color565(255, 188, 32);
  uint16_t inner = tft.color565(255, 244, 180);

  canvas.fillCircle(x, y, radius, outer);
  canvas.fillCircle(x - radius / 3, y + 2, radius / 2, mid);
  canvas.fillCircle(x + radius / 4, y - radius / 5, radius / 3, inner);
  for (int i = 0; i < 6; ++i) {
    int dx = (i - 3) * (frame + 2);
    canvas.fillTriangle(x + dx, y - radius, x + dx - 5, y - radius - 10, x + dx + 5, y - radius - 10, mid);
  }
}

template <typename CanvasT>
void drawTitle(CanvasT& canvas) {
  uint16_t bg = tft.color565(18, 28, 52);
  uint16_t panel = tft.color565(36, 58, 102);
  uint16_t stripe = tft.color565(255, 196, 82);
  uint16_t accent = tft.color565(255, 234, 170);
  uint16_t roadBand = tft.color565(52, 52, 68);
  uint16_t grassBand = tft.color565(38, 110, 62);
  uint16_t skyBand = tft.color565(86, 132, 204);

  canvas.fillRect(0, 0, SCREEN_W, SCREEN_H, bg);
  canvas.fillRect(320, 0, SCREEN_W - 320, SCREEN_H, bg);
  canvas.fillRect(24, 24, SCREEN_W - 48, SCREEN_H - 48, panel);
  canvas.fillRect(40, 40, SCREEN_W - 80, 44, accent);
  canvas.fillRect(40, 88, SCREEN_W - 80, 16, stripe);
  canvas.fillRect(40, 108, SCREEN_W - 80, 92, skyBand);
  canvas.fillRect(40, 200, SCREEN_W - 80, 68, grassBand);
  canvas.fillRect(116, 214, SCREEN_W - 232, 38, roadBand);
  canvas.fillRect(SCREEN_W / 2 - 4, 220, 8, 24, TFT_WHITE);

  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawString("RAD", 70, 64, 7);
  canvas.setTextColor(stripe, panel);
  canvas.drawString("RUNNER", 126, 128, 7);
  drawPlayerCar(canvas, SCREEN_W / 2, 212);

  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawString("DISTANCE RANKING RUN", 96, 248, 2);
  canvas.drawString("BLUE START", 128, 274, 4);
  canvas.drawString("GREEN CLEAR SCORES", 112, 302, 2);
}

template <typename CanvasT>
void drawLeaderboard(CanvasT& canvas) {
  uint16_t bg = tft.color565(14, 18, 34);
  uint16_t rowBg = tft.color565(52, 60, 86);
  canvas.fillScreen(bg);
  canvas.fillRect(320, 0, SCREEN_W - 320, SCREEN_H, bg);
  canvas.setTextColor(TFT_YELLOW, bg);
  canvas.drawString("LEADERBOARD", 98, 24, 4);
  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawString("DISTANCE KM", 122, 64, 2);

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    int y = 100 + i * 36;
    uint16_t rowColor = leaderboard[i].color;
    uint16_t fillColor = (i == leaderboardHighlight) ? darken565(rowColor, 170) : rowBg;
    uint16_t textColor = (i == leaderboardHighlight) ? lighten565(rowColor, 130) : TFT_WHITE;
    canvas.fillRoundRect(64, y - 6, 352, 28, 6, fillColor);
    canvas.setTextColor(textColor, fillColor);
    canvas.drawString(String(i + 1), 84, y, 2);
    canvas.drawString(String(leaderboard[i].km, 2) + " km", 152, y, 2);
    if (i == leaderboardHighlight) {
      canvas.fillTriangle(384, y + 8, 400, y, 400, y + 16, rowColor);
      canvas.fillCircle(370, y + 8, 7, lighten565(rowColor, 120));
    }
  }

  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawString("LAST RUN " + String(lastRunKm, 2) + " km", 118, 286, 2);
  canvas.drawString("BLUE RETRY", 74, 306, 2);
  canvas.drawString("GREEN TITLE", 292, 306, 2);
}

template <typename CanvasT>
void drawScene(CanvasT& canvas) {
  uint16_t skyTop = tft.color565(70, 168, 255);
  uint16_t skyBottom = tft.color565(158, 220, 255);
  uint16_t hillDark = tft.color565(32, 106, 70);
  uint16_t hillLight = tft.color565(58, 142, 89);
  uint16_t roadColor = tft.color565(66, 66, 74);
  uint16_t laneColor = tft.color565(255, 245, 170);
  uint16_t grassA = tft.color565(40, 138, 44);
  uint16_t grassB = tft.color565(22, 116, 40);
  uint16_t rumbleA = tft.color565(255, 243, 240);
  uint16_t rumbleB = tft.color565(224, 62, 62);

  canvas.fillScreen(flashFrames > 0 ? TFT_WHITE : skyBottom);
  canvas.fillRect(0, 0, SCREEN_W, ROAD_HORIZON_Y, skyTop);
  canvas.fillCircle(382, 42, 22, TFT_YELLOW);
  canvas.fillTriangle(10, ROAD_HORIZON_Y, 92, 36, 174, ROAD_HORIZON_Y, hillDark);
  canvas.fillTriangle(120, ROAD_HORIZON_Y, 208, 28, 298, ROAD_HORIZON_Y, hillLight);
  canvas.fillTriangle(244, ROAD_HORIZON_Y, 334, 42, 424, ROAD_HORIZON_Y, hillDark);
  canvas.fillTriangle(344, ROAD_HORIZON_Y, 422, 24, SCREEN_W + 8, ROAD_HORIZON_Y, hillLight);

  for (int y = ROAD_HORIZON_Y; y < SCREEN_H; ++y) {
    float p = (float)(y - ROAD_HORIZON_Y) / (float)(SCREEN_H - ROAD_HORIZON_Y);
    float z = p * p;
    int center = SCREEN_W / 2 + (int)(roadCurve * z * 110.0f) - (int)(playerOffset * z * 44.0f);
    int roadHalf = 32 + (int)(p * 154.0f);
    int rumbleW = 4 + (int)(p * 14.0f);
    uint16_t grassColor = (((int)(distanceMeters * 3.0f) + y / 14) & 1) ? grassA : grassB;
    uint16_t rumbleColor = (((int)(distanceMeters * 7.0f) + y / 7) & 1) ? rumbleA : rumbleB;

    canvas.drawFastHLine(0, y, SCREEN_W, grassColor);
    canvas.drawFastHLine(center - roadHalf - rumbleW, y, rumbleW, rumbleColor);
    canvas.drawFastHLine(center - roadHalf, y, roadHalf * 2, roadColor);
    canvas.drawFastHLine(center + roadHalf, y, rumbleW, rumbleColor);

    if ((((int)(distanceMeters * 11.0f) + y / 9) % 18) < 7) {
      int laneOffset = roadHalf / 3;
      int laneW = 2 + (int)(p * 6.0f);
      canvas.drawFastHLine(center - laneOffset - laneW / 2, y, laneW, laneColor);
      canvas.drawFastHLine(center + laneOffset - laneW / 2, y, laneW, laneColor);
    }
  }

  int order[5] = {0, 1, 2, 3, 4};
  int activeCars = 2 + min(3, (int)(distanceMeters / 3200.0f));
  for (int i = 0; i < 4; ++i) {
    for (int j = i + 1; j < 5; ++j) {
      if (trafficCars[order[i]].depth > trafficCars[order[j]].depth) {
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
  }

  for (int i = 0; i < activeCars; ++i) {
    const TrafficCar& car = trafficCars[order[i]];
    ScreenCarRect rect = getTrafficScreenRect(car);
    drawTrafficCar(canvas, rect.x, rect.y, rect.w, rect.h, car.color);
  }

  int playerX = SCREEN_W / 2 + (int)(playerOffset * 110.0f);
  drawPlayerCar(canvas, playerX, SCREEN_H - 34);

  canvas.fillRect(0, 0, SCREEN_W, 24, TFT_BLACK);
  char hudText[32];
  float kmValue = distanceMeters / 1000.0f;
  canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
  snprintf(hudText, sizeof(hudText), "%.2f KM", kmValue);
  canvas.drawString(hudText, 8, 0, 4);
  canvas.drawString(hudText, 9, 0, 4);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(hudText, sizeof(hudText), "SPD %d", (int)(speedValue * 160.0f));
  canvas.drawString(hudText, 360, 6, 2);
  canvas.drawString("DISTANCE BUILDS TRAFFIC", 8, SCREEN_H - 18, 2);
}

void renderGame() {
  if (gameMode == MODE_TITLE) {
    if (lastRenderedMode != MODE_TITLE) {
      drawTitle(tft);
      lastRenderedMode = MODE_TITLE;
    }
    return;
  }

  if (frameBufferReady) {
    if (gameMode == MODE_PLAYING) {
      drawScene(frameBuffer);
    } else if (gameMode == MODE_EXPLODING) {
      drawScene(frameBuffer);
      drawExplosion(frameBuffer, SCREEN_W / 2 + (int)(playerOffset * 110.0f), SCREEN_H - 34, explosionFrame);
      frameBuffer.fillRoundRect(126, 118, 228, 76, 10, tft.color565(18, 18, 22));
      frameBuffer.setTextColor(TFT_WHITE, tft.color565(18, 18, 22));
      frameBuffer.drawString("GAME OVER", 150, 132, 4);
      frameBuffer.drawString("DIST " + String(lastRunKm, 2) + " km", 154, 166, 2);
    } else {
      drawLeaderboard(frameBuffer);
    }
    frameBuffer.pushSprite(0, 0);
  } else {
    if (gameMode == MODE_PLAYING) {
      drawScene(tft);
    } else if (gameMode == MODE_EXPLODING) {
      drawScene(tft);
      drawExplosion(tft, SCREEN_W / 2 + (int)(playerOffset * 110.0f), SCREEN_H - 34, explosionFrame);
      tft.fillRoundRect(126, 118, 228, 76, 10, tft.color565(18, 18, 22));
      tft.setTextColor(TFT_WHITE, tft.color565(18, 18, 22));
      tft.drawString("GAME OVER", 150, 132, 4);
      tft.drawString("DIST " + String(lastRunKm, 2) + " km", 154, 166, 2);
    } else {
      drawLeaderboard(tft);
    }
  }
  lastRenderedMode = gameMode;
}

void updateFrontEnd() {
  if (gameMode == MODE_TITLE) {
    if (bluePressedEdge()) {
      beginRun();
    } else if (greenPressedEdge()) {
      seedLeaderboard();
      lastRenderedMode = MODE_LEADERBOARD;
    }
    return;
  }

  if (gameMode == MODE_EXPLODING) {
    explosionFrame = min(7, (int)((millis() - explosionStartMs) / 90));
    if (millis() - explosionStartMs > 1100) {
      gameMode = MODE_LEADERBOARD;
      resetInputLatches();
    }
    return;
  }

  if (gameMode == MODE_LEADERBOARD) {
    if (bluePressedEdge()) {
      beginRun();
    } else if (greenPressedEdge()) {
      gameMode = MODE_TITLE;
      lastRenderedMode = MODE_LEADERBOARD;
      resetInputLatches();
    }
  }
}

void setup() {
  randomSeed(micros());
  pinMode(JOY_X_PIN, INPUT);
  pinMode(JOY_Y_PIN, INPUT);
  pinMode(GREEN_BTN_PIN, INPUT_PULLUP);
  pinMode(BLUE_BTN_PIN, INPUT_PULLUP);
  ledcSetup(reverse_ch, motor_frequency, motor_resolution);
  ledcAttachPin(reverse_pin, reverse_ch);
  ledcSetup(forward_ch, motor_frequency, motor_resolution);
  ledcAttachPin(forward_pin, forward_ch);
  stopMotor();
  i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);
  i2s_zero_dma_buffer(I2S_NUM_1);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  initFrameBuffer();
  seedLeaderboard();
  resetInputLatches();
  xTaskCreatePinnedToCore(
      audioTaskMain,
      "audio_task",
      4096,
      nullptr,
      2,
      &audioTaskHandle,
      0);
  renderGame();
}

void loop() {
  updateFrontEnd();
  updateGame();
  updateVibration();
  renderGame();
  delay(1);
}
