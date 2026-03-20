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
static const int FLOOR_Y = 264;
static const int HUD_H = 28;
static const int MAX_ENEMIES = 4;
static const int LEADERBOARD_SIZE = 5;
static const uint16_t GAME_UPDATE_INTERVAL_MS = 16;
static const int PLAYER_MAX_HP = 18;

enum GameState {
  TITLE,
  PLAYING,
  GAME_OVER
};

enum AttackType {
  ATTACK_NONE,
  ATTACK_PUNCH,
  ATTACK_KICK,
  ATTACK_FLY
};

enum EnemyState {
  ENEMY_ADVANCE,
  ENEMY_ATTACK,
  ENEMY_HURT,
  ENEMY_FALL
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

struct Fighter {
  float x;
  float y;
  float vx;
  float vy;
  int hp;
  bool grounded;
  bool facingRight;
  bool attacking;
  bool hurt;
  bool alive;
  uint8_t walkFrame;
  AttackType attackType;
  unsigned long attackUntil;
  unsigned long attackCooldownUntil;
  unsigned long hurtUntil;
};

struct Enemy {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  int hp;
  bool grounded;
  bool facingRight;
  bool attacking;
  bool hurt;
  EnemyState state;
  bool hasStaff;
  unsigned long attackUntil;
  unsigned long attackCooldownUntil;
  unsigned long hurtUntil;
};

struct Rect {
  float x;
  float y;
  float w;
  float h;
};

GameState gameState = TITLE;
GameState lastRenderedState = GAME_OVER;

Fighter player;
Enemy enemies[MAX_ENEMIES];

bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
bool upDown = false;
bool upPressed = false;
int latestX = 2048;
int latestY = 2048;

bool vibrating = false;
bool vibForward = false;
unsigned long vibEndMs = 0;
unsigned long vibLastToggleMs = 0;

Voice sfxVoices[4];
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t audioTaskHandle = nullptr;
int16_t audioBuffer[AUDIO_CHUNK_SAMPLES];
bool frameBufferReady = false;

unsigned long lastGameUpdateMs = 0;
unsigned long nextSpawnMs = 0;
unsigned long stageClearMs = 0;

int combo = 0;
int enemiesDefeated = 0;
int stageNumber = 1;
bool stageCleared = false;
char endMessage[32] = "GAME OVER";
int leaderboard[LEADERBOARD_SIZE] = {12, 9, 7, 5, 3};
int latestRank = -1;

float bgmPhase = 0.0f;
int bgmIndex = 0;
int bgmRemainSamples = 0;

const Note bgmLoop[] = {
  {330, 140}, {392, 140}, {440, 140}, {392, 140},
  {330, 120}, {392, 120}, {523, 180}, {0, 60},
  {330, 140}, {392, 140}, {440, 140}, {392, 140},
  {330, 120}, {294, 120}, {262, 200}, {0, 60},
  {392, 120}, {440, 120}, {523, 140}, {587, 140},
  {523, 120}, {440, 120}, {392, 200}, {0, 60},
  {330, 120}, {392, 120}, {440, 120}, {523, 140},
  {440, 120}, {392, 120}, {330, 220}, {0, 60}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

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

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool overlapRect(const Rect &a, const Rect &b) {
  return !(a.x + a.w < b.x || b.x + b.w < a.x || a.y + a.h < b.y || b.y + b.h < a.y);
}

void stopMotor() {
  ledcWrite(forward_ch, 0);
  ledcWrite(reverse_ch, 0);
}

void startVibration(uint16_t durationMs = 180) {
  vibrating = true;
  vibForward = false;
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

  if (vibLastToggleMs == 0 || now - vibLastToggleMs >= 20) {
    vibLastToggleMs = now;
    vibForward = !vibForward;
    const int duty = 190;
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

  for (int i = 0; i < 4; i++) {
    if (!sfxVoices[i].active) {
      assignVoice(sfxVoices[i], freq, durationSamples, volume, wave);
      taskEXIT_CRITICAL(&audioMux);
      return;
    }
  }

  assignVoice(sfxVoices[0], freq, durationSamples, volume, wave);
  taskEXIT_CRITICAL(&audioMux);
}

void playSfxPunch() {
  startVoice(180.0f, 50, 0.10f, 2);
  startVoice(760.0f, 40, 0.08f, 0, 1);
}

void playSfxJump() {
  startVoice(520.0f, 70, 0.08f, 0);
  startVoice(740.0f, 60, 0.06f, 0, 1);
}

void playSfxHit() {
  startVoice(120.0f, 80, 0.12f, 2);
  startVoice(220.0f, 100, 0.08f, 0, 1);
}

void playSfxKO() {
  startVoice(220.0f, 90, 0.10f, 0);
  startVoice(180.0f, 120, 0.10f, 0, 1);
  startVoice(120.0f, 180, 0.10f, 0, 2);
}

void playSfxClear() {
  startVoice(523.0f, 80, 0.08f, 0);
  startVoice(659.0f, 80, 0.08f, 0, 1);
  startVoice(784.0f, 120, 0.10f, 0, 2);
}

float bgmSample() {
  if (gameState != PLAYING || stageCleared) {
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

  float duty = (bgmPhase < 0.5f) ? 0.006f : -0.006f;
  bgmPhase += (float)freq / SAMPLE_RATE;
  if (bgmPhase >= 1.0f) {
    bgmPhase -= 1.0f;
  }
  return duty;
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

    mix = clampf(mix, -0.65f, 0.65f);
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
  bool upNow = (latestY < 1200);
  greenPressed = (g && !greenDown);
  bluePressed = (b && !blueDown);
  upPressed = (upNow && !upDown);
  greenDown = g;
  blueDown = b;
  upDown = upNow;
}

void resetPlayer() {
  player.x = 84.0f;
  player.y = FLOOR_Y;
  player.vx = 0.0f;
  player.vy = 0.0f;
  player.hp = PLAYER_MAX_HP;
  player.grounded = true;
  player.facingRight = true;
  player.attacking = false;
  player.hurt = false;
  player.alive = true;
  player.walkFrame = 0;
  player.attackType = ATTACK_NONE;
  player.attackUntil = 0;
  player.attackCooldownUntil = 0;
  player.hurtUntil = 0;
}

void clearEnemies() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    enemies[i].active = false;
  }
}

void spawnEnemy() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy &enemy = enemies[i];
    if (enemy.active) {
      continue;
    }

    enemy.active = true;
    bool spawnRight = (random(0, 100) < 70);
    enemy.x = spawnRight ? SCREEN_W + random(0, 26) : -20.0f - random(0, 20);
    enemy.y = FLOOR_Y;
    enemy.vx = 0.0f;
    enemy.vy = 0.0f;
    enemy.hp = 1;
    enemy.grounded = true;
    enemy.facingRight = !spawnRight;
    enemy.attacking = false;
    enemy.hurt = false;
    enemy.state = ENEMY_ADVANCE;
    enemy.hasStaff = (stageNumber == 1 && random(0, 100) < 45);
    enemy.attackUntil = 0;
    enemy.attackCooldownUntil = millis() + (enemy.hasStaff ? random(760, 1120) : random(500, 900));
    enemy.hurtUntil = 0;
    return;
  }
}

