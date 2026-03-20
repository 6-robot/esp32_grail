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
TFT_eSprite frameBuffer = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int PLAY_X = 20;
static const int PLAY_Y = 14;
static const int PLAY_W = 316;
static const int PLAY_H = 292;
static const int HUD_X = PLAY_X + PLAY_W + 10;
static const int HUD_W = SCREEN_W - HUD_X - 12;
static const int GRID_COLS = 10;
static const int GRID_ROWS = 13;
static const int BUBBLE_R = 14;
static const int DIAMETER = BUBBLE_R * 2;
static const int ROW_STEP = 24;
static const float BUBBLE_SPEED = 28.8f;
static const float AIM_MIN = -1.28f;
static const float AIM_MAX = 1.28f;
static const uint16_t UPDATE_MS = 16;
static const uint8_t POP_ANIM_FRAMES = 8;
static const uint8_t COLOR_COUNT = 5;
static const uint8_t LEADERBOARD_SIZE = 5;
static const uint8_t EMPTY_CELL = 255;
static const int SHOTS_PER_DESCENT = 7;
static const uint16_t PLAYER_LINE_Y = 274;
static const float SHOT_SPAWN_OFFSET = 34.0f;

enum GameState {
  TITLE,
  PLAYING,
  GAME_OVER
};

enum WaveType : uint8_t {
  WAVE_SQUARE = 0,
  WAVE_SINE = 1,
  WAVE_NOISE = 2,
  WAVE_TRIANGLE = 3
};

struct Voice {
  bool active;
  float freq;
  float phase;
  int remainSamples;
  float volume;
  WaveType wave;
  float slidePerSample;
};

struct Note {
  uint16_t freq;
  uint16_t ms;
};

struct MovingBubble {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  uint8_t color;
};

struct GridPos {
  int row;
  int col;
};

struct PopBubble {
  bool active;
  int row;
  int col;
  uint8_t color;
};

struct FallingBubble {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  uint8_t color;
};

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;

uint8_t grid[GRID_ROWS][GRID_COLS];
bool frameBufferReady = false;
bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
bool joyLeftDown = false;
bool joyRightDown = false;
int latestX = 2048;
int latestY = 2048;

unsigned long lastUpdateMs = 0;
unsigned long titlePulseMs = 0;
unsigned long gameOverPulseMs = 0;
unsigned long lastAimStepMs = 0;
bool lastTitlePromptVisible = false;
bool lastGameOverPromptVisible = false;

float aimAngle = 0.0f;
MovingBubble shot;
uint8_t currentBubble = 0;
uint8_t nextBubble = 1;
uint16_t score = 0;
uint16_t combo = 0;
uint16_t leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int8_t latestRank = -1;
bool scoreSubmitted = false;
uint8_t shotsUntilDrop = SHOTS_PER_DESCENT;
uint8_t rowsShifted = 0;
char endReason[28] = "";
PopBubble popBubbles[GRID_ROWS * GRID_COLS];
uint8_t popBubbleCount = 0;
uint8_t popAnimFrame = 0;
bool popAnimating = false;
FallingBubble fallingBubbles[GRID_ROWS * GRID_COLS];
uint8_t fallingBubbleCount = 0;
uint8_t fallAnimFrame = 0;
bool fallAnimating = false;
uint16_t pendingClusterScore = 0;
uint16_t pendingComboBonus = 0;

bool vibrating = false;
bool vibForward = true;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;

Voice sfxVoices[6];
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
  {392, 120}, {440, 120}, {523, 120}, {659, 120},
  {587, 120}, {523, 120}, {440, 120}, {392, 120},
  {349, 120}, {392, 120}, {440, 120}, {523, 120},
  {494, 120}, {440, 120}, {392, 180}, {0, 80},
  {523, 120}, {587, 120}, {659, 120}, {784, 120},
  {698, 120}, {659, 120}, {587, 120}, {523, 120},
  {440, 120}, {523, 120}, {659, 120}, {523, 120},
  {494, 160}, {440, 160}, {392, 220}, {0, 100}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

const uint16_t bubbleColors[COLOR_COUNT] = {
  0xF9C4,  // pink
  0xFF40,  // gold
  0x4FBF,  // cyan
  0x7DA7,  // mint
  0x9B5F   // violet
};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
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

uint16_t lighten565(uint16_t color, uint8_t amount) {
  uint8_t r = (((color >> 11) & 0x1F) * 255) / 31;
  uint8_t g = (((color >> 5) & 0x3F) * 255) / 63;
  uint8_t b = ((color & 0x1F) * 255) / 31;
  r = (uint8_t)min(255, r + amount);
  g = (uint8_t)min(255, g + amount);
  b = (uint8_t)min(255, b + amount);
  return rgb565(r, g, b);
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

void assignVoice(Voice &voice, float freq, int durationSamples, float volume, WaveType wave, float slidePerSample = 0.0f) {
  voice.active = true;
  voice.freq = freq;
  voice.phase = 0.0f;
  voice.remainSamples = durationSamples;
  voice.volume = volume;
  voice.wave = wave;
  voice.slidePerSample = slidePerSample;
}

void startVoice(float freq, int ms, float volume, WaveType wave, int preferredChannel = -1, float slidePerSample = 0.0f) {
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  taskENTER_CRITICAL(&audioMux);
  if (preferredChannel >= 0 && preferredChannel < 6) {
    assignVoice(sfxVoices[preferredChannel], freq, durationSamples, volume, wave, slidePerSample);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }

  for (int i = 0; i < 6; i++) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave, slidePerSample);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }

  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave, slidePerSample);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxShoot() {
  startVoice(760.0f, 60, 0.12f, WAVE_SQUARE, 0, 0.05f);
  startVoice(980.0f, 40, 0.06f, WAVE_TRIANGLE, 1, -0.08f);
}

