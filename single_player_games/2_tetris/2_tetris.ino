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
static const int BOARD_COLS = 10;
static const int BOARD_ROWS = 20;
static const int CELL = 12;
static const int BOARD_X = 84;
static const int BOARD_Y = 36;
static const int HUD_X = BOARD_X + BOARD_COLS * CELL + 24;

// User-configurable flags.
bool vibrationEnabled = true;
bool bgmEnabled = false;

enum GameState {
  TITLE,
  PLAYING,
  PAUSED,
  CLEARED,
  GAME_OVER
};

struct Voice {
  bool active;
  float freq;
  float phase;
  int remainSamples;
  float volume;
  uint8_t wave;  // 0 square, 1 sine, 2 noise, 3 tri
};

struct SfxEvent {
  float freq;
  int ms;
  float volume;
  uint8_t wave;
};

struct Note {
  uint16_t freq;
  uint16_t ms;
};

struct Piece {
  int type;
  int rot;
  int x;
  int y;
};

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;

uint8_t board[BOARD_ROWS][BOARD_COLS];
Piece activePiece;
int nextPieceType = 0;

int score = 0;
int linesClearedTotal = 0;
int level = 1;
const int WIN_LINE_TARGET = 40;

unsigned long lastDropMs = 0;
unsigned long dropIntervalMs = 650;
unsigned long lastMoveMs = 0;
unsigned long lastDownMs = 0;
unsigned long lastRotateMs = 0;
unsigned long lockDelayStartMs = 0;
bool touchingGround = false;

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;

// Vibration state.
bool vibrating = false;
bool vibForward = true;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;

// Audio engine.
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
float bgmPhase = 0.0f;
int bgmIndex = 0;
int bgmRemainSamples = 0;
SfxEvent sfxQueue[24];
volatile uint8_t sfxQueueHead = 0;
volatile uint8_t sfxQueueTail = 0;
const Note *activeJingle = nullptr;
int activeJingleCount = 0;
int activeJingleIndex = 0;
int jingleRemainSamples = 0;
float jinglePhase = 0.0f;
bool jingleActive = false;