void updateLeaderboard() {
  latestRank = LEADERBOARD_SIZE;
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    if (enemiesDefeated > leaderboard[i]) {
      latestRank = i;
      break;
    }
  }

  if (latestRank >= LEADERBOARD_SIZE) {
    if (enemiesDefeated <= leaderboard[LEADERBOARD_SIZE - 1]) {
      latestRank = -1;
      return;
    }
    latestRank = LEADERBOARD_SIZE - 1;
  }

  for (int i = LEADERBOARD_SIZE - 1; i > latestRank; i--) {
    leaderboard[i] = leaderboard[i - 1];
  }
  leaderboard[latestRank] = enemiesDefeated;
}

void beginGame() {
  resetPlayer();
  clearEnemies();
  combo = 0;
  enemiesDefeated = 0;
  stageNumber = 1;
  stageCleared = false;
  nextSpawnMs = millis() + 700;
  stageClearMs = 0;
  latestRank = -1;
  lastGameUpdateMs = millis();
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = (SAMPLE_RATE * bgmLoop[0].ms) / 1000;
  snprintf(endMessage, sizeof(endMessage), "GAME OVER");
  gameState = PLAYING;
  stopMotor();
}

Rect playerBodyRect() {
  Rect r;
  r.x = player.x - 12.0f;
  r.y = player.y - 46.0f;
  r.w = 24.0f;
  r.h = 46.0f;
  return r;
}

Rect enemyBodyRect(const Enemy &e) {
  Rect r;
  r.x = e.x - 12.0f;
  r.y = e.y - 44.0f;
  r.w = 24.0f;
  r.h = 44.0f;
  return r;
}

Rect playerAttackRect() {
  Rect r = {0, 0, 0, 0};
  if (!player.attacking) {
    return r;
  }

  float dir = player.facingRight ? 1.0f : -1.0f;
  if (player.attackType == ATTACK_PUNCH) {
    r.x = player.x + dir * 8.0f - (dir < 0 ? 24.0f : 0.0f);
    r.y = player.y - 38.0f;
    r.w = 24.0f;
    r.h = 12.0f;
  } else if (player.attackType == ATTACK_KICK) {
    r.x = player.x + dir * 8.0f - (dir < 0 ? 30.0f : 0.0f);
    r.y = player.y - 18.0f;
    r.w = 30.0f;
    r.h = 12.0f;
  } else if (player.attackType == ATTACK_FLY) {
    r.x = player.x + dir * 4.0f - (dir < 0 ? 34.0f : 0.0f);
    r.y = player.y - 34.0f;
    r.w = 34.0f;
    r.h = 18.0f;
  }
  return r;
}

Rect enemyAttackRect(const Enemy &e) {
  Rect r = {0, 0, 0, 0};
  if (!e.attacking) {
    return r;
  }
  float dir = e.facingRight ? 1.0f : -1.0f;
  r.x = e.x + dir * 8.0f - (dir < 0 ? 24.0f : 0.0f);
  r.y = e.y - 34.0f;
  r.w = 24.0f;
  r.h = 12.0f;
  return r;
}