void playSfxStick() {
  startVoice(330.0f, 80, 0.10f, WAVE_TRIANGLE, 2);
}

void playSfxSwap() {
  startVoice(520.0f, 45, 0.08f, WAVE_SINE, 3);
  startVoice(690.0f, 45, 0.06f, WAVE_SINE, 4);
}

void playSfxClear() {
  startVoice(620.0f, 90, 0.10f, WAVE_SQUARE, 0);
  startVoice(780.0f, 90, 0.10f, WAVE_SQUARE, 1);
  startVoice(980.0f, 120, 0.09f, WAVE_TRIANGLE, 2);
}

void playSfxDrop() {
  startVoice(240.0f, 120, 0.11f, WAVE_NOISE, 3);
  startVoice(180.0f, 140, 0.11f, WAVE_TRIANGLE, 4, -0.015f);
}

void playSfxLose() {
  startVoice(440.0f, 120, 0.11f, WAVE_SQUARE, 0, -0.02f);
  startVoice(330.0f, 160, 0.11f, WAVE_TRIANGLE, 1, -0.015f);
  startVoice(220.0f, 220, 0.12f, WAVE_TRIANGLE, 2, -0.012f);
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

  float sample = (bgmPhase < 0.5f) ? 0.024f : -0.024f;
  bgmPhase += (float)freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  return sample;
}

float waveSample(Voice &v) {
  float s = 0.0f;
  if (v.wave == WAVE_SQUARE) {
    s = (v.phase < 0.5f) ? 1.0f : -1.0f;
  } else if (v.wave == WAVE_SINE) {
    s = sinf(v.phase * 2.0f * PI);
  } else if (v.wave == WAVE_NOISE) {
    s = ((float)random(-1000, 1000)) / 1000.0f;
  } else {
    s = (v.phase < 0.5f) ? (v.phase * 4.0f - 1.0f) : (3.0f - v.phase * 4.0f);
  }

  v.phase += v.freq / SAMPLE_RATE;
  v.freq += v.slidePerSample;
  if (v.freq < 40.0f) {
    v.freq = 40.0f;
  }
  if (v.phase >= 1.0f) {
    v.phase -= 1.0f;
  }
  return s * v.volume;
}

void audioTick() {
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; i++) {
    float mix = bgmSample();
    taskENTER_CRITICAL(&audioMux);
    for (int v = 0; v < 6; v++) {
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

    if (mix > 1.0f) mix = 1.0f;
    if (mix < -1.0f) mix = -1.0f;
    audioBuffer[i] = (int16_t)(mix * 12800.0f);
  }

  size_t written = 0;
  i2s_write(I2S_NUM_1, audioBuffer, sizeof(audioBuffer), &written, portMAX_DELAY);
}

void audioTaskMain(void *parameter) {
  (void)parameter;
  while (true) {
    audioTick();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
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

void readButtons() {
  bool g = (digitalRead(green_btn_pin) == LOW);
  bool b = (digitalRead(blue_btn_pin) == LOW);
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);
  bool leftNow = (latestX < 1300);
  bool rightNow = (latestX > 2900);
  greenPressed = (g && !greenDown);
  bluePressed = (b && !blueDown);
  greenDown = g;
  blueDown = b;
  joyLeftDown = leftNow;
  joyRightDown = rightNow;
}

bool inBounds(int row, int col) {
  return row >= 0 && row < GRID_ROWS && col >= 0 && col < GRID_COLS;
}

float rowOffsetX(int row) {
  return (row & 1) ? (float)BUBBLE_R : 0.0f;
}

float cellCenterX(int row, int col) {
  return PLAY_X + BUBBLE_R + rowOffsetX(row) + col * DIAMETER;
}

float cellCenterY(int row) {
  return PLAY_Y + BUBBLE_R + row * ROW_STEP;
}

int launcherCenterX() {
  return PLAY_X + PLAY_W / 2;
}

int launcherCenterY() {
  return PLAYER_LINE_Y + 12;
}

int rowBubbleCount(int row) {
  return (row & 1) ? (GRID_COLS - 1) : GRID_COLS;
}

bool cellExists(int row, int col) {
  if (row < 0 || row >= GRID_ROWS) {
    return false;
  }
  return col >= 0 && col < rowBubbleCount(row);
}

uint8_t randomPresentColor() {
  bool present[COLOR_COUNT];
  for (int i = 0; i < COLOR_COUNT; i++) {
    present[i] = false;
  }

  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < rowBubbleCount(row); col++) {
      uint8_t value = grid[row][col];
      if (value < COLOR_COUNT) {
        present[value] = true;
      }
    }
  }

  uint8_t pool[COLOR_COUNT];
  uint8_t size = 0;
  for (int i = 0; i < COLOR_COUNT; i++) {
    if (present[i]) {
      pool[size++] = i;
    }
  }

  if (size == 0) {
    return random(COLOR_COUNT);
  }
  return pool[random(size)];
}

