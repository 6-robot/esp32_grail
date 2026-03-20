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
TFT_eSprite titleRightBuffer = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int MAZE_COLS = 19;
static const int MAZE_ROWS = 15;
static const int TILE_SIZE = 20;
static const int MAZE_W = MAZE_COLS * TILE_SIZE;
static const int MAZE_H = MAZE_ROWS * TILE_SIZE;
static const int MAZE_X = 8;
static const int MAZE_Y = 10;
static const int HUD_X = MAZE_X + MAZE_W + 8;
static const int HUD_W = SCREEN_W - HUD_X - 8;

static const uint16_t GAME_UPDATE_INTERVAL_MS = 16;
static constexpr float PLAYER_SPEED_PX = 5.40f;
static constexpr float GHOST_SPEED_PX = 4.40f;
static constexpr float FRIGHTENED_SPEED_PX = 3.28f;
static constexpr float EATEN_GHOST_SPEED_PX = 7.40f;
static const uint16_t POWER_MODE_MS = 7000;
static const uint16_t RESPAWN_DELAY_MS = 1200;
static const uint16_t LEVEL_CLEAR_DELAY_MS = 1600;
static const uint8_t PLAYER_LIVES = 3;
static const uint8_t GHOST_COUNT = 4;
static const uint8_t LEADERBOARD_SIZE = 5;
static const uint16_t TITLE_BG = TFT_BLACK;

enum GameState {
  TITLE,
  READY,
  PLAYING,
  LEVEL_CLEAR,
  GAME_OVER
};

enum Direction : uint8_t {
  DIR_UP = 0,
  DIR_RIGHT = 1,
  DIR_DOWN = 2,
  DIR_LEFT = 3,
  DIR_NONE = 4
};

enum TileType : uint8_t {
  TILE_WALL = 0,
  TILE_DOT = 1,
  TILE_EMPTY = 2,
  TILE_POWER = 3
};

enum GhostMode : uint8_t {
  GHOST_CHASE = 0,
  GHOST_FRIGHTENED = 1,
  GHOST_EATEN = 2
};

enum WaveType : uint8_t {
  WAVE_SQUARE = 0,
  WAVE_TRIANGLE = 1,
  WAVE_NOISE = 2
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

struct Actor {
  float x;
  float y;
  int col;
  int row;
  int targetCol;
  int targetRow;
  Direction dir;
  Direction desiredDir;
  bool moving;
};

struct Ghost {
  Actor actor;
  uint8_t homeCol;
  uint8_t homeRow;
  uint16_t color;
  GhostMode mode;
};

static const char *MAZE_TEMPLATE[MAZE_ROWS] = {
  "###################",
  "#o....#.....#....o#",
  "#.###.#.###.#.###.#",
  "#.................#",
  "#.###.#.###.#.###.#",
  "#.....#.....#.....#",
  "###.#.### ###.#.###",
  "#...#..     ..#...#",
  "###.#.# ### #.#.###",
  "#.......# #.......#",
  "#.###.#.# #.#.###.#",
  "#o..#.#.....#.#..o#",
  "##.#.#.#####.#.#.##",
  "#........#........#",
  "###################"
};

TileType maze[MAZE_ROWS][MAZE_COLS];

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;

Actor player;
Ghost ghosts[GHOST_COUNT];

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
int latestX = 2048;
int latestY = 2048;

bool frameBufferReady = false;
bool titleRightBufferReady = false;
bool poweredUp = false;
bool paused = false;
bool mouthOpen = true;
uint16_t score = 0;
uint8_t lives = PLAYER_LIVES;
uint16_t dotsRemaining = 0;
uint8_t levelNumber = 1;
uint16_t leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int8_t latestRank = -1;
bool scoreSubmitted = false;
unsigned long powerModeEndMs = 0;
unsigned long stateTimerMs = 0;
unsigned long lastGameUpdateMs = 0;
unsigned long lastMouthToggleMs = 0;
char bannerText[24] = "";

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
bool audioEnabled = true;

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
  {392, 120}, {494, 120}, {523, 120}, {587, 120},
  {523, 120}, {494, 120}, {392, 120}, {330, 120},
  {392, 120}, {494, 120}, {587, 120}, {659, 160},
  {523, 120}, {494, 120}, {392, 180}, {0, 60}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

const int dRow[4] = {-1, 0, 1, 0};
const int dCol[4] = {0, 1, 0, -1};

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

bool isInside(int col, int row) {
  return col >= 0 && col < MAZE_COLS && row >= 0 && row < MAZE_ROWS;
}

bool isWalkable(int col, int row) {
  return isInside(col, row) && maze[row][col] != TILE_WALL;
}

float tileCenterX(int col) {
  return (float)(MAZE_X + col * TILE_SIZE + TILE_SIZE / 2);
}

float tileCenterY(int row) {
  return (float)(MAZE_Y + row * TILE_SIZE + TILE_SIZE / 2);
}

void snapActorToTile(Actor &actor, int col, int row) {
  actor.col = col;
  actor.row = row;
  actor.targetCol = col;
  actor.targetRow = row;
  actor.x = tileCenterX(col);
  actor.y = tileCenterY(row);
  actor.moving = false;
}

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startVibration(uint16_t durationMs = 150) {
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
    const int duty = 150;
    if (vibForward) {
      ledcWrite(forward_ch, duty);
      ledcWrite(reverse_ch, 0);
    } else {
      ledcWrite(forward_ch, 0);
      ledcWrite(reverse_ch, duty);
    }
  }
}