void damagePlayer(int amount, float knockback) {
  if (player.hurt || !player.alive) {
    return;
  }
  player.hp -= amount;
  player.hurt = true;
  player.hurtUntil = millis() + 360;
  player.attacking = false;
  player.attackType = ATTACK_NONE;
  player.vx = knockback;
  player.vy = -2.8f;
  player.grounded = false;
  combo = 0;
  playSfxHit();
  startVibration(260);

  if (player.hp <= 0) {
    player.hp = 0;
    player.alive = false;
    updateLeaderboard();
    snprintf(endMessage, sizeof(endMessage), "RANKING");
    gameState = GAME_OVER;
    playSfxKO();
  }
}

void damageEnemy(Enemy &e, int amount, float knockback) {
  if (!e.active || e.hurt) {
    return;
  }

  e.hp -= amount;
  e.hurt = true;
  e.hurtUntil = millis() + 220;
  e.attacking = false;
  e.state = ENEMY_HURT;
  e.vx = knockback;
  e.vy = -1.8f;
  e.grounded = false;
  combo++;
  playSfxHit();

  if (e.hp <= 0) {
    e.hp = 0;
    e.state = ENEMY_FALL;
    e.vx = knockback * 1.2f;
    e.vy = -3.2f;
    enemiesDefeated++;
    playSfxKO();
  }
}

void startPlayerAttack() {
  if (!player.alive) {
    return;
  }
  unsigned long now = millis();
  if (now < player.attackCooldownUntil || player.hurt) {
    return;
  }

  player.attacking = true;
  if (!player.grounded) {
    player.attackType = ATTACK_FLY;
    player.attackUntil = now + 170;
    player.attackCooldownUntil = now + 240;
    player.vx += player.facingRight ? 1.8f : -1.8f;
  } else if (latestY > 3000) {
    player.attackType = ATTACK_KICK;
    player.attackUntil = now + 150;
    player.attackCooldownUntil = now + 210;
  } else {
    player.attackType = ATTACK_PUNCH;
    player.attackUntil = now + 110;
    player.attackCooldownUntil = now + 180;
  }
  playSfxPunch();
}

void updatePlayerInput() {
  if (!player.alive || player.hurt) {
    return;
  }

  float move = 0.0f;
  if (latestX < 1500) {
    move = -1.0f;
  } else if (latestX > 2600) {
    move = 1.0f;
  }

  if (move != 0.0f) {
    player.vx = move * 2.3f;
    player.facingRight = (move > 0.0f);
    if (player.grounded && millis() % 180 < 90) {
      player.walkFrame = 1;
    } else {
      player.walkFrame = 0;
    }
  } else {
    player.vx *= player.grounded ? 0.65f : 0.96f;
    if (fabsf(player.vx) < 0.05f) {
      player.vx = 0.0f;
    }
    player.walkFrame = 0;
  }

  if (bluePressed && player.grounded) {
    player.vy = -5.8f;
    player.grounded = false;
    playSfxJump();
  }

  if (greenPressed) {
    startPlayerAttack();
  }
}

void updatePlayerPhysics() {
  unsigned long now = millis();
  if (player.hurt && now >= player.hurtUntil) {
    player.hurt = false;
  }
  if (player.attacking && now >= player.attackUntil) {
    player.attacking = false;
    player.attackType = ATTACK_NONE;
  }

  if (!player.grounded) {
    player.vy += 0.30f;
    if (player.vy > 6.2f) {
      player.vy = 6.2f;
    }
  }

  player.x += player.vx;
  player.y += player.vy;
  player.x = clampf(player.x, 26.0f, SCREEN_W - 26.0f);

  if (player.y >= FLOOR_Y) {
    player.y = FLOOR_Y;
    player.vy = 0.0f;
    player.grounded = true;
  }
}

void updateEnemyAI(Enemy &e) {
  unsigned long now = millis();
  if (!e.active) {
    return;
  }

  if (e.hurt && now >= e.hurtUntil) {
    e.hurt = false;
    if (e.state != ENEMY_FALL) {
      e.state = ENEMY_ADVANCE;
    }
  }

  if (e.attacking && now >= e.attackUntil) {
    e.attacking = false;
    if (e.state != ENEMY_FALL) {
      e.state = ENEMY_ADVANCE;
    }
  }

  if (e.state == ENEMY_FALL) {
    e.vy += 0.34f;
    e.x += e.vx;
    e.y += e.vy;
    e.vx *= 0.98f;
    if (e.y >= FLOOR_Y) {
      e.active = false;
    }
    return;
  }

  if (!e.grounded) {
    e.vy += 0.28f;
    if (e.vy > 5.2f) {
      e.vy = 5.2f;
    }
  }

  float dx = player.x - e.x;
  e.facingRight = (dx > 0.0f);

  if (e.hurt) {
    e.x += e.vx;
    e.y += e.vy;
    e.vx *= 0.92f;
  } else if (fabsf(dx) > 32.0f) {
    e.state = ENEMY_ADVANCE;
    float speed = e.hasStaff ? 0.72f : 1.00f;
    e.vx = (dx > 0.0f) ? speed + stageNumber * 0.04f : -speed - stageNumber * 0.04f;
    e.x += e.vx;
  } else {
    e.vx = 0.0f;
    if (now >= e.attackCooldownUntil) {
      e.attacking = true;
      e.state = ENEMY_ATTACK;
      e.attackUntil = now + (e.hasStaff ? 220 : 150);
      e.attackCooldownUntil = now + (e.hasStaff ? random(900, 1350) : random(620, 900));
      playSfxPunch();
    }
  }

  e.y += e.vy;
  if (e.y >= FLOOR_Y) {
    e.y = FLOOR_Y;
    e.vy = 0.0f;
    e.grounded = true;
  }

  if (e.x < -30.0f || e.x > SCREEN_W + 30.0f) {
    e.active = false;
  }
}