void chooseUpcomingBubbles() {
  currentBubble = randomPresentColor();
  nextBubble = randomPresentColor();
}

void clearPopAnimation() {
  popBubbleCount = 0;
  popAnimFrame = 0;
  popAnimating = false;
  pendingClusterScore = 0;
  pendingComboBonus = 0;
  for (int i = 0; i < GRID_ROWS * GRID_COLS; i++) {
    popBubbles[i].active = false;
  }
}

void clearFallAnimation() {
  fallingBubbleCount = 0;
  fallAnimFrame = 0;
  fallAnimating = false;
  for (int i = 0; i < GRID_ROWS * GRID_COLS; i++) {
    fallingBubbles[i].active = false;
  }
}

void submitScore() {
  if (scoreSubmitted) {
    return;
  }

  latestRank = -1;
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    if (score > leaderboard[i]) {
      latestRank = i;
      break;
    }
  }

  if (latestRank >= 0) {
    for (int i = LEADERBOARD_SIZE - 1; i > latestRank; --i) {
      leaderboard[i] = leaderboard[i - 1];
    }
    leaderboard[latestRank] = score;
  }

  scoreSubmitted = true;
}

bool colorStillPresent(uint8_t color) {
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < rowBubbleCount(row); col++) {
      if (grid[row][col] == color) {
        return true;
      }
    }
  }
  return false;
}

void normalizeUpcomingBubbles() {
  if (!colorStillPresent(currentBubble)) {
    currentBubble = randomPresentColor();
  }
  if (!colorStillPresent(nextBubble)) {
    nextBubble = randomPresentColor();
  }
}

void pushTopRow() {
  for (int row = GRID_ROWS - 1; row > 0; row--) {
    for (int col = 0; col < GRID_COLS; col++) {
      grid[row][col] = cellExists(row, col) ? grid[row - 1][col] : EMPTY_CELL;
    }
  }

  uint8_t rowColors[GRID_COLS];
  uint8_t slots = rowBubbleCount(0);
  uint8_t palette = min((int)COLOR_COUNT, 3 + rowsShifted / 2);
  for (int col = 0; col < slots; col++) {
    rowColors[col] = random(palette);
  }

  bool sameOnly = true;
  for (int col = 1; col < slots; col++) {
    if (rowColors[col] != rowColors[0]) {
      sameOnly = false;
      break;
    }
  }
  if (sameOnly && slots > 1) {
    rowColors[random(slots)] = (rowColors[0] + 1) % palette;
  }

  for (int col = 0; col < GRID_COLS; col++) {
    grid[0][col] = (col < slots) ? rowColors[col] : EMPTY_CELL;
  }
  rowsShifted++;
}

void resetGrid() {
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < GRID_COLS; col++) {
      grid[row][col] = EMPTY_CELL;
    }
  }

  for (int row = 0; row < 4; row++) {
    int slots = rowBubbleCount(row);
    for (int col = 0; col < slots; col++) {
      grid[row][col] = (row + col + random(3)) % COLOR_COUNT;
    }
  }
}

void beginGame() {
  resetGrid();
  chooseUpcomingBubbles();
  clearPopAnimation();
  clearFallAnimation();
  aimAngle = 0.0f;
  shot.active = false;
  score = 0;
  combo = 0;
  latestRank = -1;
  scoreSubmitted = false;
  shotsUntilDrop = SHOTS_PER_DESCENT;
  rowsShifted = 0;
  endReason[0] = '\0';
  lastUpdateMs = millis();
  lastAimStepMs = 0;
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  gameState = PLAYING;
}

void endGame(const char *reason) {
  strncpy(endReason, reason, sizeof(endReason) - 1);
  endReason[sizeof(endReason) - 1] = '\0';
  submitScore();
  gameState = GAME_OVER;
  gameOverPulseMs = millis();
  shot.active = false;
  playSfxLose();
  startVibration(260);
}

void getNeighbors(int row, int col, GridPos out[6], int &count) {
  count = 0;
  int sameRowCols[2] = {col - 1, col + 1};
  for (int i = 0; i < 2; i++) {
    int nc = sameRowCols[i];
    if (cellExists(row, nc)) {
      out[count++] = {row, nc};
    }
  }

  int upperRow = row - 1;
  int lowerRow = row + 1;
  if (row & 1) {
    int candidates[2] = {col, col + 1};
    for (int i = 0; i < 2; i++) {
      if (cellExists(upperRow, candidates[i])) {
        out[count++] = {upperRow, candidates[i]};
      }
      if (cellExists(lowerRow, candidates[i])) {
        out[count++] = {lowerRow, candidates[i]};
      }
    }
  } else {
    int candidates[2] = {col - 1, col};
    for (int i = 0; i < 2; i++) {
      if (cellExists(upperRow, candidates[i])) {
        out[count++] = {upperRow, candidates[i]};
      }
      if (cellExists(lowerRow, candidates[i])) {
        out[count++] = {lowerRow, candidates[i]};
      }
    }
  }
}

