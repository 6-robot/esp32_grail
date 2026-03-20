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
#define AUDIO_CHUNK_SAMPLES 128

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBuffer = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int HUD_H = 24;
static const float FLOOR_Y = SCREEN_H + 30.0f;

static const float BODY_W = 22.0f;
static const float BODY_H = 18.0f;
static const float BALLOON_R = 8.0f;
static const float GRAVITY = 0.22f;
static const float FLAP_IMPULSE = -3.7f;
static const float MAX_VX = 2.6f;
static const float MAX_VY_UP = -4.2f;
static const float MAX_VY_DOWN = 3.8f;
static const float FALL_MAX_VY = 7.2f;
static const uint16_t GAME_UPDATE_INTERVAL_MS = 12;
static const uint16_t FACE_COLOR = 0xFD20;

struct Voice;
struct Fighter;
struct Platform;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

enum GameState {
  TITLE,
  PLAYING,
  GAME_OVER
};

struct Voice {
  bool active;
  float freq;
  float phase;
  int remainSamples;
  float volume;
  uint8_t wave;  // 0 square, 1 sine, 2 noise
};

struct Note {
  uint16_t freq;
  uint16_t ms;
};

struct Fighter {
  float x;
  float y;
  float vx;
  float vy;
  uint8_t balloons;
  bool alive;
  bool falling;
  bool stunned;
  bool isPlayer;
  bool facingRight;
  bool flapPose;
  unsigned long stunUntil;
  unsigned long invulnUntil;
  unsigned long flapUntil;
  unsigned long aiMoveUntil;
  unsigned long aiNextFlapMs;
  unsigned long aiNextTurnMs;
  unsigned long aiGlideUntil;
  bool perched;
  int perchIndex;
};

struct Platform {
  int x;
  int y;
  int w;
};

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;

Fighter player;
Fighter enemy;
static const int MAX_PLATFORMS = 4;
Platform platforms[MAX_PLATFORMS];
int platformCount = 0;

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
bool upDown = false;
bool upPressed = false;
int latestX = 2048;
int latestY = 2048;

bool vibrating = false;
bool vibForward = true;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;

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

Voice sfxVoices[4];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
TaskHandle_t audioTaskHandle = nullptr;
bool frameBufferReady = false;
uint8_t nextFlapChannel = 0;
float bgmPhase = 0.0f;
int bgmIndex = 0;
int bgmRemainSamples = 0;