void handleCombat() {
  Rect pBody = playerBodyRect();
  Rect pAtk = playerAttackRect();
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy &enemy = enemies[i];
    if (!enemy.active) {
      continue;
    }

    Rect eBody = enemyBodyRect(enemy);
    Rect eAtk = enemyAttackRect(enemy);

    if (player.attacking && !enemy.hurt && enemy.state != ENEMY_FALL && overlapRect(pAtk, eBody)) {
      float knockback = player.facingRight ? 2.4f : -2.4f;
      damageEnemy(enemy, 1, knockback);
    }

    if (enemy.attacking && !player.hurt && overlapRect(eAtk, pBody)) {
      float knockback = enemy.facingRight ? 2.0f : -2.0f;
      damagePlayer(enemy.hasStaff ? 2 : 1, knockback);
    }

    if (!player.hurt && !enemy.hurt && enemy.state != ENEMY_FALL && overlapRect(pBody, eBody) && fabsf(player.vx) > 1.6f) {
      float knockback = player.facingRight ? 2.0f : -2.0f;
      damageEnemy(enemy, 1, knockback);
    }
  }
}

void updateSpawning() {
  unsigned long now = millis();
  int activeCount = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      activeCount++;
    }
  }

  if (activeCount < 2 + min(stageNumber / 2, 1) && now >= nextSpawnMs) {
    spawnEnemy();
    nextSpawnMs = now + max(380, 980 - stageNumber * 80) + random(0, 140);
  }

  int targetStage = 1 + enemiesDefeated / 10;
  if (targetStage > stageNumber && !stageCleared) {
    stageCleared = true;
    stageClearMs = now;
    stageNumber = targetStage;
    playSfxClear();
  }
}

void advanceStageIfNeeded() {
  if (!stageCleared) {
    return;
  }

  if (millis() - stageClearMs < 1400) {
    return;
  }

  stageCleared = false;
  player.vx = 0.0f;
  player.vy = 0.0f;
  player.grounded = true;
  player.attacking = false;
  player.attackType = ATTACK_NONE;
  combo = 0;
  nextSpawnMs = millis() + 500;
}

template <typename CanvasT>
void drawPine(CanvasT &canvas, int x, int baseY, int h, uint16_t leafColor) {
  canvas.fillRect(x - 3, baseY - h / 5, 6, h / 5, rgb565(112, 72, 36));
  canvas.fillTriangle(x, baseY - h, x - 16, baseY - h / 2, x + 16, baseY - h / 2, leafColor);
  canvas.fillTriangle(x, baseY - h + 18, x - 20, baseY - h / 3, x + 20, baseY - h / 3, leafColor);
  canvas.fillTriangle(x, baseY - h + 38, x - 24, baseY, x + 24, baseY, leafColor);
}

template <typename CanvasT>
void drawBackground(CanvasT &canvas) {
  for (int y = 0; y < SCREEN_H; y += 4) {
    float t = (float)y / SCREEN_H;
    uint8_t r = (uint8_t)((1.0f - t) * 6 + t * 26);
    uint8_t g = (uint8_t)((1.0f - t) * 16 + t * 44);
    uint8_t b = (uint8_t)((1.0f - t) * 54 + t * 86);
    canvas.fillRect(0, y, SCREEN_W, 4, rgb565(r, g, b));
  }

  canvas.fillCircle(382, 58, 22, rgb565(248, 220, 96));
  canvas.fillCircle(374, 56, 22, rgb565(18, 26, 58));

  static const int stars[][2] = {
    {34, 28}, {72, 48}, {118, 24}, {160, 42}, {202, 32}, {244, 56},
    {286, 18}, {314, 40}, {348, 26}, {420, 34}, {448, 54}, {96, 66}
  };
  for (unsigned int i = 0; i < sizeof(stars) / sizeof(stars[0]); i++) {
    int sx = stars[i][0];
    int sy = stars[i][1];
    canvas.drawPixel(sx, sy, rgb565(250, 222, 102));
    canvas.drawPixel(sx + 1, sy, rgb565(250, 222, 102));
    canvas.drawPixel(sx, sy + 1, rgb565(250, 222, 102));
  }

  canvas.fillRect(0, FLOOR_Y - 46, SCREEN_W, 18, rgb565(18, 44, 22));
  canvas.fillRect(0, FLOOR_Y - 26, SCREEN_W, 20, rgb565(26, 62, 30));

  drawPine(canvas, 36, FLOOR_Y - 6, 68, rgb565(26, 88, 36));
  drawPine(canvas, 94, FLOOR_Y - 8, 82, rgb565(22, 76, 32));
  drawPine(canvas, 152, FLOOR_Y - 4, 64, rgb565(30, 98, 42));
  drawPine(canvas, 220, FLOOR_Y - 8, 86, rgb565(24, 82, 34));
  drawPine(canvas, 286, FLOOR_Y - 6, 72, rgb565(28, 92, 40));
  drawPine(canvas, 350, FLOOR_Y - 8, 84, rgb565(20, 72, 30));
  drawPine(canvas, 424, FLOOR_Y - 4, 66, rgb565(26, 88, 36));

  canvas.fillRect(0, FLOOR_Y, SCREEN_W, SCREEN_H - FLOOR_Y, rgb565(68, 40, 22));
  canvas.fillRect(0, FLOOR_Y + 12, SCREEN_W, 12, rgb565(84, 52, 28));
  for (int x = 0; x < SCREEN_W; x += 28) {
    canvas.fillRect(x + 4, FLOOR_Y + 18 + ((x / 28) % 2), 10, 2, rgb565(98, 62, 34));
    canvas.drawPixel(x + 18, FLOOR_Y + 10, rgb565(110, 72, 42));
  }
}