int collectCluster(int startRow, int startCol, uint8_t color, GridPos results[GRID_ROWS * GRID_COLS]) {
  bool visited[GRID_ROWS][GRID_COLS];
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < GRID_COLS; col++) {
      visited[row][col] = false;
    }
  }

  GridPos queue[GRID_ROWS * GRID_COLS];
  int head = 0;
  int tail = 0;
  queue[tail++] = {startRow, startCol};
  visited[startRow][startCol] = true;

  int count = 0;
  while (head < tail) {
    GridPos current = queue[head++];
    results[count++] = current;
    GridPos neighbors[6];
    int neighborCount = 0;
    getNeighbors(current.row, current.col, neighbors, neighborCount);
    for (int i = 0; i < neighborCount; i++) {
      int nr = neighbors[i].row;
      int nc = neighbors[i].col;
      if (!inBounds(nr, nc) || !cellExists(nr, nc) || visited[nr][nc]) {
        continue;
      }
      if (grid[nr][nc] != color) {
        continue;
      }
      visited[nr][nc] = true;
      queue[tail++] = {nr, nc};
    }
  }

  return count;
}

int collectFloating(FallingBubble out[GRID_ROWS * GRID_COLS]) {
  bool anchored[GRID_ROWS][GRID_COLS];
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < GRID_COLS; col++) {
      anchored[row][col] = false;
    }
  }

  GridPos queue[GRID_ROWS * GRID_COLS];
  int head = 0;
  int tail = 0;
  for (int col = 0; col < rowBubbleCount(0); col++) {
    if (grid[0][col] < COLOR_COUNT) {
      anchored[0][col] = true;
      queue[tail++] = {0, col};
    }
  }

  while (head < tail) {
    GridPos current = queue[head++];
    GridPos neighbors[6];
    int neighborCount = 0;
    getNeighbors(current.row, current.col, neighbors, neighborCount);
    for (int i = 0; i < neighborCount; i++) {
      int nr = neighbors[i].row;
      int nc = neighbors[i].col;
      if (!inBounds(nr, nc) || !cellExists(nr, nc) || anchored[nr][nc]) {
        continue;
      }
      if (grid[nr][nc] >= COLOR_COUNT) {
        continue;
      }
      anchored[nr][nc] = true;
      queue[tail++] = {nr, nc};
    }
  }

  int dropped = 0;
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < rowBubbleCount(row); col++) {
      if (grid[row][col] < COLOR_COUNT && !anchored[row][col]) {
        if (out != nullptr) {
          out[dropped].active = true;
          out[dropped].x = cellCenterX(row, col);
          out[dropped].y = cellCenterY(row);
          out[dropped].vx = ((col & 1) ? 0.35f : -0.35f) + ((row % 3) - 1) * 0.18f;
          out[dropped].vy = 1.2f + (row * 0.05f);
          out[dropped].color = grid[row][col];
        }
        grid[row][col] = EMPTY_CELL;
        dropped++;
      }
    }
  }
  return dropped;
}

bool isPopAnimatingCell(int row, int col) {
  if (!popAnimating) {
    return false;
  }
  for (int i = 0; i < popBubbleCount; i++) {
    if (popBubbles[i].active && popBubbles[i].row == row && popBubbles[i].col == col) {
      return true;
    }
  }
  return false;
}

void startPopAnimation(const GridPos *cluster, int clusterCount, uint8_t color) {
  clearPopAnimation();
  clearFallAnimation();
  popAnimating = true;
  popAnimFrame = 0;
  popBubbleCount = min(clusterCount, GRID_ROWS * GRID_COLS);
  for (int i = 0; i < popBubbleCount; i++) {
    popBubbles[i].active = true;
    popBubbles[i].row = cluster[i].row;
    popBubbles[i].col = cluster[i].col;
    popBubbles[i].color = color;
  }
  pendingClusterScore = clusterCount * 10;
  pendingComboBonus = combo * 8;
}

void startFallAnimation(FallingBubble *bubbles, int count) {
  clearFallAnimation();
  fallAnimating = true;
  fallingBubbleCount = min(count, GRID_ROWS * GRID_COLS);
  for (int i = 0; i < fallingBubbleCount; i++) {
    fallingBubbles[i] = bubbles[i];
    fallingBubbles[i].active = true;
    fallingBubbles[i].vx *= 1.4f;
    fallingBubbles[i].vy += 2.8f;
  }
}

void updateFallAnimation() {
  if (!fallAnimating) {
    return;
  }

  fallAnimFrame++;
  bool anyVisible = false;
  for (int i = 0; i < fallingBubbleCount; i++) {
    if (!fallingBubbles[i].active) {
      continue;
    }
    fallingBubbles[i].x += fallingBubbles[i].vx;
    fallingBubbles[i].y += fallingBubbles[i].vy;
    fallingBubbles[i].vy += 1.8f;
    if (fallingBubbles[i].y - BUBBLE_R <= SCREEN_H + 8) {
      anyVisible = true;
    } else {
      fallingBubbles[i].active = false;
    }
  }

  if (!anyVisible) {
    clearFallAnimation();

    shot.active = false;

    if (boardCleared()) {
      score += 250;
      pushTopRow();
      pushTopRow();
    }

    normalizeUpcomingBubbles();

    if (reachedLoseLine()) {
      endGame("Bubble wall reached launcher");
    }
  }
}