const Note bgmLoop[] = {
  {523, 120}, {659, 120}, {784, 120}, {659, 120},
  {587, 120}, {659, 120}, {698, 120}, {659, 120},
  {523, 120}, {659, 120}, {784, 120}, {659, 120},
  {494, 120}, {587, 120}, {659, 180}, {0, 60},

  {587, 120}, {698, 120}, {784, 120}, {698, 120},
  {659, 120}, {698, 120}, {880, 120}, {698, 120},
  {587, 120}, {698, 120}, {784, 120}, {698, 120},
  {523, 120}, {659, 120}, {698, 180}, {0, 60},

  {392, 160}, {440, 160}, {523, 160}, {440, 160},
  {349, 160}, {392, 160}, {466, 160}, {392, 160},
  {330, 160}, {392, 160}, {523, 160}, {392, 160},
  {294, 160}, {349, 160}, {440, 220}, {0, 100},

  {784, 100}, {698, 100}, {659, 100}, {587, 100},
  {523, 100}, {587, 100}, {659, 100}, {698, 100},
  {784, 100}, {698, 100}, {659, 100}, {587, 100},
  {523, 120}, {494, 120}, {440, 180}, {0, 100}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

unsigned long lastGameUpdateMs = 0;
char endMessage[32] = "";

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

  unsigned long now = millis();
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

void startVoice(float freq, int ms, float volume, uint8_t wave, int preferredChannel = -1) {
  int durationSamples = (SAMPLE_RATE * ms) / 1000;
  taskENTER_CRITICAL(&audioMux);
  if (preferredChannel >= 0 && preferredChannel < 4) {
    assignVoice(sfxVoices[preferredChannel], freq, durationSamples, volume, wave);
    taskEXIT_CRITICAL(&audioMux);
    return;
  }

  for (int i = 1; i < 3; i++) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }

  assignVoice(sfxVoices[1], freq, durationSamples, volume, wave);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxFlap() {
  int channel = nextFlapChannel;
  nextFlapChannel = (nextFlapChannel == 0) ? 3 : 0;
  startVoice(880.0f, 70, 0.16f, 0, channel);
}

void playSfxPop() {
  startVoice(220.0f, 100, 0.14f, 2);
  startVoice(520.0f, 50, 0.08f, 0);
}

void playSfxFall() {
  startVoice(180.0f, 120, 0.12f, 0);
  startVoice(110.0f, 180, 0.14f, 0);
}

void playSfxWin() {
  startVoice(523.0f, 100, 0.10f, 0);
  startVoice(659.0f, 100, 0.10f, 0);
  startVoice(784.0f, 180, 0.12f, 0);
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

  float s = (bgmPhase < 0.5f) ? 0.006f : -0.006f;
  bgmPhase += (float)freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  return s;
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
    for (int v = 0; v < 4; v++) {
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

    if (mix > 0.6f) mix = 0.6f;
    if (mix < -0.6f) mix = -0.6f;
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

void readButtons() {
  bool g = (digitalRead(green_btn_pin) == LOW);
  bool b = (digitalRead(blue_btn_pin) == LOW);
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);
  bool upNow = (latestY < 1200);
  greenPressed = (g && !greenDown);
  bluePressed = (b && !blueDown);
  upPressed = (upNow && !upDown);
  greenDown = g;
  blueDown = b;
  upDown = upNow;
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

void resetFighter(Fighter &f, float x, float y, bool isPlayer) {
  f.x = x;
  f.y = y;
  f.vx = 0.0f;
  f.vy = 0.0f;
  f.balloons = 2;
  f.alive = true;
  f.falling = false;
  f.stunned = false;
  f.isPlayer = isPlayer;
  f.facingRight = isPlayer;
  f.flapPose = false;
  f.stunUntil = 0;
  f.invulnUntil = millis() + 600;
  f.flapUntil = 0;
  f.aiMoveUntil = 0;
  f.aiNextFlapMs = millis() + random(260, 520);
  f.aiNextTurnMs = millis() + random(220, 520);
  f.aiGlideUntil = millis() + random(240, 520);
  f.perched = false;
  f.perchIndex = -1;
}

void generatePlatforms() {
  static const Platform candidates[] = {
    {54, 104, 64}, {164, 92, 70}, {302, 112, 66},
    {88, 170, 68}, {224, 176, 64}, {340, 166, 62},
    {138, 236, 66}, {284, 228, 70}
  };
  static const int candidateCount = sizeof(candidates) / sizeof(candidates[0]);
  bool used[candidateCount];
  for (int i = 0; i < candidateCount; i++) {
    used[i] = false;
  }

  platformCount = 3;
  for (int i = 0; i < platformCount; i++) {
    int pick = random(0, candidateCount);
    while (used[pick]) {
      pick = (pick + 1) % candidateCount;
    }
    used[pick] = true;
    platforms[i] = candidates[pick];
  }
}

void beginGame() {
  generatePlatforms();
  resetFighter(player, 120.0f, 176.0f, true);
  resetFighter(enemy, 360.0f, 128.0f, false);
  lastGameUpdateMs = millis();
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  snprintf(endMessage, sizeof(endMessage), "FIGHT");
  gameState = PLAYING;
  stopMotor();
}

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool overlap1D(float a0, float a1, float b0, float b1) {
  return !(a1 < b0 || a0 > b1);
}

bool stompHit(const Fighter &attacker, const Fighter &defender) {
  if (!attacker.alive || !defender.alive || attacker.falling || defender.falling) {
    return false;
  }
  if (millis() < defender.invulnUntil) {
    return false;
  }

  float feetTop = attacker.y + BODY_H * 0.35f;
  float feetBottom = attacker.y + BODY_H * 0.72f;
  float feetLeft = attacker.x - BODY_W * 0.24f;
  float feetRight = attacker.x + BODY_W * 0.24f;

  float balloonsTop = defender.y - BODY_H - 26.0f;
  float balloonsBottom = defender.y - BODY_H - 2.0f;
  float balloonsLeft = defender.x - 18.0f;
  float balloonsRight = defender.x + 18.0f;

  if (!overlap1D(feetLeft, feetRight, balloonsLeft, balloonsRight)) {
    return false;
  }
  if (!overlap1D(feetTop, feetBottom, balloonsTop, balloonsBottom)) {
    return false;
  }
  return attacker.vy > defender.vy + 0.45f;
}

void applyHit(Fighter &victim, Fighter &attacker) {
  if (victim.balloons > 0) {
    victim.balloons--;
  }
  victim.perched = false;
  victim.perchIndex = -1;
  victim.stunned = true;
  victim.stunUntil = millis() + 700;
  victim.invulnUntil = millis() + 950;
  victim.vx = attacker.facingRight ? 1.7f : -1.7f;
  victim.vy = -1.8f;
  attacker.vy = -2.0f;
  attacker.flapPose = true;
  attacker.flapUntil = millis() + 120;
  playSfxPop();
  startVibration();

  if (victim.balloons == 0) {
    victim.falling = true;
    victim.stunned = false;
    victim.vx *= 0.5f;
    if (victim.vy < 0.5f) {
      victim.vy = 0.5f;
    }
    playSfxFall();
  }
}

void flap(Fighter &f, float impulseScale, bool playSound) {
  if (!f.alive || f.falling) {
    return;
  }
  f.perched = false;
  f.perchIndex = -1;
  f.vy += FLAP_IMPULSE * impulseScale;
  if (f.vy < MAX_VY_UP) {
    f.vy = MAX_VY_UP;
  }
  f.flapPose = true;
  f.flapUntil = millis() + 120;
  if (playSound) {
    playSfxFlap();
  }
}

void updatePlayerDrift() {
  int xv = latestX;
  float ax = 0.0f;
  if (xv < 1500) {
    ax = -((1500.0f - xv) / 1500.0f) * 0.34f;
  } else if (xv > 2600) {
    ax = ((xv - 2600.0f) / (4095.0f - 2600.0f)) * 0.34f;
  }

  player.vx += ax;
  player.vx = clampf(player.vx, -MAX_VX, MAX_VX);
  if (fabsf(player.vx) > 0.04f) {
    player.facingRight = (player.vx > 0.0f);
  }
}

void handlePlayerImmediateInput() {
  if (gameState != PLAYING) {
    return;
  }
  updatePlayerDrift();
  if ((greenPressed || upPressed) && !player.stunned) {
    if (player.perched) {
      player.vx *= 0.4f;
    }
    flap(player, 1.0f, true);
  }
}

void updateEnemyAI(unsigned long now) {
  if (!enemy.alive || enemy.falling) {
    return;
  }

  if (enemy.stunned) {
    enemy.vx += (random(0, 2) == 0) ? -0.08f : 0.08f;
  } else {
    float targetX = player.x;
    if (player.y < enemy.y - 14.0f) {
      targetX += (player.x < enemy.x) ? -10.0f : 10.0f;
    }

    if (now >= enemy.aiNextTurnMs) {
      enemy.aiNextTurnMs = now + random(180, 420);
      if (fabsf(targetX - enemy.x) < 26.0f) {
        enemy.aiMoveUntil = now + random(90, 220);
        enemy.vx += (random(0, 2) == 0) ? -0.45f : 0.45f;
      }
    }

    if (targetX < enemy.x - 6.0f) {
      enemy.vx -= 0.16f;
      enemy.facingRight = false;
    } else if (targetX > enemy.x + 6.0f) {
      enemy.vx += 0.16f;
      enemy.facingRight = true;
    }
  }

  float heightDiff = enemy.y - player.y;
  int flapIntervalMs = 700;
  if (heightDiff > 90.0f) {
    flapIntervalMs = 240;
  } else if (heightDiff > 48.0f) {
    flapIntervalMs = 340;
  } else if (heightDiff > 12.0f) {
    flapIntervalMs = 470;
  } else if (heightDiff > -24.0f) {
    flapIntervalMs = 620;
  } else if (heightDiff > -64.0f) {
    flapIntervalMs = 820;
  } else {
    flapIntervalMs = 1040;
  }

  if (enemy.perched) {
    enemy.vx *= 0.88f;
    if (fabsf(player.x - enemy.x) > 10.0f) {
      enemy.vx += (player.x > enemy.x) ? 0.14f : -0.14f;
    }
    if (heightDiff > 36.0f || now >= enemy.aiNextFlapMs) {
      flap(enemy, 0.95f, false);
      enemy.aiNextFlapMs = now + flapIntervalMs + random(120, 260);
    }
    return;
  }

  enemy.vx = clampf(enemy.vx, -2.2f, 2.2f);

  if (now >= enemy.aiGlideUntil) {
    enemy.aiGlideUntil = now + random(480, 980);
    enemy.aiNextFlapMs = now + random(320, 760);
  }

  bool inGlideWindow = (now + 220 < enemy.aiGlideUntil);
  bool shouldFlap = false;
  if (enemy.y > player.y + 34.0f) {
    shouldFlap = true;
  }
  if (enemy.y > SCREEN_H - 56.0f) {
    shouldFlap = true;
  }
  if (enemy.vy > 2.35f) {
    shouldFlap = true;
  }
  if (!inGlideWindow && now >= enemy.aiNextFlapMs) {
    shouldFlap = true;
  }
  if (enemy.y < player.y - 40.0f) {
    shouldFlap = false;
  }
  if (enemy.y < HUD_H + 84.0f && enemy.vy < 1.2f) {
    shouldFlap = false;
  }

  if (shouldFlap && !enemy.stunned) {
    flap(enemy, 0.92f, false);
    enemy.aiNextFlapMs = now + flapIntervalMs + random(120, 260);
    if (random(0, 100) < 55) {
      enemy.aiGlideUntil = now + random(420, 900);
    }
  }
}

bool fighterOnPlatformX(const Fighter &f, const Platform &p) {
  float left = f.x - BODY_W * 0.38f;
  float right = f.x + BODY_W * 0.38f;
  return !(right < p.x || left > p.x + p.w);
}

bool landOnPlatform(Fighter &f, float previousY) {
  if (f.falling) {
    return false;
  }
  for (int i = 0; i < platformCount; i++) {
    const Platform &p = platforms[i];
    float top = (float)p.y;
    if (previousY <= top && f.y >= top && fighterOnPlatformX(f, p)) {
      f.y = top;
      f.vx = 0.0f;
      f.vy = 0.0f;
      f.perched = true;
      f.perchIndex = i;
      f.flapPose = false;
      f.stunned = false;
      return true;
    }
  }
  return false;
}

void updateFighterPhysics(Fighter &f) {
  unsigned long now = millis();
  if (!f.alive) {
    return;
  }

  if (f.stunned && now >= f.stunUntil) {
    f.stunned = false;
  }
  if (f.flapPose && now >= f.flapUntil) {
    f.flapPose = false;
  }

  if (f.perched && f.perchIndex >= 0 && f.perchIndex < platformCount) {
    if (!fighterOnPlatformX(f, platforms[f.perchIndex])) {
      f.perched = false;
      f.perchIndex = -1;
    } else {
      f.x += f.vx;
      f.y = platforms[f.perchIndex].y;
      f.vy = 0.0f;
      f.vx *= 0.84f;
      if (fabsf(f.vx) < 0.05f) {
        f.vx = 0.0f;
      }
      return;
    }
  }

  float previousY = f.y;

  if (f.falling) {
    f.vy += GRAVITY * 1.45f;
    f.vx *= 0.99f;
  } else if (f.stunned) {
    f.vy += GRAVITY * 1.05f;
    f.vx *= 0.992f;
  } else {
    f.vy += GRAVITY;
    f.vx *= 0.95f;
  }

  if (f.falling) {
    f.vy = clampf(f.vy, MAX_VY_UP, FALL_MAX_VY);
  } else {
    f.vy = clampf(f.vy, MAX_VY_UP, MAX_VY_DOWN);
  }
  f.x += f.vx;
  f.y += f.vy;

  if (f.x < 14.0f) {
    f.x = 14.0f;
    f.vx *= -0.45f;
    f.facingRight = true;
  } else if (f.x > SCREEN_W - 14.0f) {
    f.x = SCREEN_W - 14.0f;
    f.vx *= -0.45f;
    f.facingRight = false;
  }

  if (!f.falling && f.y < HUD_H + 44.0f) {
    f.y = HUD_H + 44.0f;
    if (f.vy < 0.0f) {
      f.vy = 0.0f;
    }
  }

  if (f.vy >= 0.0f) {
    landOnPlatform(f, previousY);
  }

  if (f.y > FLOOR_Y) {
    f.alive = false;
  }
}

void resolveBodyBump(Fighter &a, Fighter &b) {
  if (!a.alive || !b.alive || a.falling || b.falling) {
    return;
  }

  float ax0 = a.x - BODY_W * 0.5f;
  float ax1 = a.x + BODY_W * 0.5f;
  float ay0 = a.y - BODY_H;
  float ay1 = a.y;
  float bx0 = b.x - BODY_W * 0.5f;
  float bx1 = b.x + BODY_W * 0.5f;
  float by0 = b.y - BODY_H;
  float by1 = b.y;

  if (!overlap1D(ax0, ax1, bx0, bx1) || !overlap1D(ay0, ay1, by0, by1)) {
    return;
  }

  float push = (a.x < b.x) ? -0.7f : 0.7f;
  a.vx += push;
  b.vx -= push;
  a.vy -= 0.2f;
  b.vy -= 0.2f;
}

void updateCombat() {
  if (stompHit(player, enemy)) {
    applyHit(enemy, player);
  } else if (stompHit(enemy, player)) {
    applyHit(player, enemy);
  } else {
    resolveBodyBump(player, enemy);
  }

  if (!player.alive && gameState == PLAYING) {
    snprintf(endMessage, sizeof(endMessage), "YOU LOSE");
    gameState = GAME_OVER;
  } else if (!enemy.alive && gameState == PLAYING) {
    snprintf(endMessage, sizeof(endMessage), "YOU WIN");
    playSfxWin();
    gameState = GAME_OVER;
  }
}

template <typename CanvasT>
void drawSkyBase(CanvasT &canvas) {
  canvas.fillRect(0, 0, SCREEN_W, HUD_H, TFT_NAVY);
  canvas.fillRect(0, HUD_H, SCREEN_W, SCREEN_H - HUD_H, TFT_CYAN);

  for (int i = 0; i < 5; i++) {
    int cx = 54 + i * 84;
    int cy = 46 + (i % 2) * 26;
    canvas.fillCircle(cx, cy, 14, TFT_WHITE);
    canvas.fillCircle(cx + 12, cy + 2, 12, TFT_WHITE);
    canvas.fillCircle(cx - 12, cy + 2, 10, TFT_WHITE);
  }

  canvas.fillRect(0, SCREEN_H - 26, SCREEN_W, 26, TFT_DARKGREEN);
}

void drawSky(TFT_eSPI &canvas) {
  canvas.fillScreen(TFT_CYAN);
  drawSkyBase(canvas);
}

void drawSky(TFT_eSprite &canvas) {
  canvas.fillSprite(TFT_CYAN);
  drawSkyBase(canvas);
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.setTextColor(TFT_WHITE, TFT_NAVY);
  canvas.setTextSize(1);
  canvas.setCursor(6, 8);
  canvas.printf("P:%d  E:%d  FLAP:UP or GREEN", player.balloons, enemy.balloons);
}

static const uint8_t PLAYER_PIXEL_W = 12;
static const uint8_t PLAYER_PIXEL_H = 12;
static const uint8_t ENEMY_PIXEL_W = 12;
static const uint8_t ENEMY_PIXEL_H = 12;
static const int FIGHTER_PIXEL_SCALE = 2;

static const uint8_t playerSpritePixels0[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0,0,0,3,3,3,3,3,0,0,0,0,
  0,0,3,3,4,4,4,3,3,0,0,0,
  0,3,3,4,4,4,4,4,3,3,0,0,
  0,3,4,4,1,1,1,1,1,3,0,0,
  0,3,4,1,1,1,1,1,2,1,3,0,
  0,3,1,1,1,1,1,1,1,1,3,0,
  0,0,3,1,5,1,1,5,1,3,0,0,
  0,3,3,3,6,6,6,6,3,3,3,0,
  3,3,0,6,6,6,6,6,6,0,3,3,
  0,0,0,6,6,6,6,6,6,0,0,0,
  0,0,0,7,0,0,0,0,7,0,0,0,
  0,0,7,0,0,0,0,0,0,7,0,0
};

static const uint8_t playerSpritePixels1[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0,0,0,3,3,3,3,3,0,0,0,0,
  0,0,3,3,4,4,4,3,3,0,0,0,
  0,3,3,4,4,4,4,4,3,3,0,0,
  0,3,4,4,1,1,1,1,1,3,0,0,
  0,3,4,1,1,1,1,1,2,1,3,0,
  0,3,1,1,1,1,1,1,1,1,3,0,
  0,0,3,1,5,1,1,5,1,3,0,0,
  3,3,3,3,6,6,6,6,3,3,3,3,
  0,3,0,6,6,6,6,6,6,0,3,0,
  0,0,6,6,6,6,6,6,6,6,0,0,
  0,0,0,7,0,0,0,0,7,0,0,0,
  0,0,7,0,0,0,0,0,0,7,0,0
};

static const uint8_t enemySpritePixels0[ENEMY_PIXEL_W * ENEMY_PIXEL_H] = {
  0,0,0,0,0,4,4,4,0,0,0,0,
  0,0,0,4,4,4,4,4,4,0,0,0,
  0,0,4,4,1,1,1,1,1,4,0,0,
  0,4,4,1,1,1,1,1,1,1,8,0,
  0,4,1,1,2,1,1,2,1,8,8,8,
  0,4,1,1,1,1,3,3,1,1,8,0,
  0,0,4,1,1,1,1,1,1,4,0,0,
  0,4,4,4,5,5,5,5,4,4,4,0,
  4,4,0,5,5,5,5,5,5,0,4,4,
  0,0,0,5,0,5,5,0,5,0,0,0,
  0,0,6,0,0,0,0,0,0,6,0,0,
  0,6,0,0,0,0,0,0,0,0,6,0
};

static const uint8_t enemySpritePixels1[ENEMY_PIXEL_W * ENEMY_PIXEL_H] = {
  0,0,0,0,0,4,4,4,0,0,0,0,
  0,0,0,4,4,4,4,4,4,0,0,0,
  0,0,4,4,1,1,1,1,1,4,0,0,
  0,4,4,1,1,1,1,1,1,1,8,0,
  0,4,1,1,2,1,1,2,1,8,8,8,
  0,4,1,1,1,1,3,3,1,1,8,0,
  4,4,4,1,1,1,1,1,1,4,4,4,
  0,0,4,4,5,5,5,5,4,4,0,0,
  0,4,0,5,5,5,5,5,5,0,4,0,
  4,0,5,5,0,5,5,0,5,5,0,4,
  0,0,6,0,0,0,0,0,0,6,0,0,
  0,6,0,0,0,0,0,0,0,0,6,0
};

static const uint8_t playerSitPixels[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0,0,0,0,3,3,3,3,0,0,0,0,
  0,0,0,3,4,4,4,3,3,0,0,0,
  0,0,3,4,4,4,4,4,3,0,0,0,
  0,3,3,4,1,1,1,1,3,3,0,0,
  0,3,4,1,1,1,1,2,1,3,0,0,
  0,3,1,1,1,1,1,1,1,3,0,0,
  0,0,3,1,5,1,1,5,3,0,0,0,
  0,3,3,6,6,6,6,6,6,3,0,0,
  0,0,6,6,6,6,6,6,6,6,0,0,
  0,0,0,0,6,6,6,6,0,0,0,0,
  0,0,0,7,7,0,0,7,7,0,0,0,
  0,0,7,7,0,0,0,0,7,7,0,0
};

static const uint8_t enemySitPixels[ENEMY_PIXEL_W * ENEMY_PIXEL_H] = {
  0,0,0,0,0,4,4,4,0,0,0,0,
  0,0,0,0,4,4,4,4,4,0,0,0,
  0,0,0,4,1,1,1,1,1,8,0,0,
  0,0,4,1,1,1,1,1,8,8,8,0,
  0,4,1,1,2,1,1,2,1,8,0,0,
  0,4,1,1,1,3,3,1,1,4,0,0,
  0,0,4,1,1,1,1,1,4,0,0,0,
  0,4,4,5,5,5,5,5,5,4,0,0,
  0,0,5,5,5,5,5,5,5,5,0,0,
  0,0,0,0,5,5,5,5,0,0,0,0,
  0,0,0,6,0,0,0,0,6,0,0,0,
  0,0,6,0,0,0,0,0,0,6,0,0
};

static const uint8_t playerRunPixels0[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0,0,0,0,3,3,3,3,0,0,0,0,
  0,0,0,3,4,4,4,3,3,0,0,0,
  0,0,3,4,4,4,4,4,3,0,0,0,
  0,3,3,4,1,1,1,1,3,3,0,0,
  0,3,4,1,1,1,1,2,1,3,0,0,
  0,3,1,1,1,1,1,1,1,3,0,0,
  0,0,3,1,5,1,1,5,3,0,0,0,
  0,3,3,6,6,6,6,6,6,3,0,0,
  0,0,6,6,6,6,6,6,6,6,0,0,
  0,0,0,6,6,6,6,0,0,0,7,0,
  0,0,0,0,7,7,0,0,0,7,0,0,
  0,0,7,7,0,0,0,0,7,0,0,0
};

static const uint8_t playerRunPixels1[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0,0,0,0,3,3,3,3,0,0,0,0,
  0,0,0,3,4,4,4,3,3,0,0,0,
  0,0,3,4,4,4,4,4,3,0,0,0,
  0,3,3,4,1,1,1,1,3,3,0,0,
  0,3,4,1,1,1,1,2,1,3,0,0,
  0,3,1,1,1,1,1,1,1,3,0,0,
  0,0,3,1,5,1,1,5,3,0,0,0,
  0,3,3,6,6,6,6,6,6,3,0,0,
  0,0,6,6,6,6,6,6,6,6,0,0,
  0,7,0,0,6,6,6,6,0,0,0,0,
  0,0,7,0,0,0,7,7,0,0,0,0,
  0,0,0,7,7,0,0,0,0,7,7,0
};

static const uint8_t enemyRunPixels0[ENEMY_PIXEL_W * ENEMY_PIXEL_H] = {
  0,0,0,0,0,4,4,4,0,0,0,0,
  0,0,0,0,4,4,4,4,4,0,0,0,
  0,0,0,4,1,1,1,1,1,8,0,0,
  0,0,4,1,1,1,1,1,8,8,8,0,
  0,4,1,1,2,1,1,2,1,8,0,0,
  0,4,1,1,1,3,3,1,1,4,0,0,
  0,0,4,1,1,1,1,1,4,0,0,0,
  0,4,4,5,5,5,5,5,5,4,0,0,
  0,0,5,5,5,5,5,5,5,5,0,0,
  0,0,0,5,5,5,5,0,0,0,6,0,
  0,0,0,0,6,0,0,0,0,6,0,0,
  0,0,6,6,0,0,0,0,6,0,0,0
};

static const uint8_t enemyRunPixels1[ENEMY_PIXEL_W * ENEMY_PIXEL_H] = {
  0,0,0,0,0,4,4,4,0,0,0,0,
  0,0,0,0,4,4,4,4,4,0,0,0,
  0,0,0,4,1,1,1,1,1,8,0,0,
  0,0,4,1,1,1,1,1,8,8,8,0,
  0,4,1,1,2,1,1,2,1,8,0,0,
  0,4,1,1,1,3,3,1,1,4,0,0,
  0,0,4,1,1,1,1,1,4,0,0,0,
  0,4,4,5,5,5,5,5,5,4,0,0,
  0,0,5,5,5,5,5,5,5,5,0,0,
  0,6,0,0,5,5,5,5,0,0,0,0,
  0,0,6,0,0,0,6,0,0,0,0,0,
  0,0,0,6,6,0,0,0,0,6,6,0
};

static const uint8_t playerFallPixels0[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0,0,0,0,3,3,3,3,0,0,0,0,
  0,0,0,3,4,4,4,3,3,0,0,0,
  0,0,3,4,4,4,4,4,3,0,0,0,
  0,3,4,4,1,1,1,1,1,3,0,0,
  0,3,4,1,1,1,1,1,2,1,3,0,
  3,3,1,1,1,1,1,1,1,1,3,3,
  0,3,3,1,5,1,1,5,1,3,3,0,
  0,0,3,3,6,6,6,6,3,3,0,0,
  0,3,0,6,6,6,6,6,6,0,3,0,
  3,0,0,0,6,6,6,6,0,0,0,3,
  0,0,7,0,0,0,0,0,0,7,0,0,
  0,7,0,0,0,0,0,0,0,0,7,0
};

static const uint8_t playerFallPixels1[PLAYER_PIXEL_W * PLAYER_PIXEL_H] = {
  0,0,0,0,3,3,3,3,0,0,0,0,
  0,0,0,3,4,4,4,3,3,0,0,0,
  0,0,3,4,4,4,4,4,3,0,0,0,
  0,3,4,4,1,1,1,1,1,3,0,0,
  0,3,4,1,1,1,1,1,2,1,3,0,
  3,3,1,1,1,1,1,1,1,1,3,3,
  0,0,3,1,5,1,1,5,1,3,0,0,
  0,3,3,3,6,6,6,6,3,3,3,0,
  0,0,0,6,6,6,6,6,6,0,0,0,
  0,3,0,0,6,6,6,6,0,0,3,0,
  0,0,0,7,0,0,0,0,7,0,0,0,
  0,0,7,0,0,0,0,0,0,7,0,0
};

static const uint8_t enemyFallPixels0[ENEMY_PIXEL_W * ENEMY_PIXEL_H] = {
  0,0,0,0,0,4,4,4,0,0,0,0,
  0,0,0,0,4,4,4,4,4,0,0,0,
  0,0,0,4,1,1,1,1,1,8,0,0,
  0,0,4,1,1,1,1,1,8,8,8,0,
  0,4,1,1,2,1,1,2,1,8,0,0,
  4,4,1,1,1,3,3,1,1,4,4,0,
  0,0,4,1,1,1,1,1,4,0,0,0,
  0,4,4,5,5,5,5,5,5,4,0,0,
  4,0,0,5,5,5,5,5,0,0,0,4,
  0,0,6,0,0,5,5,0,0,0,6,0,
  0,6,0,0,0,0,0,0,0,6,0,0,
  6,0,0,0,0,0,0,0,0,0,0,6
};

static const uint8_t enemyFallPixels1[ENEMY_PIXEL_W * ENEMY_PIXEL_H] = {
  0,0,0,0,0,4,4,4,0,0,0,0,
  0,0,0,0,4,4,4,4,4,0,0,0,
  0,0,0,4,1,1,1,1,1,8,0,0,
  0,0,4,1,1,1,1,1,8,8,8,0,
  0,4,1,1,2,1,1,2,1,8,0,0,
  0,4,1,1,1,3,3,1,1,4,4,0,
  0,0,4,1,1,1,1,1,4,0,0,0,
  0,4,4,5,5,5,5,5,5,4,0,0,
  0,0,0,5,5,5,5,5,5,0,0,0,
  0,0,6,0,0,5,5,0,0,0,6,0,
  0,0,0,6,0,0,0,0,0,6,0,0,
  0,6,6,0,0,0,0,0,0,0,6,6
};

template <typename CanvasT>
void drawPlatform(CanvasT &canvas, const Platform &p) {
  canvas.fillRect(p.x, p.y, p.w, 5, TFT_GREEN);
  canvas.fillRect(p.x, p.y + 5, p.w, 4, rgb565(94, 62, 32));
  canvas.fillRect(p.x + 2, p.y + 9, p.w - 4, 8, rgb565(129, 94, 58));
  canvas.drawFastHLine(p.x + 4, p.y + 11, p.w - 8, rgb565(155, 120, 74));
  for (int x = p.x + 6; x < p.x + p.w - 6; x += 14) {
    canvas.drawPixel(x, p.y + 3, TFT_YELLOW);
  }
}

template <typename CanvasT>
void drawPixelSprite(CanvasT &canvas, const uint8_t *pixels, uint8_t width, uint8_t height,
                     int xCenter, int yBottom, const uint16_t *palette, int scale, bool facingRight) {
  int left = xCenter - (width * scale) / 2;
  int top = yBottom - height * scale;
  for (int py = 0; py < height; py++) {
    for (int px = 0; px < width; px++) {
      int sx = facingRight ? px : (width - 1 - px);
      uint8_t idx = pixels[py * width + sx];
      if (idx == 0) {
        continue;
      }
      canvas.fillRect(left + px * scale, top + py * scale, scale, scale, palette[idx]);
    }
  }
}

template <typename CanvasT>
void drawPixelBalloon(CanvasT &canvas, int cx, int cy, uint16_t color) {
  uint16_t gloss = TFT_WHITE;
  canvas.fillRect(cx - 5, cy - 7, 10, 12, color);
  canvas.fillRect(cx - 8, cy - 5, 16, 8, color);
  canvas.fillRect(cx - 6, cy + 3, 12, 4, color);
  canvas.fillRect(cx - 3, cy - 4, 4, 3, gloss);
  canvas.fillRect(cx - 1, cy - 1, 2, 2, gloss);
  canvas.fillRect(cx - 2, cy + 6, 4, 2, color);
  canvas.drawLine(cx, cy + 8, cx, cy + 14, TFT_DARKGREY);
}

void drawTitleBalloon(int cx, int cy, uint16_t color, uint16_t shadowColor) {
  tft.fillEllipse(cx + 12, cy + 16, 34, 42, shadowColor);

  tft.fillEllipse(cx, cy, 34, 44, color);
  tft.fillEllipse(cx - 6, cy - 8, 22, 26, TFT_WHITE);
  tft.fillEllipse(cx - 10, cy - 10, 10, 12, rgb565(255, 250, 210));
  tft.fillEllipse(cx + 8, cy + 10, 24, 30, shadowColor);
  tft.fillEllipse(cx + 2, cy + 4, 26, 32, color);

  tft.drawEllipse(cx, cy, 34, 44, TFT_WHITE);
  tft.drawEllipse(cx, cy, 33, 43, rgb565(255, 240, 220));
  tft.fillTriangle(cx - 5, cy + 40, cx + 5, cy + 40, cx, cy + 52, color);
  tft.drawLine(cx, cy + 52, cx - 2, cy + 82, TFT_WHITE);
}

template <typename CanvasT>
void drawFighter(CanvasT &canvas, const Fighter &f, bool isPlayer) {
  if (!f.alive) {
    return;
  }

  int x = (int)f.x;
  int y = (int)f.y;
  if (f.falling) {
    x += (((millis() / 80) & 1) == 0) ? -3 : 3;
  }
  int wingY = y - 11;
  int wingLift = f.flapPose ? 10 : 4;
  uint16_t balloonColor = isPlayer ? TFT_RED : TFT_ORANGE;
  int rearBalloonX = f.facingRight ? x - 6 : x + 6;
  int frontBalloonX = f.facingRight ? x + 7 : x - 7;
  int rearBalloonY = y - 39;
  int frontBalloonY = y - 35;

  if (f.balloons > 0) {
    drawPixelBalloon(canvas, rearBalloonX, rearBalloonY, balloonColor);
  }
  if (f.balloons > 1) {
    drawPixelBalloon(canvas, frontBalloonX, frontBalloonY, balloonColor);
  }

  uint16_t playerPalette[9] = {
    TFT_BLACK, FACE_COLOR, TFT_BLACK, rgb565(70, 130, 255), rgb565(255, 230, 90), TFT_BLACK,
    rgb565(230, 60, 60), TFT_BLACK, TFT_BLACK
  };
  uint16_t enemyPalette[9] = {
    TFT_BLACK, rgb565(255, 236, 190), TFT_BLACK, TFT_WHITE, rgb565(210, 70, 70), rgb565(110, 215, 130),
    TFT_BLACK, TFT_BLACK, rgb565(255, 180, 40)
  };
  const uint16_t *palette = isPlayer ? playerPalette : enemyPalette;
  bool flapFrame = f.flapPose || ((millis() / 180) % 2 == 1 && fabsf(f.vy) > 0.45f);
  const uint8_t *sprite = nullptr;
  if (f.falling) {
    bool fallFrame = (((millis() / 90) & 1) == 0);
    sprite = isPlayer
        ? (fallFrame ? playerFallPixels0 : playerFallPixels1)
        : (fallFrame ? enemyFallPixels0 : enemyFallPixels1);
  } else if (f.perched) {
    bool runFrame = fabsf(f.vx) > 0.18f;
    if (isPlayer) {
      sprite = runFrame
          ? ((((millis() / 120) & 1) == 0) ? playerRunPixels0 : playerRunPixels1)
          : playerSitPixels;
    } else {
      sprite = runFrame
          ? ((((millis() / 120) & 1) == 0) ? enemyRunPixels0 : enemyRunPixels1)
          : enemySitPixels;
    }
  } else {
    sprite = isPlayer
        ? (flapFrame ? playerSpritePixels1 : playerSpritePixels0)
        : (flapFrame ? enemySpritePixels1 : enemySpritePixels0);
  }

  uint16_t wingColor = isPlayer ? rgb565(180, 230, 255) : rgb565(240, 240, 240);
  if (!f.perched && !f.falling) {
    if (f.facingRight) {
      canvas.drawLine(x - 6, wingY, x - 18, wingY - wingLift, wingColor);
      canvas.drawLine(x + 1, wingY, x + 10, wingY - (flapFrame ? wingLift / 3 : wingLift / 2), wingColor);
    } else {
      canvas.drawLine(x + 6, wingY, x + 18, wingY - wingLift, wingColor);
      canvas.drawLine(x - 1, wingY, x - 10, wingY - (flapFrame ? wingLift / 3 : wingLift / 2), wingColor);
    }
  }

  drawPixelSprite(canvas, sprite,
                  isPlayer ? PLAYER_PIXEL_W : ENEMY_PIXEL_W,
                  isPlayer ? PLAYER_PIXEL_H : ENEMY_PIXEL_H,
                  x, y + 6, palette, FIGHTER_PIXEL_SCALE, f.facingRight);

  if (f.falling) {
    canvas.drawLine(x - 14, y - 6, x - 18, y + 6, TFT_WHITE);
    canvas.drawLine(x + 14, y - 2, x + 18, y + 10, TFT_WHITE);
  }

  if (f.stunned && !f.falling && (millis() / 100) % 2 == 0) {
    canvas.drawCircle(x, y - 26, 4, TFT_RED);
  }
}

void drawTitle() {
  uint16_t bg = rgb565(16, 26, 84);
  uint16_t panel = rgb565(24, 38, 108);
  tft.fillScreen(bg);
  tft.fillRoundRect(58, 30, 250, 56, 12, panel);
  drawTitleBalloon(380, 92, TFT_RED, rgb565(176, 38, 38));
  drawTitleBalloon(424, 130, TFT_YELLOW, rgb565(196, 154, 20));
  tft.fillRoundRect(76, 102, 276, 96, 10, panel);

  tft.setTextColor(TFT_WHITE, panel);
  tft.setTextSize(3);
  tft.setCursor(74, 54);
  tft.print("BALLOON FIGHT");

  tft.setTextSize(2);
  tft.setCursor(90, 120);
  tft.print("Joystick Left/Right: Drift");
  tft.setCursor(90, 148);
  tft.print("Joystick Up or Green: Flap");
  tft.setCursor(90, 176);
  tft.print("Blue: Start / Restart");

  tft.fillRoundRect(92, 226, 238, 18, 8, panel);
  tft.setTextSize(1);
  tft.setCursor(118, 232);
  tft.print("Prototype: flight physics + stomp collision");
}

void drawGameOver() {
  tft.fillScreen(TFT_MAROON);
  tft.setTextColor(TFT_WHITE, TFT_MAROON);
  tft.setTextSize(3);
  tft.setCursor(132, 84);
  tft.print(endMessage);

  tft.setTextSize(2);
  tft.setCursor(108, 154);
  tft.print("Blue Button to Restart");
  tft.setCursor(120, 186);
  tft.print("Green or Up to Flap");
}

template <typename CanvasT>
void drawScene(CanvasT &canvas) {
  drawSky(canvas);
  for (int i = 0; i < platformCount; i++) {
    drawPlatform(canvas, platforms[i]);
  }
  drawHud(canvas);
  drawFighter(canvas, player, true);
  drawFighter(canvas, enemy, false);
}

void renderPlaying() {
  if (frameBufferReady) {
    drawScene(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    tft.fillScreen(TFT_BLACK);
    drawScene(tft);
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

  if (gameState == TITLE) {
    if (bluePressed) {
      beginGame();
    }
  } else if (gameState == PLAYING) {
    handlePlayerImmediateInput();
    unsigned long now = millis();
    if (now - lastGameUpdateMs >= GAME_UPDATE_INTERVAL_MS) {
      lastGameUpdateMs = now;
      updateEnemyAI(now);
      updateFighterPhysics(player);
      updateFighterPhysics(enemy);
      updateCombat();
    }
  } else if (gameState == GAME_OVER) {
    if (bluePressed) {
      beginGame();
    }
  }

  updateVibration();

  if (gameState == TITLE) {
    if (lastRenderedState != TITLE) {
      drawTitle();
      lastRenderedState = TITLE;
    }
  } else if (gameState == PLAYING) {
    lastRenderedState = PLAYING;
    renderPlaying();
  } else if (gameState == GAME_OVER) {
    if (lastRenderedState != GAME_OVER) {
      drawGameOver();
      lastRenderedState = GAME_OVER;
    }
  }

  delay(1);
}