template <typename CanvasT>
void drawHealthBar(CanvasT &canvas, int x, int y, int hp, int maxHp, uint16_t color) {
  canvas.drawRect(x, y, 74, 10, TFT_WHITE);
  int fillW = 0;
  if (maxHp > 0 && hp > 0) {
    fillW = (hp * 72) / maxHp;
    if (fillW < 1) {
      fillW = 1;
    }
  }
  canvas.fillRect(x + 1, y + 1, fillW, 8, color);
}

template <typename CanvasT>
void drawPlayer(CanvasT &canvas) {
  int x = (int)player.x;
  int y = (int)player.y;
  int dir = player.facingRight ? 1 : -1;
  bool running = player.grounded && fabsf(player.vx) > 0.6f;
  uint16_t skin = rgb565(246, 210, 150);
  uint16_t cloth = player.hurt ? rgb565(216, 82, 82) : rgb565(250, 244, 108);
  uint16_t trim = rgb565(236, 92, 56);
  uint16_t hair = rgb565(24, 18, 18);
  uint16_t pants = rgb565(236, 236, 214);
  uint16_t shoe = rgb565(22, 18, 18);

  int headX = x - 7;
  int headY = y - 46;
  canvas.fillRect(headX, headY, 14, 14, skin);
  canvas.fillRect(headX - 1, headY, 16, 3, hair);
  if (dir > 0) {
    canvas.fillRect(headX - 2, headY + 2, 4, 5, hair);
  } else {
    canvas.fillRect(headX + 12, headY + 2, 4, 5, hair);
  }
  canvas.drawPixel(x + dir * 3, headY + 6, TFT_BLACK);
  canvas.drawFastHLine(x - 2, headY + 11, 5, TFT_BLACK);

  canvas.fillRect(x - 8, y - 31, 16, 17, cloth);
  canvas.fillRect(x - 10, y - 31, 20, 3, trim);
  canvas.fillRect(x - 2, y - 14, 4, 4, trim);
  canvas.fillRect(x - 8, y - 14, 16, 8, pants);

  int backArmX = (dir > 0) ? x - 14 : x + 8;
  canvas.fillRect(backArmX, y - 28 + (player.walkFrame ? 2 : 0), 6, 11, cloth);
  canvas.fillRect(backArmX + (dir > 0 ? 0 : 2), y - 18 + (player.walkFrame ? 2 : 0), 4, 5, skin);

  if (player.attacking && player.attackType == ATTACK_PUNCH) {
    int armX = (dir > 0) ? x + 8 : x - 22;
    canvas.fillRect(armX, y - 28, 14, 5, cloth);
    canvas.fillRect(armX + (dir > 0 ? 10 : 0), y - 27, 6, 7, skin);
    canvas.fillRect(armX + (dir > 0 ? 14 : -2), y - 26, 2, 4, TFT_WHITE);
  } else {
    int frontArmX = (dir > 0) ? x + 8 : x - 14;
    int frontArmY = y - 27 + (running ? (player.walkFrame ? -2 : 3) : 2);
    canvas.fillRect(frontArmX, frontArmY, 6, 12, cloth);
    canvas.fillRect(frontArmX + (dir > 0 ? 2 : 0), frontArmY + 9, 4, 5, skin);
  }

  if (player.attacking && player.attackType == ATTACK_KICK) {
    int kickX = (dir > 0) ? x + 2 : x - 18;
    canvas.fillRect(x - 6, y - 6, 4, 15, pants);
    canvas.fillRect(kickX, y - 10, 20, 5, pants);
    canvas.fillRect(kickX + (dir > 0 ? 16 : 0), y - 10, 4, 5, shoe);
  } else if (player.attacking && player.attackType == ATTACK_FLY) {
    int flyX = (dir > 0) ? x + 4 : x - 24;
    canvas.fillRect(x - 10, y - 24, 20, 10, cloth);
    canvas.fillRect(x - 8, y - 14, 16, 8, pants);
    canvas.fillRect(flyX, y - 12, 22, 5, pants);
    canvas.fillRect(flyX + (dir > 0 ? 18 : 0), y - 12, 4, 5, shoe);
    canvas.fillRect((dir > 0) ? x - 18 : x + 10, y - 18, 10, 4, cloth);
  } else {
    if (running) {
      if (player.walkFrame) {
        canvas.fillRect(x - 8, y - 6, 4, 12, pants);
        canvas.fillRect(x + 4, y - 2, 4, 16, pants);
        canvas.fillRect(x - 10, y + 6, 8, 3, shoe);
        canvas.fillRect(x + 2, y + 12, 8, 3, shoe);
      } else {
        canvas.fillRect(x - 8, y - 2, 4, 16, pants);
        canvas.fillRect(x + 4, y - 6, 4, 12, pants);
        canvas.fillRect(x - 10, y + 12, 8, 3, shoe);
        canvas.fillRect(x + 2, y + 6, 8, 3, shoe);
      }
      if (dir > 0) {
        canvas.fillRect(x - 14, y - 18, 6, 4, trim);
      } else {
        canvas.fillRect(x + 8, y - 18, 6, 4, trim);
      }
    } else {
      canvas.fillRect(x - 6, y - 6, 4, 16, pants);
      canvas.fillRect(x + 2, y - 6, 4, 16, pants);
      if (dir > 0) {
        canvas.fillRect(x - 8, y + 10, 6, 3, shoe);
        canvas.fillRect(x + 1, y + 10, 6, 3, shoe);
      } else {
        canvas.fillRect(x - 7, y + 10, 6, 3, shoe);
        canvas.fillRect(x + 2, y + 10, 6, 3, shoe);
      }
    }
  }
}