void finishPopAnimation() {
  for (int i = 0; i < popBubbleCount; i++) {
    if (!popBubbles[i].active) {
      continue;
    }
    grid[popBubbles[i].row][popBubbles[i].col] = EMPTY_CELL;
  }

  FallingBubble droppedBubbles[GRID_ROWS * GRID_COLS];
  int dropped = collectFloating(droppedBubbles);
  score += pendingClusterScore + pendingComboBonus + dropped * 18 + popBubbleCount;
  combo++;
  playSfxClear();
  if (dropped > 0) {
    playSfxDrop();
    startVibration(140);
  }

  clearPopAnimation();
  if (dropped > 0) {
    startFallAnimation(droppedBubbles, dropped);
  } else {
    shot.active = false;

    if (boardCleared()) {
      score += 250;
      pushTopRow();
      pushTopRow();
    }

    normalizeUpcomingBubbles();

    if (reachedLoseLine()) {
      endGame("Bubble wall reached launcher");
    }
  }
}

bool boardCleared() {
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < rowBubbleCount(row); col++) {
      if (grid[row][col] < COLOR_COUNT) {
        return false;
      }
    }
  }
  return true;
}

bool reachedLoseLine() {
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < rowBubbleCount(row); col++) {
      if (grid[row][col] >= COLOR_COUNT) {
        continue;
      }
      if (cellCenterY(row) + BUBBLE_R >= PLAYER_LINE_Y) {
        return true;
      }
    }
  }
  return false;
}

bool snapShotToGrid(float x, float y, int &bestRow, int &bestCol) {
  float bestDist = 1e9f;
  bool found = false;
  bestRow = 0;
  bestCol = 0;
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < rowBubbleCount(row); col++) {
      if (grid[row][col] != EMPTY_CELL) {
        continue;
      }
      float dx = x - cellCenterX(row, col);
      float dy = y - cellCenterY(row);
      float d2 = dx * dx + dy * dy;
      if (d2 < bestDist) {
        bestDist = d2;
        bestRow = row;
        bestCol = col;
        found = true;
      }
    }
  }
  return found;
}

void resolvePlacement(int row, int col, uint8_t color) {
  grid[row][col] = color;
  playSfxStick();

  GridPos cluster[GRID_ROWS * GRID_COLS];
  int clusterCount = collectCluster(row, col, color, cluster);
  if (clusterCount >= 3) {
    startPopAnimation(cluster, clusterCount, color);
    shot.active = false;
    return;
  } else {
    combo = 0;
  }

  if (--shotsUntilDrop == 0) {
    pushTopRow();
    shotsUntilDrop = SHOTS_PER_DESCENT;
    startVibration(70);
  }

  shot.active = false;

  if (boardCleared()) {
    score += 250;
    pushTopRow();
    pushTopRow();
  }

  normalizeUpcomingBubbles();

  if (reachedLoseLine()) {
    endGame("Bubble wall reached launcher");
  }
}

void placeShotBubble() {
  int row = 0;
  int col = 0;
  if (!snapShotToGrid(shot.x, shot.y, row, col)) {
    endGame("No free slot for bubble");
    return;
  }
  resolvePlacement(row, col, shot.color);
}

void updateShot() {
  if (!shot.active) {
    return;
  }

  shot.x += shot.vx;
  shot.y += shot.vy;

  float leftWall = PLAY_X + BUBBLE_R;
  float rightWall = PLAY_X + PLAY_W - BUBBLE_R;
  if (shot.x <= leftWall) {
    shot.x = leftWall;
    shot.vx = fabsf(shot.vx);
  } else if (shot.x >= rightWall) {
    shot.x = rightWall;
    shot.vx = -fabsf(shot.vx);
  }

  if (shot.y <= PLAY_Y + BUBBLE_R) {
    shot.y = PLAY_Y + BUBBLE_R;
    placeShotBubble();
    return;
  }

  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < rowBubbleCount(row); col++) {
      if (grid[row][col] >= COLOR_COUNT) {
        continue;
      }
      float dx = shot.x - cellCenterX(row, col);
      float dy = shot.y - cellCenterY(row);
      float dist2 = dx * dx + dy * dy;
      float limit = (DIAMETER - 3);
      if (dist2 <= limit * limit) {
        placeShotBubble();
        return;
      }
    }
  }
}

void fireBubble() {
  if (shot.active) {
    return;
  }

  shot.active = true;
  shot.color = currentBubble;
  shot.vx = sinf(aimAngle) * BUBBLE_SPEED;
  shot.vy = -cosf(aimAngle) * BUBBLE_SPEED;
  shot.x = launcherCenterX() + sinf(aimAngle) * SHOT_SPAWN_OFFSET;
  shot.y = launcherCenterY() - cosf(aimAngle) * SHOT_SPAWN_OFFSET;
  currentBubble = nextBubble;
  nextBubble = randomPresentColor();
  playSfxShoot();
}