void enqueueVoice(float freq, int ms, float volume, WaveType wave, float slidePerSample = 0.0f) {
  portENTER_CRITICAL(&audioMux);
  for (int i = 0; i < 6; ++i) {
    if (!sfxVoices[i].active) {
      sfxVoices[i].active = true;
      sfxVoices[i].freq = freq;
      sfxVoices[i].phase = 0.0f;
      sfxVoices[i].remainSamples = (SAMPLE_RATE * ms) / 1000;
      sfxVoices[i].volume = volume;
      sfxVoices[i].wave = wave;
      sfxVoices[i].slidePerSample = slidePerSample;
      break;
    }
  }
  portEXIT_CRITICAL(&audioMux);
}

void playDotSfx() {
  enqueueVoice(780.0f, 28, 0.09f, WAVE_SQUARE, 0.02f);
}

void playPowerSfx() {
  enqueueVoice(280.0f, 140, 0.11f, WAVE_TRIANGLE, 0.12f);
  enqueueVoice(420.0f, 160, 0.09f, WAVE_SQUARE, 0.05f);
}

void playGhostEatenSfx() {
  enqueueVoice(880.0f, 90, 0.12f, WAVE_TRIANGLE, -0.03f);
  enqueueVoice(1240.0f, 120, 0.09f, WAVE_SQUARE, -0.06f);
}

void playDeathSfx() {
  enqueueVoice(520.0f, 320, 0.11f, WAVE_NOISE, -0.04f);
  enqueueVoice(360.0f, 420, 0.10f, WAVE_TRIANGLE, -0.03f);
}

void playClearSfx() {
  enqueueVoice(660.0f, 120, 0.10f, WAVE_TRIANGLE);
  enqueueVoice(880.0f, 120, 0.10f, WAVE_TRIANGLE);
  enqueueVoice(1320.0f, 180, 0.10f, WAVE_SQUARE);
}

float sampleVoice(Voice &voice) {
  if (!voice.active || voice.remainSamples <= 0) {
    voice.active = false;
    return 0.0f;
  }

  float sample = 0.0f;
  switch (voice.wave) {
    case WAVE_SQUARE:
      sample = (voice.phase < 0.5f) ? 1.0f : -1.0f;
      break;
    case WAVE_TRIANGLE:
      sample = 1.0f - 4.0f * fabsf(voice.phase - 0.5f);
      break;
    case WAVE_NOISE:
      sample = (float)random(-1000, 1001) / 1000.0f;
      break;
  }

  float out = sample * voice.volume;
  voice.phase += voice.freq / SAMPLE_RATE;
  if (voice.phase >= 1.0f) {
    voice.phase -= floorf(voice.phase);
  }
  voice.freq += voice.slidePerSample;
  if (voice.freq < 30.0f) {
    voice.freq = 30.0f;
  }
  --voice.remainSamples;
  if (voice.remainSamples <= 0) {
    voice.active = false;
  }
  return out;
}

float bgmSample() {
  if (!audioEnabled || gameState != PLAYING || paused) {
    return 0.0f;
  }

  if (bgmRemainSamples <= 0) {
    const Note &note = bgmLoop[bgmIndex];
    bgmRemainSamples = max(1, (int)((SAMPLE_RATE * note.ms) / 1000));
    if (note.freq == 0) {
      bgmPhase = 0.0f;
    }
    bgmIndex = (bgmIndex + 1) % BGM_NOTE_COUNT;
  }

  const Note &active = bgmLoop[(bgmIndex + BGM_NOTE_COUNT - 1) % BGM_NOTE_COUNT];
  --bgmRemainSamples;
  if (active.freq == 0) {
    return 0.0f;
  }

  bgmPhase += (float)active.freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  float base = (bgmPhase < 0.5f) ? 1.0f : -1.0f;
  return base * 0.0225f;
}