template <typename CanvasT>
void drawEnemy(CanvasT &canvas, const Enemy &e) {
  int x = (int)e.x;
  int y = (int)e.y;
  int dir = e.facingRight ? 1 : -1;
  bool running = e.state == ENEMY_ADVANCE && fabsf(e.vx) > 0.2f;
  bool runFrame = ((millis() / 110) & 1) != 0;
  uint16_t skin = rgb565(228, 186, 132);
  uint16_t cloth = e.hasStaff ? rgb565(126, 86, 176) : rgb565(52, 146, 198);
  if (e.hurt) {
    cloth = rgb565(220, 84, 104);
  }
  uint16_t hair = rgb565(18, 16, 16);
  uint16_t trim = e.hasStaff ? rgb565(250, 204, 72) : rgb565(190, 228, 244);
  uint16_t pants = e.hasStaff ? rgb565(56, 36, 92) : rgb565(28, 44, 92);
  uint16_t shoe = rgb565(16, 14, 14);
  uint16_t evilEye = rgb565(236, 72, 72);

  canvas.fillRect(x - 8, y - 45, 16, 15, skin);
  canvas.fillRect(x - 10, y - 46, 20, 4, hair);
  canvas.fillRect(x - 12, y - 38, 3, 10, hair);
  canvas.fillRect(x + 9, y - 38, 3, 10, hair);
  canvas.fillRect(x - 6, y - 28, 12, 3, hair);
  canvas.fillRect(x - 10, y - 26, 4, 2, hair);
  canvas.fillRect(x + 6, y - 26, 4, 2, hair);
  if (dir > 0) {
    canvas.drawPixel(x + 3, y - 39, evilEye);
    canvas.drawFastHLine(x + 1, y - 41, 5, hair);
  } else {
    canvas.drawPixel(x - 3, y - 39, evilEye);
    canvas.drawFastHLine(x - 5, y - 41, 5, hair);
  }
  canvas.drawFastHLine(x - 4, y - 34, 9, TFT_BLACK);
  canvas.drawPixel(x - dir * 2, y - 33, hair);

  int bodyTilt = running ? (runFrame ? dir * 2 : -dir * 1) : 0;
  canvas.fillRect(x - 10 + bodyTilt, y - 30, 20, 18, cloth);
  canvas.fillRect(x - 12 + bodyTilt, y - 31, 24, 4, trim);
  canvas.fillRect(x - 12 + bodyTilt, y - 20, 4, 10, trim);
  canvas.fillRect(x + 8 + bodyTilt, y - 20, 4, 10, trim);
  canvas.fillRect(x - 10 + bodyTilt, y - 10, 20, 10, pants);

  int backArmX = (dir > 0) ? x - 18 : x + 12;
  int backArmY = y - 26 + (running ? (runFrame ? 5 : -3) : 1);
  canvas.fillRect(backArmX, backArmY, 7, 13, cloth);
  canvas.fillRect(backArmX + (dir > 0 ? 0 : 3), backArmY + 9, 4, 6, skin);

  if (e.attacking) {
    int armX = (dir > 0) ? x + 10 : x - 24;
    canvas.fillRect(armX, y - 26, 14, 6, cloth);
    canvas.fillRect(armX + (dir > 0 ? 10 : 0), y - 27, 6, 8, skin);
  } else {
    int frontArmX = (dir > 0) ? x + 11 : x - 18;
    int frontArmY = y - 25 + (running ? (runFrame ? -4 : 5) : 1);
    canvas.fillRect(frontArmX, frontArmY, 7, 12, cloth);
    canvas.fillRect(frontArmX + (dir > 0 ? 3 : 0), frontArmY + 8, 4, 6, skin);
  }

  if (e.hasStaff) {
    int staffX = x + dir * 14 - (dir < 0 ? 34 : 0);
    int staffY = y - 31;
    canvas.fillRect(staffX, staffY, 34, 4, rgb565(154, 112, 64));
    canvas.fillRect(staffX + (dir > 0 ? 27 : 3), staffY - 4, 4, 12, rgb565(196, 154, 92));
  }

  if (running && !e.attacking) {
    if (runFrame) {
      canvas.fillRect(x - 10, y - 12, 5, 12, pants);
      canvas.fillRect(x + 4, y - 6, 5, 19, pants);
      canvas.fillRect(x - 12, y + 0, 8, 4, shoe);
      canvas.fillRect(x + 3, y + 11, 9, 4, shoe);
    } else {
      canvas.fillRect(x - 10, y - 6, 5, 19, pants);
      canvas.fillRect(x + 4, y - 12, 5, 12, pants);
      canvas.fillRect(x - 12, y + 11, 9, 4, shoe);
      canvas.fillRect(x + 3, y + 0, 8, 4, shoe);
    }
    canvas.fillRect(x - 4 + bodyTilt, y - 14, 8, 3, trim);
  } else {
    canvas.fillRect(x - 7, y - 8, 5, 16, pants);
    canvas.fillRect(x + 2, y - 8, 5, 16, pants);
    canvas.fillRect(x - 8, y + 8, 7, 3, shoe);
    canvas.fillRect(x + 1, y + 8, 7, 3, shoe);
  }
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  canvas.fillRect(0, 0, SCREEN_W, HUD_H, rgb565(12, 14, 24));
  canvas.fillRect(SCREEN_W / 2 - 56, 2, 112, 20, rgb565(26, 30, 46));
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(TFT_WHITE, rgb565(12, 14, 24));

  char buf[48];
  snprintf(buf, sizeof(buf), "STAGE %d", stageNumber);
  canvas.setTextColor(TFT_WHITE, rgb565(26, 30, 46));
  canvas.drawString(buf, SCREEN_W / 2, 12, 2);

  canvas.setTextDatum(TR_DATUM);
  canvas.setTextColor(rgb565(250, 214, 84), rgb565(12, 14, 24));
  snprintf(buf, sizeof(buf), "%d", enemiesDefeated);
  canvas.drawString(buf, SCREEN_W - 34, 3, 4);
  canvas.setTextDatum(TL_DATUM);

  canvas.setTextColor(rgb565(98, 236, 116), rgb565(12, 14, 24));
  canvas.drawString("HP", 8, 2, 2);
  drawHealthBar(canvas, 8, 18, player.hp, PLAYER_MAX_HP, rgb565(238, 78, 78));
}