void swapBubble() {
  if (shot.active) {
    return;
  }
  uint8_t tmp = currentBubble;
  currentBubble = nextBubble;
  nextBubble = tmp;
  playSfxSwap();
}

void updateAim(unsigned long now) {
  float delta = 0.0f;
  if (joyLeftDown) {
    delta -= 0.062f;
  }
  if (joyRightDown) {
    delta += 0.062f;
  }

  if (delta != 0.0f && (lastAimStepMs == 0 || now - lastAimStepMs >= 18)) {
    aimAngle += delta;
    if (aimAngle < AIM_MIN) aimAngle = AIM_MIN;
    if (aimAngle > AIM_MAX) aimAngle = AIM_MAX;
    lastAimStepMs = now;
  } else if (delta == 0.0f) {
    lastAimStepMs = 0;
  }
}

void updatePlaying(unsigned long now) {
  if (popAnimating) {
    popAnimFrame++;
    if (popAnimFrame >= POP_ANIM_FRAMES) {
      finishPopAnimation();
    }
    return;
  }

  if (fallAnimating) {
    updateFallAnimation();
    return;
  }

  updateAim(now);
  if (greenPressed) {
    swapBubble();
  }
  if (bluePressed) {
    fireBubble();
  }
  updateShot();
}

template <typename CanvasT>
void drawBubble(CanvasT &canvas, int x, int y, uint16_t color, bool shine = true) {
  canvas.fillCircle(x, y, BUBBLE_R - 1, color);
  canvas.drawCircle(x, y, BUBBLE_R - 1, TFT_WHITE);
  canvas.drawCircle(x, y, BUBBLE_R - 2, rgb565(255, 255, 255));
  if (shine) {
    canvas.fillCircle(x - 4, y - 5, 4, rgb565(255, 255, 255));
    canvas.fillCircle(x + 4, y + 4, 2, rgb565(255, 214, 240));
  }
}

template <typename CanvasT>
void drawBackground(CanvasT &canvas) {
  uint16_t top = rgb565(24, 24, 42);
  uint16_t bottom = rgb565(10, 12, 24);
  for (int y = 0; y < SCREEN_H; y++) {
    float t = (float)y / SCREEN_H;
    uint8_t r = (uint8_t)(((top >> 11) & 0x1F) * 8 * (1.0f - t) + ((bottom >> 11) & 0x1F) * 8 * t);
    uint8_t g = (uint8_t)(((top >> 5) & 0x3F) * 4 * (1.0f - t) + ((bottom >> 5) & 0x3F) * 4 * t);
    uint8_t b = (uint8_t)((top & 0x1F) * 8 * (1.0f - t) + (bottom & 0x1F) * 8 * t);
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(r, g, b));
  }

  canvas.fillRoundRect(PLAY_X, PLAY_Y, PLAY_W, PLAY_H, 14, rgb565(18, 22, 38));
  canvas.drawRoundRect(PLAY_X, PLAY_Y, PLAY_W, PLAY_H, 14, rgb565(126, 185, 255));

  for (int i = 0; i < 22; i++) {
    int sx = (i * 37 + 17) % SCREEN_W;
    int sy = (i * 53 + 11) % 120;
    canvas.fillCircle(sx, sy, (i % 3) + 1, rgb565(180, 220, 255));
  }

  canvas.fillRoundRect(HUD_X, 14, HUD_W, 292, 16, rgb565(24, 30, 48));
  canvas.drawRoundRect(HUD_X, 14, HUD_W, 292, 16, rgb565(255, 210, 90));
}

template <typename CanvasT>
void drawGridBubbles(CanvasT &canvas) {
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < rowBubbleCount(row); col++) {
      uint8_t value = grid[row][col];
      if (value >= COLOR_COUNT) {
        continue;
      }
      if (isPopAnimatingCell(row, col)) {
        continue;
      }
      drawBubble(canvas, (int)cellCenterX(row, col), (int)cellCenterY(row), bubbleColors[value]);
    }
  }
}

template <typename CanvasT>
void drawPopEffects(CanvasT &canvas) {
  if (!popAnimating) {
    return;
  }

  float t = (float)popAnimFrame / (float)POP_ANIM_FRAMES;
  int coreRadius = max(2, (int)((BUBBLE_R - 2) * (1.0f - t * 0.75f)));
  int ringRadius = max(coreRadius + 2, (int)(BUBBLE_R - 1 + t * 8.0f));
  for (int i = 0; i < popBubbleCount; i++) {
    if (!popBubbles[i].active) {
      continue;
    }
    int x = (int)cellCenterX(popBubbles[i].row, popBubbles[i].col);
    int y = (int)cellCenterY(popBubbles[i].row);
    uint16_t base = bubbleColors[popBubbles[i].color];
    uint16_t glow = lighten565(base, 90);
    canvas.drawCircle(x, y, ringRadius, glow);
    canvas.drawCircle(x, y, max(2, ringRadius - 1), TFT_WHITE);
    canvas.fillCircle(x, y, coreRadius, scale565(base, 180));
    canvas.fillCircle(x - (int)(t * 6.0f), y - (int)(t * 5.0f), max(1, coreRadius / 3), TFT_WHITE);
    for (int p = 0; p < 4; p++) {
      float angle = p * (PI * 0.5f) + t;
      int px = x + (int)(cosf(angle) * (6.0f + t * 11.0f));
      int py = y + (int)(sinf(angle) * (6.0f + t * 11.0f));
      canvas.fillCircle(px, py, 2, glow);
    }
  }
}