void audioTick() {
  size_t written = 0;
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    float mix = bgmSample();
    portENTER_CRITICAL(&audioMux);
    for (int j = 0; j < 6; ++j) {
      mix += sampleVoice(sfxVoices[j]);
    }
    portEXIT_CRITICAL(&audioMux);

    if (mix > 1.0f) {
      mix = 1.0f;
    } else if (mix < -1.0f) {
      mix = -1.0f;
    }
    audioBuffer[i] = (int16_t)(mix * 32767.0f);
  }
  i2s_write(I2S_NUM_1, audioBuffer, sizeof(audioBuffer), &written, portMAX_DELAY);
}

void audioTaskMain(void *param) {
  (void)param;
  while (true) {
    audioTick();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void initAudio() {
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, nullptr);
  if (err != ESP_OK) {
    audioEnabled = false;
    return;
  }
  err = i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);
  if (err != ESP_OK) {
    audioEnabled = false;
    return;
  }
  i2s_zero_dma_buffer(I2S_NUM_1);
  xTaskCreatePinnedToCore(audioTaskMain, "audio_task", 4096, nullptr, 2, &audioTaskHandle, 0);
}

void initFrameBuffer() {
  frameBuffer.setColorDepth(16);
  if (frameBuffer.createSprite(SCREEN_W, SCREEN_H)) {
    frameBufferReady = true;
  } else {
    frameBuffer.deleteSprite();
    frameBuffer.setColorDepth(8);
    frameBufferReady = frameBuffer.createSprite(SCREEN_W, SCREEN_H);
  }

  titleRightBuffer.setColorDepth(16);
  if (titleRightBuffer.createSprite(SCREEN_W - 320, SCREEN_H)) {
    titleRightBufferReady = true;
    return;
  }
  titleRightBuffer.deleteSprite();
  titleRightBuffer.setColorDepth(8);
  titleRightBufferReady = titleRightBuffer.createSprite(SCREEN_W - 320, SCREEN_H);
}

void readInputs() {
  bool g = digitalRead(green_btn_pin) == LOW;
  bool b = digitalRead(blue_btn_pin) == LOW;
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);
  greenPressed = g && !greenDown;
  bluePressed = b && !blueDown;
  greenDown = g;
  blueDown = b;
}

Direction readJoystickDirection() {
  if (latestY < 1500) {
    return DIR_UP;
  }
  if (latestY > 2600) {
    return DIR_DOWN;
  }
  if (latestX < 1500) {
    return DIR_LEFT;
  }
  if (latestX > 2600) {
    return DIR_RIGHT;
  }
  return DIR_NONE;
}

void loadMaze() {
  dotsRemaining = 0;
  for (int row = 0; row < MAZE_ROWS; ++row) {
    for (int col = 0; col < MAZE_COLS; ++col) {
      char c = MAZE_TEMPLATE[row][col];
      TileType tile = TILE_EMPTY;
      if (c == '#') {
        tile = TILE_WALL;
      } else if (c == '.') {
        tile = TILE_DOT;
        ++dotsRemaining;
      } else if (c == 'o') {
        tile = TILE_POWER;
        ++dotsRemaining;
      } else {
        tile = TILE_EMPTY;
      }
      maze[row][col] = tile;
    }
  }
}

void resetActors() {
  snapActorToTile(player, 9, 11);
  player.dir = DIR_LEFT;
  player.desiredDir = DIR_LEFT;

  snapActorToTile(ghosts[0].actor, 9, 7);
  ghosts[0].homeCol = 9;
  ghosts[0].homeRow = 7;
  ghosts[0].color = rgb565(255, 64, 64);
  ghosts[0].mode = GHOST_CHASE;
  ghosts[0].actor.dir = DIR_LEFT;
  ghosts[0].actor.desiredDir = DIR_LEFT;

  snapActorToTile(ghosts[1].actor, 8, 7);
  ghosts[1].homeCol = 8;
  ghosts[1].homeRow = 7;
  ghosts[1].color = rgb565(255, 160, 80);
  ghosts[1].mode = GHOST_CHASE;
  ghosts[1].actor.dir = DIR_RIGHT;
  ghosts[1].actor.desiredDir = DIR_RIGHT;

  snapActorToTile(ghosts[2].actor, 10, 7);
  ghosts[2].homeCol = 10;
  ghosts[2].homeRow = 7;
  ghosts[2].color = rgb565(80, 220, 255);
  ghosts[2].mode = GHOST_CHASE;
  ghosts[2].actor.dir = DIR_LEFT;
  ghosts[2].actor.desiredDir = DIR_LEFT;

  snapActorToTile(ghosts[3].actor, 9, 8);
  ghosts[3].homeCol = 9;
  ghosts[3].homeRow = 8;
  ghosts[3].color = rgb565(255, 120, 210);
  ghosts[3].mode = GHOST_CHASE;
  ghosts[3].actor.dir = DIR_UP;
  ghosts[3].actor.desiredDir = DIR_UP;

  poweredUp = false;
  powerModeEndMs = 0;
}