template <typename CanvasT>
void drawOverlay(CanvasT &canvas) {
  if (!stageCleared) {
    return;
  }

  canvas.fillRoundRect(142, 108, 196, 72, 12, rgb565(20, 28, 42));
  canvas.drawRoundRect(142, 108, 196, 72, 12, rgb565(248, 214, 90));
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(rgb565(248, 214, 90), rgb565(20, 28, 42));
  canvas.drawString("STAGE CLEAR", SCREEN_W / 2, 130, 4);
  canvas.setTextColor(TFT_WHITE, rgb565(20, 28, 42));
  canvas.drawString("Get ready for next wave", SCREEN_W / 2, 160, 2);
}

template <typename CanvasT>
void drawScene(CanvasT &canvas) {
  drawBackground(canvas);
  drawHud(canvas);
  drawPlayer(canvas);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      drawEnemy(canvas, enemies[i]);
    }
  }
  drawOverlay(canvas);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  drawBackground(canvas);
  uint16_t poster = rgb565(30, 24, 22);
  uint16_t border = rgb565(240, 196, 84);
  uint16_t redPanel = rgb565(138, 44, 34);
  uint16_t cream = rgb565(246, 230, 182);
  uint16_t ink = rgb565(26, 18, 16);
  uint16_t tag = rgb565(44, 52, 74);

  canvas.fillRoundRect(56, 18, SCREEN_W - 112, SCREEN_H - 36, 10, poster);
  canvas.drawRoundRect(56, 18, SCREEN_W - 112, SCREEN_H - 36, 10, border);
  canvas.drawRoundRect(60, 22, SCREEN_W - 120, SCREEN_H - 44, 10, rgb565(156, 116, 44));

  canvas.fillRect(74, 34, SCREEN_W - 148, 62, redPanel);
  canvas.fillRect(74, 100, SCREEN_W - 148, 10, border);
  canvas.fillRect(74, 114, SCREEN_W - 148, 102, rgb565(38, 34, 44));
  canvas.fillRect(74, 220, SCREEN_W - 148, 44, redPanel);

  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(cream, redPanel);
  canvas.drawString("KUNG", SCREEN_W / 2, 52, 7);
  canvas.drawString("FU", SCREEN_W / 2, 84, 7);

  canvas.fillCircle(156, 168, 18, rgb565(246, 190, 66));
  canvas.fillCircle(324, 160, 14, rgb565(246, 190, 66));

  int heroX = SCREEN_W / 2;
  int heroY = 212;
  uint16_t skin = rgb565(246, 210, 150);
  uint16_t shirt = rgb565(248, 236, 92);
  uint16_t trim = rgb565(228, 82, 48);
  uint16_t pants = rgb565(234, 232, 214);
  uint16_t shoe = rgb565(22, 18, 18);
  uint16_t hair = rgb565(20, 18, 18);

  canvas.fillRect(heroX - 10, heroY - 68, 20, 18, skin);
  canvas.fillRect(heroX - 11, heroY - 68, 22, 4, hair);
  canvas.fillRect(heroX - 13, heroY - 64, 5, 7, hair);
  canvas.drawPixel(heroX + 4, heroY - 60, TFT_BLACK);
  canvas.drawFastHLine(heroX - 2, heroY - 54, 6, TFT_BLACK);
  canvas.fillRect(heroX - 12, heroY - 48, 24, 24, shirt);
  canvas.fillRect(heroX - 14, heroY - 48, 28, 4, trim);
  canvas.fillRect(heroX - 3, heroY - 24, 6, 5, trim);
  canvas.fillRect(heroX - 18, heroY - 44, 8, 18, shirt);
  canvas.fillRect(heroX + 10, heroY - 42, 20, 6, shirt);
  canvas.fillRect(heroX + 28, heroY - 41, 8, 8, skin);
  canvas.fillRect(heroX - 10, heroY - 20, 20, 10, pants);
  canvas.fillRect(heroX - 10, heroY - 10, 6, 20, pants);
  canvas.fillRect(heroX + 4, heroY - 14, 24, 6, pants);
  canvas.fillRect(heroX + 26, heroY - 14, 6, 6, shoe);
  canvas.fillRect(heroX - 12, heroY + 10, 10, 4, shoe);

  canvas.setTextColor(border, rgb565(38, 34, 44));
  canvas.drawString("8-BIT STREET BRAWL", SCREEN_W / 2, 128, 2);
  canvas.setTextColor(TFT_WHITE, rgb565(38, 34, 44));
  canvas.drawString("Blue Start", SCREEN_W / 2, 236, 4);

  canvas.fillRoundRect(106, 272, SCREEN_W - 212, 22, 6, tag);
  canvas.setTextColor(cream, tag);
  canvas.drawString("Move  Jump  Punch  Low Kick", SCREEN_W / 2, 283, 2);
}