template <typename CanvasT>
void drawFallingBubbles(CanvasT &canvas) {
  if (!fallAnimating) {
    return;
  }
  for (int i = 0; i < fallingBubbleCount; i++) {
    if (!fallingBubbles[i].active) {
      continue;
    }
    drawBubble(canvas, (int)fallingBubbles[i].x, (int)fallingBubbles[i].y, bubbleColors[fallingBubbles[i].color]);
  }
}

template <typename CanvasT>
void drawLauncher(CanvasT &canvas) {
  int baseX = launcherCenterX();
  int baseY = launcherCenterY();
  canvas.fillCircle(baseX, baseY, 26, rgb565(38, 44, 72));
  canvas.fillCircle(baseX, baseY, 18, rgb565(78, 92, 138));
  int tipX = baseX + (int)(sinf(aimAngle) * 42.0f);
  int tipY = baseY - (int)(cosf(aimAngle) * 42.0f);
  canvas.drawLine(baseX, baseY, tipX, tipY, rgb565(255, 236, 160));
  canvas.drawLine(baseX - 1, baseY, tipX - 1, tipY, rgb565(255, 236, 160));

  for (int i = 1; i <= 5; i++) {
    int dotX = baseX + (int)(sinf(aimAngle) * (48 + i * 18));
    int dotY = baseY - (int)(cosf(aimAngle) * (48 + i * 18));
    if (dotX < PLAY_X + 8 || dotX > PLAY_X + PLAY_W - 8 || dotY < PLAY_Y + 8) {
      break;
    }
    canvas.fillCircle(dotX, dotY, 2, rgb565(255, 245, 190));
  }

  drawBubble(canvas, baseX, baseY, bubbleColors[currentBubble]);
  drawBubble(canvas, HUD_X + HUD_W / 2, 212, bubbleColors[nextBubble]);
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(TFT_WHITE, rgb565(24, 30, 48));
  canvas.setTextSize(2);
  canvas.drawString("BUBBLE", HUD_X + 18, 28);
  canvas.drawString("SHOOTER", HUD_X + 18, 52);

  canvas.setTextColor(rgb565(255, 220, 120), rgb565(24, 30, 48));
  canvas.drawString("SCORE", HUD_X + 18, 96);
  canvas.setTextColor(TFT_WHITE, rgb565(24, 30, 48));
  canvas.drawNumber(score, HUD_X + 18, 120);

  canvas.setTextColor(rgb565(160, 240, 220), rgb565(24, 30, 48));
  canvas.drawString("DROP IN", HUD_X + 18, 156);
  canvas.setTextColor(TFT_WHITE, rgb565(24, 30, 48));
  canvas.drawNumber(shotsUntilDrop, HUD_X + 18, 180);

  canvas.setTextColor(rgb565(255, 195, 220), rgb565(24, 30, 48));
  canvas.drawString("NEXT", HUD_X + 18, 228);

  canvas.setTextSize(1);
  canvas.setTextColor(rgb565(210, 220, 255), rgb565(24, 30, 48));
  canvas.drawString("Blue: shoot", HUD_X + 18, 262);
  canvas.drawString("Green: swap", HUD_X + 18, 278);
  canvas.drawString("Stick: aim", HUD_X + 18, 294);
}

template <typename CanvasT>
void drawShot(CanvasT &canvas) {
  if (shot.active) {
    drawBubble(canvas, (int)shot.x, (int)shot.y, bubbleColors[shot.color]);
  }
}

template <typename CanvasT>
void drawPlayingScene(CanvasT &canvas) {
  drawBackground(canvas);
  drawGridBubbles(canvas);
  drawPopEffects(canvas);
  drawFallingBubbles(canvas);
  drawShot(canvas);
  drawLauncher(canvas);
  drawHud(canvas);
  canvas.drawFastHLine(PLAY_X + 14, PLAYER_LINE_Y + 25, PLAY_W - 28, rgb565(255, 92, 120));
}