const Note bgmLoop[] = {
  // Korobeiniki (classic Tetris theme), canonical lead line.
  {659, 180}, {494, 90},  {523, 90},  {587, 180}, {523, 90},  {494, 90},
  {440, 180}, {440, 90},  {523, 90},  {659, 180}, {587, 90},  {523, 90},
  {494, 270}, {523, 90},  {587, 180}, {659, 180}, {523, 180}, {440, 180},
  {440, 180}, {0, 120},

  {587, 180}, {698, 90},  {880, 180}, {784, 90},  {698, 90},  {659, 270},
  {523, 90},  {659, 180}, {587, 90},  {523, 90},  {494, 180}, {494, 90},
  {523, 90},  {587, 180}, {659, 180}, {523, 180}, {440, 180}, {440, 180},
  {0, 120}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

const Note clearJingle[] = {
  {523, 110}, {659, 110}, {784, 110}, {1046, 220}, {0, 80}
};
const Note gameOverJingle[] = {
  {659, 120}, {523, 120}, {440, 160}, {349, 240}, {0, 80}
};

int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
TaskHandle_t audioTaskHandle = nullptr;
bool frameBufferReady = false;

static const uint16_t pieceColors[8] = {
  TFT_BLACK,
  TFT_CYAN,    // I
  TFT_BLUE,    // J
  TFT_ORANGE,  // L
  TFT_YELLOW,  // O
  TFT_GREEN,   // S
  TFT_MAGENTA, // T
  TFT_RED      // Z
};

// [piece][rotation][block][xy]
static const int8_t SHAPES[7][4][4][2] = {
  // I
  {
    {{0,1}, {1,1}, {2,1}, {3,1}},
    {{2,0}, {2,1}, {2,2}, {2,3}},
    {{0,2}, {1,2}, {2,2}, {3,2}},
    {{1,0}, {1,1}, {1,2}, {1,3}}
  },
  // J
  {
    {{0,0}, {0,1}, {1,1}, {2,1}},
    {{1,0}, {2,0}, {1,1}, {1,2}},
    {{0,1}, {1,1}, {2,1}, {2,2}},
    {{1,0}, {1,1}, {0,2}, {1,2}}
  },
  // L
  {
    {{2,0}, {0,1}, {1,1}, {2,1}},
    {{1,0}, {1,1}, {1,2}, {2,2}},
    {{0,1}, {1,1}, {2,1}, {0,2}},
    {{0,0}, {1,0}, {1,1}, {1,2}}
  },
  // O
  {
    {{1,0}, {2,0}, {1,1}, {2,1}},
    {{1,0}, {2,0}, {1,1}, {2,1}},
    {{1,0}, {2,0}, {1,1}, {2,1}},
    {{1,0}, {2,0}, {1,1}, {2,1}}
  },
  // S
  {
    {{1,0}, {2,0}, {0,1}, {1,1}},
    {{1,0}, {1,1}, {2,1}, {2,2}},
    {{1,1}, {2,1}, {0,2}, {1,2}},
    {{0,0}, {0,1}, {1,1}, {1,2}}
  },
  // T
  {
    {{1,0}, {0,1}, {1,1}, {2,1}},
    {{1,0}, {1,1}, {2,1}, {1,2}},
    {{0,1}, {1,1}, {2,1}, {1,2}},
    {{1,0}, {0,1}, {1,1}, {1,2}}
  },
  // Z
  {
    {{0,0}, {1,0}, {1,1}, {2,1}},
    {{2,0}, {1,1}, {2,1}, {1,2}},
    {{0,1}, {1,1}, {1,2}, {2,2}},
    {{1,0}, {0,1}, {1,1}, {0,2}}
  }
};

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startVibration(uint16_t durationMs = 150) {
  if (!vibrationEnabled) {
    return;
  }
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

void startVoice(float freq, int ms, float volume, uint8_t wave) {
  portENTER_CRITICAL(&audioMux);
  uint8_t nextHead = (uint8_t)((sfxQueueHead + 1) % 24);
  if (nextHead != sfxQueueTail) {
    sfxQueue[sfxQueueHead].freq = freq;
    sfxQueue[sfxQueueHead].ms = ms;
    sfxQueue[sfxQueueHead].volume = volume;
    sfxQueue[sfxQueueHead].wave = wave;
    sfxQueueHead = nextHead;
  }
  portEXIT_CRITICAL(&audioMux);
}

void applyStartVoice(float freq, int ms, float volume, uint8_t wave) {
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  for (int i = 0; i < 6; i++) {
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

void flushSfxQueue() {
  while (true) {
    SfxEvent evt;
    bool hasEvent = false;

    portENTER_CRITICAL(&audioMux);
    if (sfxQueueTail != sfxQueueHead) {
      evt = sfxQueue[sfxQueueTail];
      sfxQueueTail = (uint8_t)((sfxQueueTail + 1) % 24);
      hasEvent = true;
    }
    portEXIT_CRITICAL(&audioMux);

    if (!hasEvent) {
      break;
    }
    applyStartVoice(evt.freq, evt.ms, evt.volume, evt.wave);
  }
}

void startJingle(const Note *melody, int count) {
  if (melody == nullptr || count <= 0) {
    return;
  }
  activeJingle = melody;
  activeJingleCount = count;
  activeJingleIndex = 0;
  jingleRemainSamples = (SAMPLE_RATE * activeJingle[0].ms) / 1000;
  jinglePhase = 0.0f;
  jingleActive = true;
}

void playSfxGameOver() {
  startJingle(gameOverJingle, sizeof(gameOverJingle) / sizeof(gameOverJingle[0]));
}

void playSfxClear() {
  startJingle(clearJingle, sizeof(clearJingle) / sizeof(clearJingle[0]));
}

void playSfxLineClear(int lines) {
  if (lines <= 0) return;
  if (lines == 1) {
    startVoice(660.0f, 90, 0.11f, 1);
  } else if (lines == 2) {
    startVoice(660.0f, 90, 0.10f, 1);
    startVoice(880.0f, 110, 0.10f, 1);
  } else if (lines == 3) {
    startVoice(740.0f, 90, 0.10f, 1);
    startVoice(988.0f, 120, 0.10f, 1);
  } else {
    startVoice(520.0f, 70, 0.11f, 1);
    startVoice(780.0f, 90, 0.11f, 1);
    startVoice(1046.0f, 130, 0.11f, 1);
  }
}

float waveSample(Voice &v) {
  float s = 0.0f;
  if (v.wave == 0) {
    s = (v.phase < 0.5f) ? 1.0f : -1.0f;
  } else if (v.wave == 1) {
    s = sinf(v.phase * 2.0f * PI);
  } else if (v.wave == 2) {
    s = ((float)random(-1000, 1000)) / 1000.0f;
  } else {
    float t = v.phase;
    s = 2.0f * fabsf(2.0f * (t - floorf(t + 0.5f))) - 1.0f;
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
  float s = 0.0f;
  if (freq > 0.0f) {
    s = (bgmPhase < 0.5f) ? 0.020f : -0.020f;
    bgmPhase += freq / SAMPLE_RATE;
    if (bgmPhase >= 1.0f) {
      bgmPhase -= 1.0f;
    }
  }
  bgmRemainSamples--;
  return s;
}

float jingleSample() {
  if (!jingleActive || activeJingle == nullptr || activeJingleCount <= 0) {
    return 0.0f;
  }

  if (jingleRemainSamples <= 0) {
    activeJingleIndex++;
    if (activeJingleIndex >= activeJingleCount) {
      jingleActive = false;
      return 0.0f;
    }
    jingleRemainSamples = (SAMPLE_RATE * activeJingle[activeJingleIndex].ms) / 1000;
    jinglePhase = 0.0f;
  }

  float freq = (float)activeJingle[activeJingleIndex].freq;
  float s = 0.0f;
  if (freq > 0.0f) {
    s = (jinglePhase < 0.5f) ? 0.14f : -0.14f;
    jinglePhase += freq / SAMPLE_RATE;
    if (jinglePhase >= 1.0f) {
      jinglePhase -= 1.0f;
    }
  }
  jingleRemainSamples--;
  return s;
}

void audioTick() {
  flushSfxQueue();

  const int n = AUDIO_CHUNK_SAMPLES;
  for (int i = 0; i < n; i++) {
    float mix = bgmSample();
    mix += jingleSample();
    for (int v = 0; v < 6; v++) {
      if (!sfxVoices[v].active) continue;
      mix += waveSample(sfxVoices[v]);
      sfxVoices[v].remainSamples--;
      if (sfxVoices[v].remainSamples <= 0) {
        sfxVoices[v].active = false;
      }
    }

    if (mix > 0.70f) mix = 0.70f;
    if (mix < -0.70f) mix = -0.70f;
    audioBuffer[i] = (int16_t)(mix * 32767.0f);
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_NUM_1, (const char *)audioBuffer, n * sizeof(int16_t), &bytesWritten, pdMS_TO_TICKS(2));
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
  greenPressed = (g && !greenDown);
  bluePressed = (b && !blueDown);
  greenDown = g;
  blueDown = b;
}

bool canPlace(const Piece &p) {
  for (int i = 0; i < 4; i++) {
    int x = p.x + SHAPES[p.type][p.rot][i][0];
    int y = p.y + SHAPES[p.type][p.rot][i][1];
    if (x < 0 || x >= BOARD_COLS || y >= BOARD_ROWS) {
      return false;
    }
    if (y >= 0 && board[y][x] != 0) {
      return false;
    }
  }
  return true;
}

void stampPiece(const Piece &p) {
  for (int i = 0; i < 4; i++) {
    int x = p.x + SHAPES[p.type][p.rot][i][0];
    int y = p.y + SHAPES[p.type][p.rot][i][1];
    if (x >= 0 && x < BOARD_COLS && y >= 0 && y < BOARD_ROWS) {
      board[y][x] = (uint8_t)(p.type + 1);
    }
  }
}

int clearLines() {
  int cleared = 0;
  for (int y = BOARD_ROWS - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < BOARD_COLS; x++) {
      if (board[y][x] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      cleared++;
      for (int yy = y; yy > 0; yy--) {
        for (int xx = 0; xx < BOARD_COLS; xx++) {
          board[yy][xx] = board[yy - 1][xx];
        }
      }
      for (int xx = 0; xx < BOARD_COLS; xx++) {
        board[0][xx] = 0;
      }
      y++;
    }
  }
  return cleared;
}

int scoreForLines(int lines) {
  if (lines == 1) return 100;
  if (lines == 2) return 300;
  if (lines == 3) return 500;
  if (lines >= 4) return 800;
  return 0;
}

void refillDropSpeed() {
  int base = 720 - (level - 1) * 55;
  if (base < 110) base = 110;
  dropIntervalMs = (unsigned long)base;
}

int randomPieceType() {
  return random(0, 7);
}

void spawnPiece() {
  activePiece.type = nextPieceType;
  activePiece.rot = 0;
  activePiece.x = 3;
  activePiece.y = -1;
  nextPieceType = randomPieceType();

  if (!canPlace(activePiece)) {
    gameState = GAME_OVER;
    playSfxGameOver();
  }
  touchingGround = false;
  lockDelayStartMs = 0;
}

void tryRotate() {
  Piece test = activePiece;
  test.rot = (test.rot + 1) % 4;
  if (canPlace(test)) {
    activePiece = test;
    return;
  }

  const int kicks[4] = {-1, 1, -2, 2};
  for (int i = 0; i < 4; i++) {
    test = activePiece;
    test.rot = (test.rot + 1) % 4;
    test.x += kicks[i];
    if (canPlace(test)) {
      activePiece = test;
      return;
    }
  }
}

bool tryMove(int dx, int dy) {
  Piece test = activePiece;
  test.x += dx;
  test.y += dy;
  if (canPlace(test)) {
    activePiece = test;
    return true;
  }
  return false;
}

void lockPieceAndContinue() {
  stampPiece(activePiece);
  int lines = clearLines();
  if (lines > 0) {
    linesClearedTotal += lines;
    score += scoreForLines(lines) * level;
    level = 1 + (linesClearedTotal / 10);
    refillDropSpeed();
    playSfxLineClear(lines);
    startVibration((uint16_t)(120 + lines * 50));
    if (linesClearedTotal >= WIN_LINE_TARGET) {
      gameState = CLEARED;
      playSfxClear();
      return;
    }
  }
  spawnPiece();
}

void beginGame() {
  for (int y = 0; y < BOARD_ROWS; y++) {
    for (int x = 0; x < BOARD_COLS; x++) {
      board[y][x] = 0;
    }
  }

  score = 0;
  linesClearedTotal = 0;
  level = 1;
  refillDropSpeed();

  gameState = PLAYING;
  nextPieceType = randomPieceType();
  spawnPiece();

  lastDropMs = millis();
  lastMoveMs = 0;
  lastDownMs = 0;
  lastRotateMs = 0;
  lockDelayStartMs = 0;
}

void processPlayingInput(unsigned long now) {
  int xv = analogRead(x_pin);
  int yv = analogRead(y_pin);

  if (xv < 1350 && now - lastMoveMs >= 120) {
    tryMove(-1, 0);
    lastMoveMs = now;
  } else if (xv > 2700 && now - lastMoveMs >= 120) {
    tryMove(1, 0);
    lastMoveMs = now;
  }

  if (greenPressed && now - lastRotateMs >= 120) {
    tryRotate();
    lastRotateMs = now;
  }

  if (bluePressed) {
    gameState = PAUSED;
  }

  if (yv > 2800 && now - lastDownMs >= 55) {
    if (tryMove(0, 1)) {
      score += 1;
    }
    lastDownMs = now;
  }
}

void updateGravityAndLock(unsigned long now) {
  if (now - lastDropMs >= dropIntervalMs) {
    lastDropMs = now;
    if (!tryMove(0, 1)) {
      if (!touchingGround) {
        touchingGround = true;
        lockDelayStartMs = now;
      }
    } else {
      touchingGround = false;
      lockDelayStartMs = 0;
    }
  }

  if (!canPlace(Piece{activePiece.type, activePiece.rot, activePiece.x, activePiece.y + 1})) {
    if (!touchingGround) {
      touchingGround = true;
      lockDelayStartMs = now;
    }
  } else {
    touchingGround = false;
    lockDelayStartMs = 0;
  }

  if (touchingGround && lockDelayStartMs > 0 && now - lockDelayStartMs >= 260) {
    lockPieceAndContinue();
    touchingGround = false;
    lockDelayStartMs = 0;
  }
}

template <typename CanvasT>
void drawCell(CanvasT &canvas, int x, int y, uint16_t color) {
  int px = BOARD_X + x * CELL;
  int py = BOARD_Y + y * CELL;
  canvas.fillRect(px, py, CELL - 1, CELL - 1, color);
}

template <typename CanvasT>
void drawBoard(CanvasT &canvas) {
  canvas.fillRoundRect(BOARD_X - 5, BOARD_Y - 5, BOARD_COLS * CELL + 10, BOARD_ROWS * CELL + 10, 8, TFT_DARKGREY);
  canvas.fillRect(BOARD_X, BOARD_Y, BOARD_COLS * CELL, BOARD_ROWS * CELL, TFT_BLACK);

  for (int y = 0; y < BOARD_ROWS; y++) {
    for (int x = 0; x < BOARD_COLS; x++) {
      uint8_t c = board[y][x];
      if (c != 0) {
        drawCell(canvas, x, y, pieceColors[c]);
      }
    }
  }

  for (int i = 0; i < 4; i++) {
    int x = activePiece.x + SHAPES[activePiece.type][activePiece.rot][i][0];
    int y = activePiece.y + SHAPES[activePiece.type][activePiece.rot][i][1];
    if (y >= 0) {
      drawCell(canvas, x, y, pieceColors[activePiece.type + 1]);
    }
  }

  for (int x = 0; x <= BOARD_COLS; x++) {
    int gx = BOARD_X + x * CELL;
    canvas.drawLine(gx, BOARD_Y, gx, BOARD_Y + BOARD_ROWS * CELL, TFT_DARKGREY);
  }
  for (int y = 0; y <= BOARD_ROWS; y++) {
    int gy = BOARD_Y + y * CELL;
    canvas.drawLine(BOARD_X, gy, BOARD_X + BOARD_COLS * CELL, gy, TFT_DARKGREY);
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.setTextSize(2);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.setCursor(HUD_X, 40);
  canvas.printf("Score\\n%d", score);

  canvas.setCursor(HUD_X, 100);
  canvas.printf("Lines\\n%d", linesClearedTotal);

  canvas.setCursor(HUD_X, 160);
  canvas.printf("Level\\n%d", level);

  canvas.setCursor(HUD_X, 220);
  canvas.print("Next");

  int nx = HUD_X + 8;
  int ny = 248;
  for (int i = 0; i < 4; i++) {
    int bx = SHAPES[nextPieceType][0][i][0];
    int by = SHAPES[nextPieceType][0][i][1];
    canvas.fillRect(nx + bx * 10, ny + by * 10, 9, 9, pieceColors[nextPieceType + 1]);
  }

  canvas.setTextSize(1);
  canvas.setCursor(HUD_X, 300);
  canvas.print("VIB:");
  canvas.print(vibrationEnabled ? "ON" : "OFF");
}

void drawTitleLogo() {
  const int block = 12;
  const int ox = 82;
  const int oy = 26;

  auto draw = [&](int x, int y, uint16_t color) {
    int px = ox + x * block;
    int py = oy + y * block;
    tft.fillRect(px + 2, py + 2, block - 2, block - 2, TFT_DARKGREY);
    tft.fillRect(px, py, block - 2, block - 2, color);
  };

  // T
  for (int x = 0; x < 5; x++) draw(x, 0, TFT_CYAN);
  for (int y = 1; y < 5; y++) draw(2, y, TFT_CYAN);
  // E
  for (int y = 0; y < 5; y++) draw(6, y, TFT_YELLOW);
  for (int x = 7; x < 10; x++) draw(x, 0, TFT_YELLOW);
  for (int x = 7; x < 9; x++) draw(x, 2, TFT_YELLOW);
  for (int x = 7; x < 10; x++) draw(x, 4, TFT_YELLOW);
  // T
  for (int x = 11; x < 16; x++) draw(x, 0, TFT_ORANGE);
  for (int y = 1; y < 5; y++) draw(13, y, TFT_ORANGE);
  // R
  for (int y = 0; y < 5; y++) draw(17, y, TFT_GREEN);
  for (int x = 18; x < 20; x++) draw(x, 0, TFT_GREEN);
  draw(20, 1, TFT_GREEN);
  for (int x = 18; x < 20; x++) draw(x, 2, TFT_GREEN);
  draw(19, 3, TFT_GREEN);
  draw(20, 4, TFT_GREEN);
  // I
  for (int x = 22; x < 25; x++) draw(x, 0, TFT_MAGENTA);
  for (int y = 1; y < 5; y++) draw(23, y, TFT_MAGENTA);
  for (int x = 22; x < 25; x++) draw(x, 4, TFT_MAGENTA);
  // S
  for (int x = 26; x < 30; x++) draw(x, 0, TFT_RED);
  draw(26, 1, TFT_RED);
  for (int x = 26; x < 30; x++) draw(x, 2, TFT_RED);
  draw(29, 3, TFT_RED);
  for (int x = 26; x < 30; x++) draw(x, 4, TFT_RED);
}

void drawTitle() {
  tft.fillScreen(TFT_NAVY);
  drawTitleLogo();

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(48, 126);
  tft.print("Joystick Left/Right: Move");
  tft.setCursor(48, 154);
  tft.print("Joystick Down: Soft Drop");
  tft.setCursor(48, 182);
  tft.print("Green Button: Rotate");
  tft.setCursor(48, 210);
  tft.print("Blue Button: Pause/Resume");

  tft.setTextSize(1);
  tft.setCursor(74, 258);
  tft.print("Line clear triggers vibration (toggle by variable)");
  tft.setCursor(126, 274);
  tft.printf("Clear %d lines to win", WIN_LINE_TARGET);
  tft.setCursor(142, 292);
  tft.print("Press Blue to Start");
}

void drawPaused() {
  if (!frameBufferReady) {
    tft.fillScreen(TFT_BLACK);
    drawBoard(tft);
    drawHud(tft);
    tft.fillRoundRect(140, 130, 200, 64, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(3);
    tft.setCursor(180, 150);
    tft.print("PAUSED");
    return;
  }

  frameBuffer.fillScreen(TFT_BLACK);
  drawBoard(frameBuffer);
  drawHud(frameBuffer);
  frameBuffer.fillRoundRect(140, 130, 200, 64, 8, TFT_DARKGREY);
  frameBuffer.setTextColor(TFT_WHITE, TFT_DARKGREY);
  frameBuffer.setTextSize(3);
  frameBuffer.setCursor(180, 150);
  frameBuffer.print("PAUSED");
  frameBuffer.pushSprite(0, 0);
}

void drawGameOver() {
  tft.fillScreen(TFT_MAROON);
  tft.setTextColor(TFT_WHITE, TFT_MAROON);
  tft.setTextSize(3);
  tft.setCursor(128, 86);
  tft.print("GAME OVER");

  tft.setTextSize(2);
  tft.setCursor(144, 146);
  tft.printf("Score: %d", score);
  tft.setCursor(144, 174);
  tft.printf("Lines: %d", linesClearedTotal);
  tft.setCursor(96, 220);
  tft.print("Press Blue to Restart");
}

void drawCleared() {
  tft.fillScreen(TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextSize(3);
  tft.setCursor(126, 86);
  tft.print("STAGE CLEAR");

  tft.setTextSize(2);
  tft.setCursor(144, 146);
  tft.printf("Score: %d", score);
  tft.setCursor(144, 174);
  tft.printf("Lines: %d", linesClearedTotal);
  tft.setCursor(96, 220);
  tft.print("Press Blue to Restart");
}

void drawPlaying() {
  if (!frameBufferReady) {
    tft.fillScreen(TFT_BLACK);
    drawBoard(tft);
    drawHud(tft);
    return;
  }

  frameBuffer.fillScreen(TFT_BLACK);
  drawBoard(frameBuffer);
  drawHud(frameBuffer);
  frameBuffer.pushSprite(0, 0);
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

  if (gameState == TITLE) {
    if (bluePressed) {
      beginGame();
    }
  } else if (gameState == PLAYING) {
    unsigned long now = millis();
    processPlayingInput(now);
    if (gameState == PLAYING) {
      updateGravityAndLock(now);
    }
  } else if (gameState == PAUSED) {
    if (bluePressed) {
      gameState = PLAYING;
      lastDropMs = millis();
    }
  } else if (gameState == CLEARED) {
    if (bluePressed) {
      beginGame();
    }
  } else if (gameState == GAME_OVER) {
    if (bluePressed) {
      beginGame();
    }
  }

  updateVibration();

  if (gameState != lastRenderedState) {
    lastRenderedState = gameState;
    if (gameState == TITLE) drawTitle();
    if (gameState == CLEARED) drawCleared();
    if (gameState == GAME_OVER) drawGameOver();
  }

  if (gameState == PLAYING) {
    drawPlaying();
  } else if (gameState == PAUSED) {
    drawPaused();
  }

  delay(1);
}