void submitScore() {
  if (scoreSubmitted) {
    return;
  }

  latestRank = -1;
  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
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

void startLevel(bool keepScore) {
  if (!keepScore) {
    score = 0;
    levelNumber = 1;
    lives = PLAYER_LIVES;
    latestRank = -1;
    scoreSubmitted = false;
  }

  loadMaze();
  resetActors();
  paused = false;
  gameState = READY;
  stateTimerMs = millis() + 1200;
  snprintf(bannerText, sizeof(bannerText), "READY!");
}

bool canMove(Direction dir, int col, int row) {
  if (dir == DIR_NONE) {
    return false;
  }
  int nextCol = col + dCol[dir];
  int nextRow = row + dRow[dir];
  return isWalkable(nextCol, nextRow);
}

bool startActorMove(Actor &actor, Direction dir) {
  if (!canMove(dir, actor.col, actor.row)) {
    return false;
  }
  actor.dir = dir;
  actor.targetCol = actor.col + dCol[dir];
  actor.targetRow = actor.row + dRow[dir];
  actor.moving = true;
  return true;
}

bool advanceActor(Actor &actor, float speedPx) {
  if (!actor.moving) {
    Direction dirToTry = actor.dir;
    if (canMove(actor.desiredDir, actor.col, actor.row)) {
      dirToTry = actor.desiredDir;
    }
    if (!startActorMove(actor, dirToTry)) {
      actor.dir = canMove(actor.desiredDir, actor.col, actor.row) ? actor.desiredDir : DIR_NONE;
      return false;
    }
  }

  float targetX = tileCenterX(actor.targetCol);
  float targetY = tileCenterY(actor.targetRow);
  float dx = targetX - actor.x;
  float dy = targetY - actor.y;
  float distance = sqrtf(dx * dx + dy * dy);
  if (distance <= speedPx) {
    actor.x = targetX;
    actor.y = targetY;
    actor.col = actor.targetCol;
    actor.row = actor.targetRow;
    actor.moving = false;
    return true;
  }

  if (distance > 0.0f) {
    actor.x += (dx / distance) * speedPx;
    actor.y += (dy / distance) * speedPx;
  }
  return false;
}

bool isOpposite(Direction a, Direction b) {
  return (a == DIR_UP && b == DIR_DOWN) ||
         (a == DIR_DOWN && b == DIR_UP) ||
         (a == DIR_LEFT && b == DIR_RIGHT) ||
         (a == DIR_RIGHT && b == DIR_LEFT);
}

Direction chooseGhostDirection(const Ghost &ghost, int targetCol, int targetRow, bool randomize) {
  Direction bestDir = DIR_NONE;
  int bestScore = 32767;

  for (int dir = 0; dir < 4; ++dir) {
    if (!canMove((Direction)dir, ghost.actor.col, ghost.actor.row)) {
      continue;
    }
    if (ghost.mode != GHOST_EATEN && isOpposite((Direction)dir, ghost.actor.dir)) {
      continue;
    }

    int nextCol = ghost.actor.col + dCol[dir];
    int nextRow = ghost.actor.row + dRow[dir];
    int scoreValue;
    if (randomize) {
      scoreValue = random(0, 1000);
    } else {
      scoreValue = abs(targetCol - nextCol) + abs(targetRow - nextRow);
    }

    if (scoreValue < bestScore) {
      bestScore = scoreValue;
      bestDir = (Direction)dir;
    }
  }

  if (bestDir == DIR_NONE) {
    for (int dir = 0; dir < 4; ++dir) {
      if (canMove((Direction)dir, ghost.actor.col, ghost.actor.row)) {
        return (Direction)dir;
      }
    }
  }
  return bestDir;
}

void frightenGhosts() {
  poweredUp = true;
  powerModeEndMs = millis() + POWER_MODE_MS;
  for (int i = 0; i < GHOST_COUNT; ++i) {
    if (ghosts[i].mode != GHOST_EATEN) {
      ghosts[i].mode = GHOST_FRIGHTENED;
    }
  }
}

void eatTile() {
  TileType &tile = maze[player.row][player.col];
  if (tile == TILE_DOT) {
    tile = TILE_EMPTY;
    score += 10;
    if (dotsRemaining > 0) {
      --dotsRemaining;
    }
    playDotSfx();
  } else if (tile == TILE_POWER) {
    tile = TILE_EMPTY;
    score += 50;
    if (dotsRemaining > 0) {
      --dotsRemaining;
    }
    frightenGhosts();
    playPowerSfx();
    startVibration(120);
  }

  if (dotsRemaining == 0) {
    ++levelNumber;
    gameState = LEVEL_CLEAR;
    stateTimerMs = millis() + LEVEL_CLEAR_DELAY_MS;
    snprintf(bannerText, sizeof(bannerText), "LEVEL CLEAR");
    playClearSfx();
    startVibration(240);
  }
}

void loseLife() {
  if (lives > 0) {
    --lives;
  }
  playDeathSfx();
  startVibration(450);
  if (lives == 0) {
    submitScore();
    gameState = GAME_OVER;
    snprintf(bannerText, sizeof(bannerText), "GAME OVER");
    return;
  }
  gameState = READY;
  stateTimerMs = millis() + RESPAWN_DELAY_MS;
  snprintf(bannerText, sizeof(bannerText), "TRY AGAIN");
  resetActors();
}

void checkGhostCollisions() {
  for (int i = 0; i < GHOST_COUNT; ++i) {
    float dx = ghosts[i].actor.x - player.x;
    float dy = ghosts[i].actor.y - player.y;
    if ((dx * dx + dy * dy) > 100.0f) {
      continue;
    }

    if (ghosts[i].mode == GHOST_FRIGHTENED) {
      ghosts[i].mode = GHOST_EATEN;
      score += 200;
      playGhostEatenSfx();
      startVibration(90);
    } else if (ghosts[i].mode != GHOST_EATEN) {
      loseLife();
      return;
    }
  }
}

void updatePlayer() {
  Direction joystickDir = readJoystickDirection();
  if (joystickDir != DIR_NONE) {
    player.desiredDir = joystickDir;
  }
  bool reachedNewTile = advanceActor(player, PLAYER_SPEED_PX);
  if (reachedNewTile) {
    eatTile();
  }
  checkGhostCollisions();
}

void updateGhosts() {
  for (int i = 0; i < GHOST_COUNT; ++i) {
    Ghost &ghost = ghosts[i];
    if (!ghost.actor.moving) {
      int targetCol = player.col;
      int targetRow = player.row;
      bool randomize = false;

      if (ghost.mode == GHOST_EATEN) {
        targetCol = ghost.homeCol;
        targetRow = ghost.homeRow;
      } else if (ghost.mode == GHOST_FRIGHTENED) {
        randomize = true;
      } else if (i == 1) {
        targetCol = player.col + 2;
        targetRow = player.row;
      } else if (i == 2) {
        targetCol = player.col;
        targetRow = player.row + 2;
      } else if (i == 3) {
        targetCol = player.col - 2;
        targetRow = player.row;
      }

      ghost.actor.desiredDir = chooseGhostDirection(ghost, targetCol, targetRow, randomize);
    }

    float speed = GHOST_SPEED_PX;
    if (ghost.mode == GHOST_FRIGHTENED) {
      speed = FRIGHTENED_SPEED_PX;
    } else if (ghost.mode == GHOST_EATEN) {
      speed = EATEN_GHOST_SPEED_PX;
    }

    bool reachedNewTile = advanceActor(ghost.actor, speed);
    if (reachedNewTile &&
        ghost.mode == GHOST_EATEN &&
        ghost.actor.col == ghost.homeCol &&
        ghost.actor.row == ghost.homeRow) {
      ghost.mode = poweredUp ? GHOST_FRIGHTENED : GHOST_CHASE;
    }
  }

  checkGhostCollisions();
}

void updatePowerMode() {
  if (!poweredUp) {
    return;
  }
  if (millis() < powerModeEndMs) {
    return;
  }
  poweredUp = false;
  for (int i = 0; i < GHOST_COUNT; ++i) {
    if (ghosts[i].mode == GHOST_FRIGHTENED) {
      ghosts[i].mode = GHOST_CHASE;
    }
  }
}

template <typename CanvasT>
void drawWall(CanvasT &canvas, int x, int y) {
  uint16_t fill = rgb565(10, 22, 120);
  uint16_t edge = rgb565(60, 180, 255);
  canvas.fillRoundRect(x + 1, y + 1, TILE_SIZE - 2, TILE_SIZE - 2, 4, fill);
  canvas.drawRoundRect(x + 1, y + 1, TILE_SIZE - 2, TILE_SIZE - 2, 4, edge);
}

template <typename CanvasT>
void drawPellet(CanvasT &canvas, int x, int y, bool power) {
  uint16_t c = rgb565(255, 225, 170);
  int r = power ? 4 : 2;
  canvas.fillCircle(x + TILE_SIZE / 2, y + TILE_SIZE / 2, r, c);
}

template <typename CanvasT>
void drawPlayer(CanvasT &canvas) {
  int px = (int)lroundf(player.x);
  int py = (int)lroundf(player.y);
  uint16_t body = rgb565(255, 220, 40);
  canvas.fillCircle(px, py, 8, body);
  if (!mouthOpen) {
    return;
  }

  int mx = px;
  int my = py;
  switch (player.dir) {
    case DIR_RIGHT:
      canvas.fillTriangle(mx, my, mx + 9, my - 6, mx + 9, my + 6, TFT_BLACK);
      break;
    case DIR_LEFT:
      canvas.fillTriangle(mx, my, mx - 9, my - 6, mx - 9, my + 6, TFT_BLACK);
      break;
    case DIR_UP:
      canvas.fillTriangle(mx, my, mx - 6, my - 9, mx + 6, my - 9, TFT_BLACK);
      break;
    case DIR_DOWN:
      canvas.fillTriangle(mx, my, mx - 6, my + 9, mx + 6, my + 9, TFT_BLACK);
      break;
    case DIR_NONE:
      break;
  }
}

template <typename CanvasT>
void drawGhost(CanvasT &canvas, const Ghost &ghost, bool blink) {
  int gx = (int)lroundf(ghost.actor.x) - 8;
  int gy = (int)lroundf(ghost.actor.y) - 8;
  uint16_t c = ghost.color;
  if (ghost.mode == GHOST_FRIGHTENED) {
    c = blink ? rgb565(240, 240, 255) : rgb565(40, 80, 255);
  } else if (ghost.mode == GHOST_EATEN) {
    c = rgb565(50, 50, 70);
  }

  canvas.fillCircle(gx + 8, gy + 8, 8, c);
  canvas.fillRect(gx, gy + 8, 16, 8, c);
  for (int i = 0; i < 4; ++i) {
    canvas.fillCircle(gx + 2 + i * 4, gy + 15, 2, c);
  }

  uint16_t eyeWhite = ghost.mode == GHOST_EATEN ? rgb565(120, 120, 160) : TFT_WHITE;
  uint16_t eyeBlue = ghost.mode == GHOST_FRIGHTENED ? rgb565(255, 180, 180) : rgb565(40, 120, 255);
  canvas.fillCircle(gx + 5, gy + 8, 3, eyeWhite);
  canvas.fillCircle(gx + 11, gy + 8, 3, eyeWhite);
  canvas.fillCircle(gx + 6, gy + 8, 1, eyeBlue);
  canvas.fillCircle(gx + 12, gy + 8, 1, eyeBlue);
}

template <typename CanvasT>
void drawMaze(CanvasT &canvas) {
  canvas.fillScreen(TFT_BLACK);
  canvas.fillRoundRect(MAZE_X - 4, MAZE_Y - 4, MAZE_W + 8, MAZE_H + 8, 8, rgb565(6, 8, 30));

  for (int row = 0; row < MAZE_ROWS; ++row) {
    for (int col = 0; col < MAZE_COLS; ++col) {
      int x = MAZE_X + col * TILE_SIZE;
      int y = MAZE_Y + row * TILE_SIZE;
      if (maze[row][col] == TILE_WALL) {
        drawWall(canvas, x, y);
      } else if (maze[row][col] == TILE_DOT) {
        drawPellet(canvas, x, y, false);
      } else if (maze[row][col] == TILE_POWER) {
        drawPellet(canvas, x, y, true);
      }
    }
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  uint16_t panel = rgb565(18, 22, 40);
  uint16_t accent = rgb565(255, 210, 60);
  canvas.fillRoundRect(HUD_X, 10, HUD_W, SCREEN_H - 20, 10, panel);
  canvas.setTextColor(accent, panel);
  canvas.setTextDatum(TL_DATUM);
  canvas.drawString("PACMAN", HUD_X + 16, 22, 2);

  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawString("Score", HUD_X + 16, 58, 2);
  canvas.drawNumber(score, HUD_X + 16, 78, 4);
  canvas.drawString("Lives", HUD_X + 16, 118, 2);
  canvas.drawNumber(lives, HUD_X + 16, 138, 4);
  canvas.drawString("Level", HUD_X + 16, 178, 2);
  canvas.drawNumber(levelNumber, HUD_X + 16, 198, 4);
  canvas.drawString("Dots", HUD_X + 16, 238, 2);
  canvas.drawNumber(dotsRemaining, HUD_X + 16, 258, 4);

  if (gameState == READY || gameState == LEVEL_CLEAR || gameState == GAME_OVER || paused) {
    canvas.setTextColor(rgb565(255, 160, 120), panel);
    const char *msg = paused ? "PAUSED" : bannerText;
    canvas.drawString(msg, HUD_X + 16, 294, 2);
  } else if (poweredUp) {
    canvas.setTextColor(rgb565(120, 200, 255), panel);
    canvas.drawString("POWER!", HUD_X + 16, 294, 2);
  } else {
    canvas.setTextColor(rgb565(150, 150, 180), panel);
    canvas.drawString("Blue: start", HUD_X + 16, 294, 2);
  }
}

template <typename CanvasT>
void drawScene(CanvasT &canvas) {
  drawMaze(canvas);
  bool blink = poweredUp && ((millis() / 180) % 2 == 0);
  drawPlayer(canvas);
  for (int i = 0; i < GHOST_COUNT; ++i) {
    drawGhost(canvas, ghosts[i], blink);
  }
  drawHud(canvas);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  canvas.fillScreen(TITLE_BG);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(rgb565(255, 210, 60), TITLE_BG);
  canvas.drawString("PACMAN", SCREEN_W / 2, 70, 7);
  canvas.setTextColor(TFT_WHITE, TITLE_BG);
  canvas.drawString("Blue: Start", SCREEN_W / 2, 150, 4);
  canvas.drawString("Green: Pause / Resume", SCREEN_W / 2, 188, 2);
  canvas.drawString("Joystick: Move", SCREEN_W / 2, 214, 2);
  canvas.drawString("Eat all dots, avoid ghosts.", SCREEN_W / 2, 254, 2);
  canvas.fillCircle(SCREEN_W / 2 - 88, 150, 14, rgb565(255, 220, 40));
  canvas.fillTriangle(SCREEN_W / 2 - 88, 150, SCREEN_W / 2 - 72, 141, SCREEN_W / 2 - 72, 159, TITLE_BG);
  canvas.fillCircle(SCREEN_W / 2 + 86, 150, 14, rgb565(255, 80, 80));
  canvas.fillRect(SCREEN_W / 2 + 72, 150, 28, 14, rgb565(255, 80, 80));
}

template <typename CanvasT>
void drawTitleRight(CanvasT &canvas) {
  uint16_t bg = TITLE_BG;
  uint16_t pellet = rgb565(255, 225, 170);
  uint16_t ghost = rgb565(255, 80, 80);
  canvas.fillScreen(bg);
  for (int y = 46; y <= 274; y += 38) {
    canvas.fillCircle(40, y, 3, pellet);
    canvas.fillCircle(120, y + 19, 2, pellet);
  }
  canvas.fillCircle(86, 132, 18, ghost);
  canvas.fillRect(68, 132, 36, 18, ghost);
  for (int i = 0; i < 4; ++i) {
    canvas.fillCircle(72 + i * 10, 149, 4, ghost);
  }
  canvas.fillCircle(78, 132, 5, TFT_WHITE);
  canvas.fillCircle(94, 132, 5, TFT_WHITE);
  canvas.fillCircle(80, 133, 2, rgb565(40, 120, 255));
  canvas.fillCircle(96, 133, 2, rgb565(40, 120, 255));
}

template <typename CanvasT>
void drawGameOver(CanvasT &canvas) {
  static const uint16_t rowColors[LEADERBOARD_SIZE] = {
    0xFD20,  // warm gold
    0x07FF,  // cyan
    0xF81F,  // magenta
    0xAFE5,  // mint
    0xFB40   // orange
  };
  uint16_t bg = rgb565(14, 18, 34);
  uint16_t rowBg = rgb565(52, 60, 86);
  canvas.fillScreen(bg);
  canvas.fillRect(320, 0, SCREEN_W - 320, SCREEN_H, bg);
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(TFT_YELLOW, bg);
  canvas.drawString("LEADERBOARD", 92, 22, 4);
  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawString("PACMAN SCORE RUN", 112, 60, 2);
  canvas.drawString("THIS RUN", 78, 92, 2);
  canvas.drawNumber(score, 198, 84, 4);

  canvas.drawString("RANK", 86, 126, 2);
  canvas.drawString("SCORE", 170, 126, 2);
  canvas.setTextDatum(TL_DATUM);
  for (int i = 0; i < LEADERBOARD_SIZE; ++i) {
    int y = 154 + i * 30;
    char rankLabel[4];
    snprintf(rankLabel, sizeof(rankLabel), "%d.", i + 1);
    uint16_t rowColor = rowColors[i];
    uint16_t fillColor = (i == latestRank) ? scale565(rowColor, 170) : rowBg;
    uint16_t textColor = (i == latestRank) ? lighten565(rowColor, 110) : TFT_WHITE;
    canvas.fillRoundRect(64, y - 6, 352, 24, 6, fillColor);
    canvas.setTextColor(textColor, fillColor);
    canvas.drawString(rankLabel, 88, y, 2);
    canvas.drawNumber(leaderboard[i], 174, y - 4, 2);
    if (i == latestRank) {
      canvas.fillTriangle(382, y + 8, 398, y, 398, y + 16, rowColor);
      canvas.fillCircle(366, y + 8, 7, lighten565(rowColor, 90));
    }
  }

  canvas.setTextColor(TFT_WHITE, bg);
  if (latestRank == 0) {
    canvas.drawString("NEW HIGH SCORE!", 144, 284, 2);
  } else if (latestRank > 0) {
    canvas.drawString("NEW TOP 5 SCORE!", 148, 284, 2);
  } else {
    canvas.drawString("TRY TO BREAK INTO TOP 5", 108, 284, 2);
  }
  canvas.drawString("BLUE RETRY", 74, 304, 2);
  canvas.drawString("GREEN TITLE", 286, 304, 2);
}

void renderPlaying() {
  if (frameBufferReady) {
    drawScene(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawScene(tft);
  }
}

void renderGameOver() {
  if (frameBufferReady) {
    drawGameOver(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawGameOver(tft);
  }
  lastRenderedState = gameState;
}

void renderTitle() {
  if (frameBufferReady) {
    drawTitle(frameBuffer);
    frameBuffer.pushSprite(0, 0);
    if (titleRightBufferReady) {
      drawTitleRight(titleRightBuffer);
      titleRightBuffer.pushSprite(320, 0);
    }
  } else {
    drawTitle(tft);
  }
  lastRenderedState = gameState;
}

void handleState() {
  unsigned long now = millis();

  if (now - lastMouthToggleMs >= 120) {
    lastMouthToggleMs = now;
    mouthOpen = !mouthOpen;
  }

  if (greenPressed && (gameState == PLAYING || paused)) {
    paused = !paused;
  }

  switch (gameState) {
    case TITLE:
      if (bluePressed) {
        startLevel(false);
      }
      renderTitle();
      break;

    case READY:
      renderPlaying();
      if (now >= stateTimerMs) {
        gameState = PLAYING;
        bannerText[0] = '\0';
      }
      break;

    case PLAYING:
      if (!paused && now - lastGameUpdateMs >= GAME_UPDATE_INTERVAL_MS) {
        lastGameUpdateMs = now;
        updatePowerMode();
        updatePlayer();
        if (gameState == PLAYING) {
          updateGhosts();
        }
      }
      renderPlaying();
      break;

    case LEVEL_CLEAR:
      renderPlaying();
      if (now >= stateTimerMs) {
        startLevel(true);
      }
      break;

    case GAME_OVER:
      renderGameOver();
      if (bluePressed) {
        startLevel(false);
      } else if (greenPressed) {
        gameState = TITLE;
      }
      break;
  }

  lastRenderedState = gameState;
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  pinMode(green_btn_pin, INPUT_PULLUP);
  pinMode(blue_btn_pin, INPUT_PULLUP);
  pinMode(green_led_pin, OUTPUT);
  pinMode(blue_led_pin, OUTPUT);
  digitalWrite(green_led_pin, HIGH);
  digitalWrite(blue_led_pin, HIGH);

  ledcSetup(forward_ch, motor_frequency, motor_resolution);
  ledcSetup(reverse_ch, motor_frequency, motor_resolution);
  ledcAttachPin(forward_pin, forward_ch);
  ledcAttachPin(reverse_pin, reverse_ch);
  stopMotor();

  analogReadResolution(12);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setSwapBytes(false);

  initFrameBuffer();
  initAudio();

  loadMaze();
  resetActors();
  snprintf(bannerText, sizeof(bannerText), "READY!");
  gameState = TITLE;
}

void loop() {
  readInputs();
  digitalWrite(blue_led_pin, blueDown ? LOW : HIGH);
  digitalWrite(green_led_pin, greenDown ? LOW : HIGH);
  handleState();
  updateVibration();
  delay(1);
}