void renderPlaying() {
  if (frameBufferReady) {
    drawPlayingScene(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    tft.fillScreen(TFT_BLACK);
    drawPlayingScene(tft);
  }
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  bool showPrompt = ((millis() - titlePulseMs) / 350) % 2 == 0;
  uint16_t bg = rgb565(8, 10, 22);
  uint16_t panel = rgb565(22, 28, 46);
  canvas.fillScreen(bg);
  canvas.fillRoundRect(42, 24, 396, 272, 20, panel);
  canvas.drawRoundRect(42, 24, 396, 272, 20, rgb565(255, 210, 90));
  for (int i = 0; i < 6; i++) {
    int x = 70 + i * 60;
    int y = 80 + (i % 2) * 20;
    drawBubble(canvas, x, y, bubbleColors[i % COLOR_COUNT]);
  }
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(rgb565(255, 240, 170), panel);
  canvas.setTextSize(3);
  canvas.drawString("BUBBLE SHOOTER", SCREEN_W / 2, 146);
  canvas.setTextSize(2);
  canvas.setTextColor(rgb565(200, 225, 255), panel);
  canvas.drawString("Blue starts", SCREEN_W / 2, 194);
  canvas.drawString("Green swaps  |  Stick aims", SCREEN_W / 2, 224);

  if (showPrompt) {
    canvas.setTextColor(rgb565(255, 180, 205), panel);
    canvas.drawString("PRESS BLUE", SCREEN_W / 2, 270);
  }
  lastTitlePromptVisible = showPrompt;
}

void renderTitle() {
  if (frameBufferReady) {
    drawTitle(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawTitle(tft);
  }
}

template <typename CanvasT>
void drawGameOver(CanvasT &canvas) {
  static const uint16_t rowColors[LEADERBOARD_SIZE] = {
    0xFD20,
    0x07FF,
    0xF81F,
    0xAFE5,
    0xFB40
  };
  bool showPrompt = ((millis() - gameOverPulseMs) / 350) % 2 == 0;
  uint16_t bg = rgb565(12, 10, 18);
  uint16_t panel = rgb565(24, 26, 44);
  uint16_t rowBg = rgb565(46, 52, 76);
  canvas.fillScreen(bg);

  canvas.fillRoundRect(18, 18, 166, 284, 16, panel);
  canvas.fillRoundRect(194, 18, 268, 284, 16, panel);

  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(rgb565(255, 160, 160), panel);
  canvas.setTextSize(3);
  canvas.drawString("GAME OVER", 101, 54);
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawString(endReason, 101, 92);
  canvas.setTextColor(rgb565(255, 220, 120), panel);
  canvas.setTextSize(2);
  canvas.drawString("THIS RUN", 101, 130);
  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawNumber(score, 101, 162, 4);

  if (latestRank >= 0) {
    char rankText[16];
    snprintf(rankText, sizeof(rankText), "RANK #%d", latestRank + 1);
    canvas.setTextColor(lighten565(rowColors[latestRank], 90), panel);
    canvas.setTextSize(2);
    canvas.drawString(rankText, 101, 220);
  } else {
    canvas.setTextColor(rgb565(180, 186, 220), panel);
    canvas.setTextSize(2);
    canvas.drawString("KEEP CLIMBING", 101, 220);
  }

  if (latestRank == 0) {
    canvas.setTextColor(rgb565(255, 220, 120), panel);
    canvas.setTextSize(1);
    canvas.drawString("NEW HIGH SCORE", 101, 260);
  } else if (latestRank > 0) {
    canvas.setTextColor(rgb565(255, 220, 120), panel);
    canvas.setTextSize(1);
    canvas.drawString("NEW TOP 5 SCORE", 101, 260);
  }

  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(rgb565(255, 220, 120), panel);
  canvas.setTextSize(2);
  canvas.drawString("LEADERBOARD", 214, 34);
  canvas.setTextColor(rgb565(188, 204, 255), panel);
  canvas.drawString("RANK", 218, 68);
  canvas.drawString("SCORE", 318, 68);

  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    int y = 100 + i * 34;
    char rankLabel[4];
    snprintf(rankLabel, sizeof(rankLabel), "%d.", i + 1);
    uint16_t rowColor = rowColors[i];
    uint16_t fillColor = (i == latestRank) ? scale565(rowColor, 170) : rowBg;
    uint16_t textColor = (i == latestRank) ? lighten565(rowColor, 110) : TFT_WHITE;
    canvas.fillRoundRect(210, y - 4, 238, 26, 8, fillColor);
    canvas.setTextColor(textColor, fillColor);
    canvas.setTextSize(2);
    canvas.drawString(rankLabel, 224, y);
    canvas.setTextDatum(TR_DATUM);
    canvas.drawNumber(leaderboard[i], 430, y);
    canvas.setTextDatum(TL_DATUM);
    if (i == latestRank) {
      canvas.fillTriangle(200, y + 8, 214, y, 214, y + 16, rowColor);
      canvas.fillCircle(440, y + 8, 6, lighten565(rowColor, 90));
    }
  }

  if (showPrompt) {
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(rgb565(255, 220, 120), bg);
    canvas.setTextSize(2);
    canvas.drawString("BLUE TO RETRY", SCREEN_W / 2, 292);
  }
  lastGameOverPromptVisible = showPrompt;
}

void renderGameOver() {
  if (frameBufferReady) {
    drawGameOver(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawGameOver(tft);
  }
}

void setup() {
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);
  initFrameBuffer();

  setupHardware();
  titlePulseMs = millis();
  renderTitle();
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
  unsigned long now = millis();

  if (gameState == TITLE) {
    if (bluePressed) {
      beginGame();
    }
  } else if (gameState == PLAYING) {
    if (now - lastUpdateMs >= UPDATE_MS) {
      lastUpdateMs = now;
      updatePlaying(now);
    }
  } else if (gameState == GAME_OVER) {
    if (bluePressed) {
      beginGame();
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
  } else if (gameState == GAME_OVER) {
    bool promptVisible = ((now - gameOverPulseMs) / 350) % 2 == 0;
    if (lastRenderedState != GAME_OVER || promptVisible != lastGameOverPromptVisible) {
      renderGameOver();
      lastRenderedState = GAME_OVER;
    }
  }

  delay(1);
}