template <typename CanvasT>
void drawGameOver(CanvasT &canvas) {
  drawBackground(canvas);
  uint16_t bg = rgb565(16, 18, 30);
  uint16_t rowBg = rgb565(48, 54, 76);
  uint16_t title = rgb565(246, 212, 90);

  canvas.fillRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, bg);
  canvas.drawRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, rgb565(232, 84, 84));
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(title, bg);
  canvas.drawString("KO RANKING", SCREEN_W / 2, 52, 4);
  canvas.setTextColor(TFT_WHITE, bg);
  char buf[48];
  snprintf(buf, sizeof(buf), "Kills %d", enemiesDefeated);
  canvas.drawString(buf, SCREEN_W / 2, 82, 2);
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    int y = 112 + i * 28;
    uint16_t fillColor = (i == latestRank) ? rgb565(84, 66, 26) : rowBg;
    uint16_t textColor = (i == latestRank) ? rgb565(250, 224, 120) : TFT_WHITE;
    canvas.fillRoundRect(92, y - 8, 296, 24, 6, fillColor);
    canvas.setTextColor(textColor, fillColor);
    snprintf(buf, sizeof(buf), "%d. %d KO", i + 1, leaderboard[i]);
    canvas.drawString(buf, 164, y, 2);
    if (i == latestRank) {
      canvas.fillTriangle(344, y + 8, 360, y, 360, y + 16, rgb565(250, 210, 92));
      canvas.fillCircle(328, y + 8, 6, rgb565(250, 210, 92));
    }
  }
  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawString("Blue Retry", 142, 286, 2);
  canvas.drawString("Green Title", 332, 286, 2);
}

void renderPlaying() {
  if (frameBufferReady) {
    drawScene(frameBuffer);
    frameBuffer.pushSprite(0, 0);
  } else {
    drawScene(tft);
  }
}

void updateGame() {
  if (gameState != PLAYING) {
    return;
  }

  updatePlayerInput();
  updatePlayerPhysics();
  for (int i = 0; i < MAX_ENEMIES; i++) {
    updateEnemyAI(enemies[i]);
  }

  handleCombat();
  updateSpawning();
  advanceStageIfNeeded();
}

void setup() {
  randomSeed(esp_random());
  setupHardware();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);
  initFrameBuffer();

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
  updateVibration();

  if (gameState == TITLE) {
    if (bluePressed) {
      beginGame();
    }
  } else if (gameState == PLAYING) {
    unsigned long now = millis();
    while (now - lastGameUpdateMs >= GAME_UPDATE_INTERVAL_MS) {
      lastGameUpdateMs += GAME_UPDATE_INTERVAL_MS;
      updateGame();
    }
  } else if (gameState == GAME_OVER) {
    if (bluePressed) {
      beginGame();
    } else if (greenPressed) {
      gameState = TITLE;
      lastRenderedState = GAME_OVER;
    }
  }

  if (gameState == TITLE) {
    if (lastRenderedState != TITLE) {
      if (frameBufferReady) {
        drawTitle(frameBuffer);
        frameBuffer.pushSprite(0, 0);
      } else {
        drawTitle(tft);
      }
      lastRenderedState = TITLE;
    }
  } else if (gameState == PLAYING) {
    renderPlaying();
    lastRenderedState = PLAYING;
  } else {
    if (lastRenderedState != GAME_OVER) {
      if (frameBufferReady) {
        drawGameOver(frameBuffer);
        frameBuffer.pushSprite(0, 0);
      } else {
        drawGameOver(tft);
      }
      lastRenderedState = GAME_OVER;
    }
  }

  delay(1);
}
