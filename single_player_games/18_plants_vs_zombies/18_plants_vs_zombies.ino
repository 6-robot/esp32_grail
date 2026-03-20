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

#define MAX98357_LRC 13
#define MAX98357_BCLK 14
#define MAX98357_DIN 4
#define SAMPLE_RATE 16000
#define AUDIO_CHUNK_SAMPLES 128

TFT_eSPI tft = TFT_eSPI(320, 480);
TFT_eSprite frameBufferLeft = TFT_eSprite(&tft);
TFT_eSprite frameBufferRight = TFT_eSprite(&tft);

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int FRAME_W = SCREEN_W / 2;
static const int BANNER_H = 52;
static const int LAWN_H = SCREEN_H - BANNER_H;

static const int LANE_COUNT = 4;
static const int CELL_W = LAWN_H / LANE_COUNT;
static const int CELL_H = CELL_W;
static const int GRID_COLS = (SCREEN_W + CELL_W - 1) / CELL_W;
static const int GRID_X = 0;
static const int GRID_Y = BANNER_H;
static const int GRID_W = GRID_COLS * CELL_W;
static const int GRID_H = LANE_COUNT * CELL_H;

static const int MAX_ZOMBIES = 12;
static const int MAX_PEAS = 24;

enum GameState {
  TITLE,
  PLAYING,
  LEADERBOARD,
  WIN,
  LOSE
};

enum PlantType : uint8_t {
  PLANT_NONE = 0,
  PLANT_PEASHOOTER = 1,
  PLANT_WALLNUT = 2
};

struct Plant {
  PlantType type;
  int hp;
  unsigned long actionAtMs;
  unsigned long recoilUntilMs;
};

struct Zombie {
  bool active;
  int lane;
  float x;
  int hp;
  unsigned long attackAtMs;
};

struct Pea {
  bool active;
  int lane;
  float x;
  float y;
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

GameState gameState = TITLE;
GameState lastRenderedState = LOSE;

Plant plants[LANE_COUNT][GRID_COLS];
Zombie zombies[MAX_ZOMBIES];
Pea peas[MAX_PEAS];

bool frameBufferReady = false;
bool greenDown = false;
bool blueDown = false;
bool greenPressed = false;
bool bluePressed = false;
bool leftPressed = false;
bool rightPressed = false;
bool upPressed = false;
bool downPressed = false;
bool lastLeftDown = false;
bool lastRightDown = false;
bool lastUpDown = false;
bool lastDownDown = false;
int latestX = 2048;
int latestY = 2048;

int cursorRow = 2;
int cursorCol = 0;
int sunPoints = 100;
int defeatedZombies = 0;
int totalSpawned = 0;
int spawnedThisWave = 0;
const int targetDefeats = 18;
static const int LEADERBOARD_SIZE = 5;
unsigned long nextSunMs = 0;
unsigned long nextSpawnMs = 0;
unsigned long stateEnteredMs = 0;
unsigned long lastUpdateMs = 0;
int leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0};
int latestRank = -1;
bool scoreSubmitted = false;

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
  {392, 180}, {523, 180}, {659, 180}, {523, 180},
  {440, 180}, {523, 180}, {587, 180}, {523, 180},
  {392, 180}, {523, 180}, {659, 180}, {523, 180},
  {330, 180}, {440, 180}, {523, 240}, {0, 80}
};
const int BGM_NOTE_COUNT = sizeof(bgmLoop) / sizeof(bgmLoop[0]);

struct WindowCanvas {
  TFT_eSprite *sprite;
  int xOffset;

  void fillScreen(uint16_t color) { sprite->fillScreen(color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) { sprite->fillRect(x - xOffset, y, w, h, color); }
  void drawRect(int x, int y, int w, int h, uint16_t color) { sprite->drawRect(x - xOffset, y, w, h, color); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) { sprite->drawLine(x0 - xOffset, y0, x1 - xOffset, y1, color); }
  void drawFastHLine(int x, int y, int w, uint16_t color) { sprite->drawFastHLine(x - xOffset, y, w, color); }
  void fillCircle(int x, int y, int r, uint16_t color) { sprite->fillCircle(x - xOffset, y, r, color); }
  void drawCircle(int x, int y, int r, uint16_t color) { sprite->drawCircle(x - xOffset, y, r, color); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    sprite->fillRoundRect(x - xOffset, y, w, h, r, color);
  }
  void setTextColor(uint16_t fg, uint16_t bg) { sprite->setTextColor(fg, bg); }
  void setTextDatum(uint8_t datum) { sprite->setTextDatum(datum); }
  int drawString(const char *text, int x, int y, int font) { return sprite->drawString(text, x - xOffset, y, font); }
  int drawString(const String &text, int x, int y, int font) { return sprite->drawString(text, x - xOffset, y, font); }
  int drawCentreString(const char *text, int x, int y, int font) { return sprite->drawCentreString(text, x - xOffset, y, font); }
};

struct ScreenCanvas {
  TFT_eSPI *screen;

  void fillScreen(uint16_t color) { screen->fillScreen(color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) { screen->fillRect(x, y, w, h, color); }
  void drawRect(int x, int y, int w, int h, uint16_t color) { screen->drawRect(x, y, w, h, color); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) { screen->drawLine(x0, y0, x1, y1, color); }
  void drawFastHLine(int x, int y, int w, uint16_t color) { screen->drawFastHLine(x, y, w, color); }
  void fillCircle(int x, int y, int r, uint16_t color) { screen->fillCircle(x, y, r, color); }
  void drawCircle(int x, int y, int r, uint16_t color) { screen->drawCircle(x, y, r, color); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) { screen->fillRoundRect(x, y, w, h, r, color); }
  void setTextColor(uint16_t fg, uint16_t bg) { screen->setTextColor(fg, bg); }
  void setTextDatum(uint8_t datum) { screen->setTextDatum(datum); }
  int drawString(const char *text, int x, int y, int font) { return screen->drawString(text, x, y, font); }
  int drawString(const String &text, int x, int y, int font) { return screen->drawString(text, x, y, font); }
  int drawCentreString(const char *text, int x, int y, int font) { return screen->drawCentreString(text, x, y, font); }
};

enum SpriteKind : uint8_t {
  SPRITE_SHOOTER = 0,
  SPRITE_WALLNUT = 1,
  SPRITE_ZOMBIE = 2
};

static const char *const SHOOTER_SPRITE[] = {
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  ".................................................KKKKK..........",
  "......................KKKKKKKKKKKK...............KKOKKK.........",
  ".....................KKKOOTCCCCTKKKKK...........KKMMLOKKK.......",
  "...................KKKTLLLLLLLLLLLLOKKKK......KKKMLLLMMSKK......",
  ".................KKKCLLLLLLLLLLLLLLLLLTKKK..KKKTLLLMSOMMKK......",
  "................KKOLHHHLLLLLLLLLLLLLLLLSKKKKKOLHLMLSKKSLDK......",
  "..............KKKTHHHHHLLLLLLLLLLLLLLLCOSMMMMLHHLLLKKKKGMKK.....",
  ".......KKKKK.KKKCHHHHHHLLLLLLLLLMDMLLLTKKLLLLLHHLLOKKKKSLOK.....",
  "......KKKSOKKKKCHHHHHHLLLLLLLLLLCCOOMLDKKLLLLLHLLLKKOKKSLSK.....",
  ".....KKSMMGSSKOLHHHHHHLLLLLLLLLLHTKKSLLMMLLLLLLLLLKKKKKSLDK.....",
  ".....KTLMGDDSKMLHHHHHLLLLLLLLLLMOKKKGLLLLLLLLLLLMLKKKSKOLDK.....",
  "....KKMLSKKKKKLLLLLLLLLLLLLLLLLLSKKOMLLLLLLLMMLLMLKKKOKSLDK.....",
  ".....KMGKKKKKOLLLLLLLLLLLLLLLLLLLMDMLLLLLLLLMMLLMLKKKSKSLSK.....",
  ".....KGSKK..KKLLLLLLLLLLLLLLLLLLLLLLLLLLLMMMMMMLMMKKKKKSLOK.....",
  ".....KOKK...KKLLLLLLLLLLLLLLLLLLLLLLLLLLMMMMMGMMMMGKKKKSMKK.....",
  ".....KKK.....KSLLLLLLLLLLLLLLLLLLLLLLLMMMMMGGGDDMMLDKKOMDKK.....",
  ".....KK......KKLLLLLLLLLLLLLLLLLLLLLMMMMMMGGMMGDDMMLDSMMKK......",
  ".............KKSLLMLLLLLLLLLLLLLLMMMMMMMMGMSOOOOKOMLLLMOKK......",
  "..............KKOMLMMLLLLLLLLLMMMMMMMMMGGGSKKKKKKKOSGGKKK.......",
  "...............KKOMMMMMMMMMMMMMMMMMMMGGGGSKKKKKKKKKKKKKK........",
  "................KKKMLMMMMMMMMMMMMMGGGGGDOKKK.......KKK..........",
  ".................KKKMMMMMMMMGGGGGGGMGGSOKKK.....................",
  "..................KKKSGMMMMMMMMMMGGDSOKKK.......................",
  "....................KKKOSSSDDDDSSSOKKKKK........................",
  ".....................KKKKKOOOOOOKKKKKKK.........................",
  "......................KKKOSSSSSSOKKK............................",
  "........................KKKSDSSKKK..............................",
  ".........................KKKDDSKK...............................",
  "........................K.KKKGSK................................",
  ".......................KKKKKOMSKK...KKKKKKK.....................",
  "....................KKKKOOKKSMDKK..KKKKSKKKKKKK................",
  "...................KKKKDDDSKSGDK.KKKSGDSSSSOOKKKK...............",
  "..................KKKSGGDDGSSDGKKKKDGSSSDGGGMDKKKKKK............",
  ".................KKKKOGGDDDDSSGKKKSGSSDGGGGGGGGGSKKK............",
  "..................KKKSGDDDDDDSGGKKSSSDGGGGGGGGGGGSKK............",
  ".................KKKKOSSSKSSSSSGDSSSDGGGGGGGGGGGGGOK............",
  "...............KKKKSDGGGDSSSSSSDGSSSGGGGGGGGGGGGGGSKK...........",
  "..............KKKSGMMMMMMMGGDSSSSSSDGGGGGGGGGGGGGGDKKK..........",
  "..............KSMMMGGGMGGGMGGGGDSSSGGGGGGGGGGMMMMGGOKK..........",
  ".............KSGMMMMMMMMMMMMMMMMGGDGGGGGGGGMMGDDGMGOKK..........",
  "............KKOGMMMMMMMGGGGGGGGGGGDGGMMMGGMMDKKKKDGKKK..........",
  "............KKGMMMMMMMMGGGGGGGGGGGDGMGGGMMMSKKKKKKDKKK..........",
  "............KSMMMMMMMMMMMMMGGMMMMGDDMMGMMMDKKKKKKKKKKK..........",
  "............KSMMMMMMMMMMMMGGGGGMMGSSGMMMMDKK..KKKKK.............",
  "............KSMMMDSSSDGGMMGGGGGGGDKKSGDGSKK....KKKK.............",
  "............KKGMSKKKKKKSGGMMGMMDKKKKKOKKKK......................",
  "............KKSSKKK.KKKKKSDGGGDKKK.KKKKKK.......................",
  ".............KKKKKK...KKKKKKKKKKK...............................",
  "..............KKKK..............................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................"
};

static const char *const WALLNUT_SPRITE[] = {
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................K...............................",
  "........................KKKKKKKKKKK.............................",
  "........................KKKKKMMMMKKKKK..........................",
  "......................KKKLMMLLLLLLLLMKK.........................",
  "....................KKLLLMMLHLLLLLLLLLKK........................",
  "...................KKMLMMMLHLLLLLLLLLLMKKK......................",
  "..................KKDLMMLLLLLLLLLLLLLLLMLKK.....................",
  ".................KKMLMMHLLLLLLLLLLLLLLLLLLKK....................",
  ".................KDLLMMLLLLLLLLLLLLLLLLLLLMKK...................",
  "................KKMLMLMLLMMMLLLLLLLLLLLLLLLLKK..................",
  "...............KKLMMMLLLLMLLLLLLLLLLLLLLLLLLLKK.................",
  "...............KLMLMLLLLLLLLLLLLLLLLLLLLLLLLLMKK................",
  "..............KKLMLMLLLLLLLLLLLLLLLLLLLLLLLLLLKK................",
  ".............KKLLMLMLLLLLLLLLLLLLLLLLLLLLLLLLLMKK...............",
  "............KKDLLMMMLLLLLLLMMMMLLLLLLLLLLLLLLLMKK...............",
  "............KMLLMLMLLLLLLLMMLLLMMLLLLLLLMMLLMLMKK...............",
  "............KLLMLMMLLLLLLMHHHHHHMMLLLLLMLHHHHLMKK...............",
  "...........KKLLMLMMLLLLLMHHHHHHHHMLLLLLMHHHHHHLMKK..............",
  "...........KLLLMLMMLLLLLLHHHHKKHHHMLLLLLHHKKHHHLKK..............",
  "..........KKKLLMLMMLLLLMHHHHHKKHHHLLLLLHHHKKHHHLKKK.............",
  ".........KKKMLLMLMLLLLLMHHHHHKKHHHLLLLLLHHKKHHHLLMK.............",
  ".........KKMLLMMMMLLLLLMHHHHHKKHHHMLLLLLHHHHHHLLLLKK............",
  ".........KMLLLMLMMLLLLLLLHHHHHHHHHMLLLLLLHHHHHLLLLKK............",
  ".........KLLLLMLLMLLLLLLMHHHHHHHHLLLLLLLLLLLLLLLLLKK............",
  "........KKLLLMMLLMLLLLLLLLLHHHHHLLLKKKKMLLLLLLLLLMKK............",
  "........KKLLLMMLMMLLLLLLLLLLLLLLLLMKKKMMLLLLLLLLLMMK............",
  "........KKLLLMMLMMLLLLLLLLLLLLLLLLLKLLMMLLLLLLLLLMMK............",
  "........KKLMLMMLMMLLLLLLLLLLLLLLLLLLLMMLLLLLLLLLLLMK............",
  "........KKLLMMMLMMMLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLMMK............",
  "........KKLMMMMLLMMMLLLLLLLLLLLLLLLLLLLLLLLLLLLLLMMK............",
  "........KKLLMMMMLMMMLLLLLLLLLLLLLLLLLLLLLLLLLLLLMMMK............",
  "........KKLLMMMLLMMMMLLLLLLLLLLLLLLLLLLLLLLLLLLLMMMK............",
  "........KKMMMMMLLMMMMMLLLLLLLLLLLLLLLLLLLLLLLLLLMMMK............",
  "........KKMMMMMMLMMMMMMLLLLLLLLLLLLLLLLLLLLLLLLMMMMK............",
  ".........KKMMMMMLMMMMMLLLLLLLLLLLLLLLLLLLLLLLLMMMLMK............",
  "..........KMMMMMLMMMMMMLLLLLLLLLLLLLLLLLLLLLLMMMMMMK............",
  "..........KMMMMMMMMMMMMMLLLLLLLLLLLLLLLLLLLLLLMMMMKK............",
  "..........KMMMMMMMMMMMMMMMLLLMMLLLLLLLLLLLLLLLMMMMK.............",
  "..........KMMMMMMLMMMMMMMMMLLLMMMMMMLLLLLLLLLMMMMKK.............",
  "..........KKMMMMDMMMMMMMMMMMLLMMMMMLLLLLLLLLMMMMMK..............",
  "...........KKMMMMMMDMMMMMMMMMMMMLLLLLLLMMMMMMMMMMK..............",
  "............KMMMMMMDMMMMMMMMMMMLLLMMLMMMMMMMMMMMKK..............",
  "............KKMMMMMMDMMMMLLLMMMMMMMMMMMMMMMMMMLMK...............",
  ".............KMMMDMMMDMMMMLLMMMMMMMMMMMMMLMMMMMKK...............",
  ".............KKMMMDMMDDMMMMMMMMMMMMLMMMMLLMMMMKK................",
  "..............KKMMMDMMDMMMMMMMMMMMMMMMMMMMMMMKK.................",
  "...............KKKMMMMDMMMMMMMLLMMMMMMMMMMMMMK..................",
  "................IKKMMMMDMMMMMMMMMMMMMMMMMMMMKK..................",
  "..................KKMMMMDMMMMMMMMMMMMMMMMMMKK...................",
  "...................KKMDMMDMMMMMMMMMMMMMMMMKK....................",
  "....................KKDMMDMMMMMMMMMMMMMMMKK.....................",
  ".....................KKKMMDMMMMMMMMMMMMMKK......................",
  ".......................KKKKKKKKKKMKKKKKKK.......................",
  "........................KKKKKKKKKKKKKKK.........................",
  "...........................KKKKKKK..............................",
  "................................................................",
  "................................................................"
};

static const char *const WALLNUT_DAMAGED_SPRITE[] = {
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "................................................................",
  "...........................KKK...KKKK...........................",
  "......................KO..KKDKKKKKKKKKKKK.......................",
  "....................KKKKK.KDIKOKKDIIIDKOKKK.....................",
  "...................KKOIOKKODIIIIIIIIIIIIKIKK....................",
  "...............KKKKKIOOOIODIIIIIIIIIIIIIKDIKK...................",
  "............KKKKKKOIOKKOIIIIIIIIIIIIIIIIKMMDKK..................",
  "............KKOOOIIOOMOOOIIIIIIIIIIIIIIIKIMMDKK.................",
  "............KOIIIIIOMLOOOOOIIOOOOOOOOOIKKMMMMDKK................",
  "............KKOIIIIOLLOOODMLLDOOOOOOOOOKMMMMMMKK................",
  ".............KKOIIODLLDMLLLLLLMIOOOKOKKKMMMMMMDKK...............",
  ".............KOKOIOMMMLLLLMMMLLLDMLDKKDMDIDDIDMIKK..............",
  "............KKHMKOOMMLLLMDMMMDILLLLLIIMODHHHHLIDOK..............",
  "...........KKLLLDKMLMLMDDHHHHHMOLLLLMMDDHHHHHHLDDKK.............",
  "...........KLLLLMILMMLDIHHHHHHHLDLLLLMOHHHHHHHHDDIK.............",
  "..........KDLLLLMDLMLMIHHHHHHHHHDMLLLMIHHHHHHHHMDDKK............",
  ".........KKMLLLLMMLMLMIHHHHHHHHHLMLLLMIHHHLHHHHMDMOK............",
  ".........KOLLLLLMMMMLMIHHHHHHLHHLDLLLLOHHHHHHHHDDMOK............",
  ".........KMLMLLLMMMLLMOHHHHHHHHHLDLLLLDMHHHHHHLIMMIK............",
  ".........KMMMMLLMDLLLMOLHHHHHHHHDMLLLLMOMHHHHLOMMDDK............",
  ".........KMMMMLMMLLLLLDOHHHHHHHLIMLLLLLMIIMMDODMDDDKK...........",
  ".........KMMMLMMMLLLLLMDOHHHHHMIMMLLLLLMMMDDDMMMDDDOK...........",
  ".........KMMDMMMMLLLLLMMDODMDOIMMLLMDDMMMMMMMMMMMMDDKK..........",
  ".........KMMMOMMMLLLLLLLMMDDDMMMMLDDMMDDMMMMMMMMMDDDKK..........",
  ".........KKMOOMMMLLLLLLLLLMMMMMMMMDMMMMMMMMMMMMMMDDDKK..........",
  "..........KKKIMDMMLLLMMMMLMMMMMMMMMMMMMMMMMMMMMMMDDDK...........",
  "..........KKKMMDMMLMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDDDKK..........",
  "............KMMDMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDMIDIKK..........",
  "............KKMDMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDMIDOKK..........",
  ".............KMDMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDIDOK...........",
  "............KKMIMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDIDDOK...........",
  "...........KKOIOMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDDDDOK...........",
  "...........KDIOOMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDDMIK...........",
  "..........KKIOOOMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDDDDIK...........",
  "..........KOOOOOMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDDDDKK...........",
  "..........KOIOOOMMMMMMMMMMMMMMMDDDMMMMMMMMMMMMMMIDIK............",
  "..........KOOOODDMMMMMMMMMMMMMMMDDMMMMMMMMMMMMDMIDOK............",
  "..........KKOODMDMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMDIOKK............",
  "...........KOIIDIMDMMMMMMMMMMMMMMMMMMMMMMDDMMMDIIOKK............",
  "...........KKDIIODDDDMMMMMMMMMMMMMMMMMMMMMMMDDDDDOK.............",
  "............KKIIIIMDDDMMMMMMMMMMMDMMMDDMMMMDDDDDIKK.............",
  "............KKOIIOMDDIMMMMMMMDMDMDDMMDDMMMDDDDIDKK..............",
  ".............KKOIODMDDDMMMMDMMMDDDDDMDDMDDMDDIDKKK..............",
  "..............KKKIODMDIIMMMMDDDDDDDDMMMDODMDDDOKK...............",
  "................KKOODMDIDDDMMDDDDDDDDDDIDMDDDOKK................",
  ".................KKKKIDDDDDDMMDDDDDDDDDDDDDIOKK.................",
  "...................KKKODDDDDIDMDDDDDDDDDDOOKKK..................",
  ".....................KKKIDDDIOOIDDDDDDDDOKKK....................",
  ".......................KKKKKKKKKKKKKKKOOKK......................",
  "........................KKKKKKKKKKKKKKKK........................",
  "................................................................",
  "................................................................"
};

static const char *const ZOMBIE_SPRITE[] = {
  "...........OKKKKKKKKKKKKKKKKKKKKKM..............................",
  "..........OKKOGGGGGGGGGGGGGGGGMKKK..............................",
  ".........KKKTLLLGGGGGGGGGGGGGGTTOKKK............................",
  ".......OKKOGLGGGGGGGTTTGGGGGGTTTTBKKK...........................",
  "......OKKMGLGGGGGGGTGGGGGGGGGGGTTTMOK...........................",
  ".....KKOTGGTGGGGTTTGGGGGGGTTTGGGTTTBKKK.........................",
  "...KKKOGGGGGTTTTTTGGGGGMBOOBBMMGGTTMOKK.........................",
  "...KKOGGGGGGGGGGGGGGGGOOMMMMMBOMTTTTBKKO........................",
  "...KOTTTTGGGGGGGGGGGTOBGWWWWLGOBTTTOKKKK........................",
  "...KOBOBBBOBGGGGGGGMOMWWWWWWWLMOBTTOKBBK........................",
  "...KOBMGLGMOOTGGGGGMMWWWWLLLWWWMOTTOKBBK........................",
  "...BOMWWWWLGBTGGGGGMOOGWWMKKGWWMOMTOKBBK........................",
  "...KOLWMKLWLBMGGGGGGMOGWWMKKGWLMOMTOKBBK........................",
  "...KOLWBKLWGBMGGBOTGMBLWWLLLWWLMBTTOKOOKO.......................",
  "...KKOGLLWGBBBMTMMMGGBOGWWWWWGMOBTGMKKKKK.......................",
  "....KKBMMMOOOMGGGGGGGTOOMMMMMBOBTMOKKKK.........................",
  "....KKKBTMTTMTTGTTGTTGGMOOOBBMMMTTOOOKKK........................",
  ".....KKOMMMMMGGMGLMLLMTGGTTTTTGMBMOOOOKKKK......................",
  "......KKKKOOMWWOMLTGGOOMMTTTTTMMMMMOOOOKKKK.....................",
  "........KKKKMGGOOOMOOOROMTTMTTMMGGMOOOOOOKKK....................",
  "..........KKOOOOORORROMMMMGTTTMMMMMOOMMOOOKKK...................",
  "..........KKBGMOOOOOOOMMOMGTTMMOOBOOMMMOOOOKK...................",
  "..........KKMMTTMRMTMTMMMTTTOOOOMGOOMMMMOOOKKKK.................",
  "..........KKKOGGGGGGGGGGTMBOOOOOMGOOMMMMOOOOOKKK................",
  "............KKKKBGTTTGGTMOOOOOOOMMOOMMMOOOOOOOKK................",
  "..............OKKKKKKKKKKKKOOMGMMMOOMMMOOOOOOOKKK...............",
  ".........................KKOOORMOKOOMMMOOOOOOOOKKKK.............",
  ".........................KKMMMROKKOMMMMOOOOOOOOOOKK.............",
  "........................MKKORRGGOKOMMMMOOOOOOOOOOKKK............",
  "........................KKOGRRRROKOMMMOOOOOOOOOOOOKKK...........",
  ".......................KKKRRLLRROKOMMMOOOOOOOOOOOOOKKKK.........",
  "........................KMRRRRLGMKOMMOOOOOOOOOOOOOOOOKKK........",
  ".......................KKMLGRRRRMKOMMOOKKKOOOOOOOOOOOOKKK.......",
  "......................KKKRRRLLGROKOMMMOKKOOOOOOOOOOOOOOKK.......",
  "......................KKKMRRRRGWMKOMMMOKKOOMMOOOOOOOOOOKK.......",
  "......................KKOKOLLRRROKOMMMOKOOOOOOOOOOOOOKKKK.......",
  "......................KOMOKKMWGOKKOMMMOKOOOOOOOOOOOOOKK.........",
  "......................KKOMOKKKOOKKOMMMOKOMOOOOOOOOOOKKK.........",
  ".....................KKKOOOOKKKBOKOMMMOKOMOOOOOOOOOOKK..........",
  ".....................KOMMMOOKKKKKKOOOOOKOOOOOOOOOOOOKK..........",
  ".....................KOMOOOOKKOKKOMMMMOKKOOOOOBBBBBOKK..........",
  "...................KKKOMMOOOKKMKKOMMMMOKKOOOOBBBBBBOKKK.........",
  "...................KKBTGGGTBKK.KKOMOOOOKKOKBPPBBBBBBKKK.........",
  "...................KOGGGGGTMOK.KOGGTTMOKKKKOBPPPBPPBKKK.........",
  "...................KOGGMMMTMOKKKOGGGGGOKKKKKKBPPPPPBOKK.........",
  "..................KKKTMOMTTMKKKOMGGGGGMKKK.KKOBPPPBBBOK.........",
  "..................KKOMMMMMMMOKKMGGGGGGGTKKMKKOBBPBBBBOK.........",
  "..................KOTMBBMMMBOKKMGGGGGTTGOKBKKMBBBBBOKKK.........",
  "..................KOTGTMMMOKKKKMGGGTTGTMKKBKKTGTOKKKKK..........",
  "..................KKKMTMBMOKKKKTGBTGMGGMKKBKOGGGOKKK............",
  "..................KKKKKKKKKKKKKMTOMGTMMMKK.KOGGGTMOKKK..........",
  "............................KKKKKKOTGTMOKK.KKKOGGGMOKK..........",
  "...........................KKKKKKKKKKKKKKK..KKKOTGGBKKK.........",
  "..........................KKKOOOBBKKKK........KKKTGTBKKKK.......",
  "...........................KOBBBBBKKO..........KKKOBBOKKKKKK....",
  ".........................KKKOBBBBBKKK.........BKKKBBBBOOOOKK....",
  ".........................KKOBBBBBBOKKKKKKK....OKKBPPPBBBBBKKO...",
  "........................KKKBBBBBBBBOOOKKKKKKKKKKOPPPPBBBBOKKK...",
  "........................KKOBBBBBOOOOOOOOOOOOOKKOBBBBPBBBBBBKK...",
  "........................KOBBOOOOOOMOKKOMMMMMMMMOOOOOBBBBBBBKK...",
  "........................KOOOOOOOMOKKKMGMMMMMMMMMMMMOOOOBBBBKK...",
  "........................KKOOOOOOKKKOMMMMMMMMMMOOOOOOOOOOOOOKK...",
  "........................KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK...",
  "................................................................"
};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t spriteColor(SpriteKind kind, char code, bool damaged = false) {
  if (code == 'K') return TFT_BLACK;
  if (code == 'O') return kind == SPRITE_ZOMBIE ? rgb565(56, 46, 36) : rgb565(60, 48, 32);

  if (kind == SPRITE_SHOOTER) {
    if (code == 'D') return rgb565(56, 140, 58);
    if (code == 'S') return rgb565(58, 122, 54);
    if (code == 'C') return rgb565(72, 164, 68);
    if (code == 'G') return rgb565(76, 162, 70);
    if (code == 'M') return rgb565(92, 188, 84);
    if (code == 'L') return rgb565(146, 228, 132);
    if (code == 'H') return rgb565(208, 248, 196);
  } else if (kind == SPRITE_WALLNUT) {
    if (code == 'D') return damaged ? rgb565(104, 68, 36) : rgb565(132, 84, 42);
    if (code == 'M') return damaged ? rgb565(132, 86, 46) : rgb565(166, 108, 58);
    if (code == 'L') return damaged ? rgb565(182, 132, 80) : rgb565(216, 164, 104);
    if (code == 'H') return damaged ? rgb565(208, 164, 114) : rgb565(238, 196, 146);
    if (code == 'I') return damaged ? rgb565(74, 46, 24) : rgb565(92, 56, 30);
  } else if (kind == SPRITE_ZOMBIE) {
    if (code == 'G') return rgb565(136, 176, 132);
    if (code == 'L') return rgb565(196, 226, 182);
    if (code == 'M') return rgb565(112, 92, 84);
    if (code == 'W') return rgb565(228, 232, 218);
    if (code == 'P') return rgb565(112, 98, 156);
    if (code == 'R') return rgb565(194, 58, 54);
    if (code == 'B') return rgb565(64, 54, 92);
    if (code == 'T') return rgb565(86, 126, 82);
  }

  return TFT_BLACK;
}

template <typename CanvasT>
void drawSprite(CanvasT &canvas, int x, int y, const char *const *rows, int rowCount, int scale, SpriteKind kind, bool damaged = false) {
  for (int r = 0; r < rowCount; r++) {
    const char *row = rows[r];
    for (int c = 0; row[c] != '\0'; c++) {
      char code = row[c];
      if (code == '.') {
        continue;
      }
      canvas.fillRect(x + c * scale, y + r * scale, scale, scale, spriteColor(kind, code, damaged));
    }
  }
}

template <typename CanvasT>
void drawSpriteRows(CanvasT &canvas, int x, int y, const char *const *rows, int rowStart, int rowEnd, int scale, SpriteKind kind, bool damaged = false) {
  for (int r = rowStart; r < rowEnd; r++) {
    const char *row = rows[r];
    for (int c = 0; row[c] != '\0'; c++) {
      char code = row[c];
      if (code == '.') {
        continue;
      }
      canvas.fillRect(x + c * scale, y + (r - rowStart) * scale, scale, scale, spriteColor(kind, code, damaged));
    }
  }
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

void enqueueVoice(float freq, int ms, float volume, WaveType wave, float slidePerSample = 0.0f) {
  portENTER_CRITICAL(&audioMux);
  for (int i = 0; i < 6; ++i) {
    if (!sfxVoices[i].active) {
      sfxVoices[i].active = true;
      sfxVoices[i].freq = freq;
      sfxVoices[i].phase = 0.0f;
      sfxVoices[i].remainSamples = max(1, (SAMPLE_RATE * ms) / 1000);
      sfxVoices[i].volume = volume;
      sfxVoices[i].wave = wave;
      sfxVoices[i].slidePerSample = slidePerSample;
      break;
    }
  }
  portEXIT_CRITICAL(&audioMux);
}

void playPlaceSfx(PlantType type) {
  if (type == PLANT_PEASHOOTER) {
    enqueueVoice(520.0f, 90, 0.10f, WAVE_TRIANGLE, 0.03f);
    enqueueVoice(760.0f, 70, 0.07f, WAVE_SQUARE, -0.04f);
  } else {
    enqueueVoice(210.0f, 140, 0.11f, WAVE_TRIANGLE, -0.01f);
    enqueueVoice(140.0f, 180, 0.07f, WAVE_NOISE, -0.02f);
  }
}

void playPeaShotSfx() {
  enqueueVoice(980.0f, 28, 0.10f, WAVE_SQUARE, -0.20f);
  enqueueVoice(620.0f, 36, 0.06f, WAVE_TRIANGLE, -0.10f);
}

void playPlantEatenSfx() {
  enqueueVoice(180.0f, 140, 0.10f, WAVE_NOISE, -0.03f);
  enqueueVoice(130.0f, 160, 0.07f, WAVE_TRIANGLE, -0.02f);
}

void playZombieDeathSfx() {
  enqueueVoice(320.0f, 100, 0.10f, WAVE_TRIANGLE, -0.06f);
  enqueueVoice(220.0f, 180, 0.09f, WAVE_NOISE, -0.03f);
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
  if (!audioEnabled || gameState != PLAYING) {
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
  return base * 0.005f;
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

bool plantHasZombieAhead(int row, int col) {
  float startX = GRID_X + col * CELL_W + CELL_W / 2;
  for (int i = 0; i < MAX_ZOMBIES; i++) {
    if (zombies[i].active && zombies[i].lane == row && zombies[i].x > startX - 8) {
      return true;
    }
  }
  return false;
}

void clearEntities() {
  for (int r = 0; r < LANE_COUNT; r++) {
    for (int c = 0; c < GRID_COLS; c++) {
      plants[r][c].type = PLANT_NONE;
      plants[r][c].hp = 0;
      plants[r][c].actionAtMs = 0;
      plants[r][c].recoilUntilMs = 0;
    }
  }

  for (int i = 0; i < MAX_ZOMBIES; i++) {
    zombies[i].active = false;
  }

  for (int i = 0; i < MAX_PEAS; i++) {
    peas[i].active = false;
  }
}

void beginGame() {
  clearEntities();
  cursorRow = 2;
  cursorCol = 0;
  sunPoints = 125;
  defeatedZombies = 0;
  totalSpawned = 0;
  spawnedThisWave = 0;
  nextSunMs = millis() + 3500;
  nextSpawnMs = millis() + 2200;
  stateEnteredMs = millis();
  lastUpdateMs = millis();
  gameState = PLAYING;
  lastRenderedState = PLAYING;
  latestRank = -1;
  scoreSubmitted = false;
  bgmIndex = 0;
  bgmPhase = 0.0f;
  bgmRemainSamples = max(1, (int)((SAMPLE_RATE * bgmLoop[0].ms) / 1000));
}

void enterTitle() {
  clearEntities();
  cursorRow = 2;
  cursorCol = 0;
  sunPoints = 125;
  defeatedZombies = 0;
  totalSpawned = 0;
  spawnedThisWave = 0;
  latestRank = -1;
  scoreSubmitted = false;
  stateEnteredMs = millis();
  gameState = TITLE;
  lastRenderedState = TITLE;
}

void submitScoreToLeaderboard() {
  if (scoreSubmitted) {
    return;
  }

  int score = defeatedZombies;
  latestRank = LEADERBOARD_SIZE;
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    if (score > leaderboard[i]) {
      latestRank = i;
      break;
    }
  }

  if (latestRank < LEADERBOARD_SIZE) {
    for (int i = LEADERBOARD_SIZE - 1; i > latestRank; --i) {
      leaderboard[i] = leaderboard[i - 1];
    }
    leaderboard[latestRank] = score;
  } else {
    latestRank = -1;
  }

  scoreSubmitted = true;
}

void initFrameBuffers() {
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
  bool g = (digitalRead(green_btn_pin) == LOW);
  bool b = (digitalRead(blue_btn_pin) == LOW);
  latestX = analogRead(x_pin);
  latestY = analogRead(y_pin);

  bool leftNow = latestX < 1200;
  bool rightNow = latestX > 2900;
  bool upNow = latestY < 1200;
  bool downNow = latestY > 2900;

  greenPressed = g && !greenDown;
  bluePressed = b && !blueDown;
  leftPressed = leftNow && !lastLeftDown;
  rightPressed = rightNow && !lastRightDown;
  upPressed = upNow && !lastUpDown;
  downPressed = downNow && !lastDownDown;

  greenDown = g;
  blueDown = b;
  lastLeftDown = leftNow;
  lastRightDown = rightNow;
  lastUpDown = upNow;
  lastDownDown = downNow;
}

void spawnZombie() {
  int laneCounts[LANE_COUNT];
  float laneRightmost[LANE_COUNT];
  for (int lane = 0; lane < LANE_COUNT; lane++) {
    laneCounts[lane] = 0;
    laneRightmost[lane] = (float)SCREEN_W;
  }

  for (int i = 0; i < MAX_ZOMBIES; i++) {
    if (!zombies[i].active) {
      continue;
    }
    laneCounts[zombies[i].lane]++;
    if (zombies[i].x > laneRightmost[zombies[i].lane]) {
      laneRightmost[zombies[i].lane] = zombies[i].x;
    }
  }

  int minCount = laneCounts[0];
  for (int lane = 1; lane < LANE_COUNT; lane++) {
    if (laneCounts[lane] < minCount) {
      minCount = laneCounts[lane];
    }
  }

  int candidateLanes[LANE_COUNT];
  int candidateCount = 0;
  for (int lane = 0; lane < LANE_COUNT; lane++) {
    if (laneCounts[lane] == minCount) {
      candidateLanes[candidateCount++] = lane;
    }
  }

  int spawnLane = candidateLanes[random(candidateCount)];
  float spawnX = max((float)SCREEN_W + random(16, 48), laneRightmost[spawnLane] + 72.0f + random(0, 28));

  for (int i = 0; i < MAX_ZOMBIES; i++) {
    if (!zombies[i].active) {
      zombies[i].active = true;
      zombies[i].lane = spawnLane;
      zombies[i].x = spawnX;
      zombies[i].hp = 5;
      zombies[i].attackAtMs = 0;
      totalSpawned++;
      spawnedThisWave++;
      return;
    }
  }
}

void emitPea(int row, int col) {
  for (int i = 0; i < MAX_PEAS; i++) {
    if (!peas[i].active) {
      peas[i].active = true;
      peas[i].lane = row;
      peas[i].x = GRID_X + col * CELL_W + 50;
      peas[i].y = GRID_Y + row * CELL_H + 17;
      plants[row][col].recoilUntilMs = millis() + 34;
      playPeaShotSfx();
      return;
    }
  }
}

void updatePlants(unsigned long now) {
  for (int r = 0; r < LANE_COUNT; r++) {
    for (int c = 0; c < GRID_COLS; c++) {
      Plant &plant = plants[r][c];
      if (plant.type == PLANT_NONE) {
        continue;
      }

      if (plant.type == PLANT_PEASHOOTER) {
        if (plantHasZombieAhead(r, c) && now >= plant.actionAtMs) {
          emitPea(r, c);
          plant.actionAtMs = now + 5600;
        }
      }
    }
  }
}

void damagePlant(int row, int col, int damage) {
  if (row < 0 || row >= LANE_COUNT || col < 0 || col >= GRID_COLS) {
    return;
  }

  Plant &plant = plants[row][col];
  if (plant.type == PLANT_NONE) {
    return;
  }

  plant.hp -= damage;
  if (plant.hp <= 0) {
    playPlantEatenSfx();
    plant.type = PLANT_NONE;
    plant.hp = 0;
    plant.actionAtMs = 0;
    plant.recoilUntilMs = 0;
  }
}

void updatePeas() {
  for (int i = 0; i < MAX_PEAS; i++) {
    if (!peas[i].active) {
      continue;
    }

    peas[i].x += 6.0f;
    bool consumed = false;
    for (int z = 0; z < MAX_ZOMBIES; z++) {
      if (!zombies[z].active || zombies[z].lane != peas[i].lane) {
        continue;
      }

      if (abs((int)(peas[i].x - zombies[z].x)) < 14) {
        zombies[z].hp -= 1;
        peas[i].active = false;
        consumed = true;
        if (zombies[z].hp <= 0) {
          zombies[z].active = false;
          defeatedZombies++;
          sunPoints += 25;
          playZombieDeathSfx();
        }
        break;
      }
    }

    if (!consumed && peas[i].x > SCREEN_W - 16) {
      peas[i].active = false;
    }
  }
}

void updateZombies(unsigned long now) {
  for (int i = 0; i < MAX_ZOMBIES; i++) {
    Zombie &zombie = zombies[i];
    if (!zombie.active) {
      continue;
    }

    int col = (int)((zombie.x - GRID_X) / CELL_W);
    bool attacking = false;
    if (col >= 0 && col < GRID_COLS) {
      Plant &target = plants[zombie.lane][col];
      if (target.type != PLANT_NONE) {
        attacking = true;
        if (zombie.attackAtMs == 0 || now >= zombie.attackAtMs) {
          damagePlant(zombie.lane, col, 1);
          zombie.attackAtMs = now + 700;
        }
      }
    }

    if (!attacking) {
      zombie.attackAtMs = 0;
      zombie.x -= 0.78f;
    }

    if (zombie.x < GRID_X - 18) {
      gameState = LOSE;
      stateEnteredMs = now;
      return;
    }
  }
}

void placePlant(PlantType type) {
  if (gameState != PLAYING) {
    return;
  }

  Plant &slot = plants[cursorRow][cursorCol];
  if (slot.type != PLANT_NONE) {
    return;
  }

  int cost = (type == PLANT_PEASHOOTER) ? 100 : 50;
  if (sunPoints < cost) {
    return;
  }

  sunPoints -= cost;
  slot.type = type;
  slot.hp = (type == PLANT_PEASHOOTER) ? 5 : 18;
  slot.actionAtMs = millis() + 500;
  slot.recoilUntilMs = 0;
  playPlaceSfx(type);
}

void updateCursor() {
  if (leftPressed) {
    cursorCol = max(0, cursorCol - 1);
  }
  if (rightPressed) {
    cursorCol = min(GRID_COLS - 1, cursorCol + 1);
  }
  if (upPressed) {
    cursorRow = max(0, cursorRow - 1);
  }
  if (downPressed) {
    cursorRow = min(LANE_COUNT - 1, cursorRow + 1);
  }
}

template <typename CanvasT>
void drawBackground(CanvasT &canvas) {
  uint16_t sky = rgb565(146, 216, 255);
  uint16_t grassA = rgb565(76, 170, 72);
  uint16_t grassB = rgb565(96, 190, 82);

  canvas.fillScreen(sky);
  canvas.fillRect(0, BANNER_H, SCREEN_W, SCREEN_H - BANNER_H, grassA);

  for (int r = 0; r < LANE_COUNT; r++) {
    for (int c = 0; c < GRID_COLS; c++) {
      uint16_t color = ((r + c) % 2 == 0) ? grassA : grassB;
      canvas.fillRect(GRID_X + c * CELL_W, GRID_Y + r * CELL_H, CELL_W - 2, CELL_H - 2, color);
    }
  }
}

template <typename CanvasT>
void drawPlantCard(CanvasT &canvas, int x, int y, PlantType type, bool selected) {
  const int contentOffset = (type == PLANT_PEASHOOTER) ? 30 : 30;
  uint16_t frame = selected ? rgb565(255, 244, 120) : rgb565(227, 214, 185);
  canvas.fillRoundRect(x, y, 120, 44, 10, frame);
  canvas.fillRoundRect(x + 3, y + 3, 114, 38, 8, rgb565(96, 67, 46));
  canvas.fillRect(x + 6, y + 6, 108, 7, rgb565(132, 98, 72));
  canvas.setTextColor(TFT_WHITE, rgb565(96, 67, 46));
  canvas.setTextDatum(TL_DATUM);
  canvas.drawString("$", x + 40 + contentOffset, y + 14, 2);
  canvas.drawString(type == PLANT_PEASHOOTER ? "100" : "50", x + 52 + contentOffset, y + 14, 2);

  if (type == PLANT_PEASHOOTER) {
    uint16_t dark = rgb565(42, 110, 42);
    uint16_t mid = rgb565(72, 170, 72);
    uint16_t light = rgb565(126, 224, 118);
    uint16_t buttonSide = rgb565(24, 96, 38);
    uint16_t buttonTop = rgb565(54, 178, 74);
    uint16_t buttonLight = rgb565(150, 238, 154);
    canvas.fillRoundRect(x + 10, y + 22, 18, 10, 4, TFT_BLACK);
    canvas.fillRoundRect(x + 11, y + 24, 16, 7, 3, buttonSide);
    canvas.fillRoundRect(x + 9, y + 18, 20, 8, 4, TFT_BLACK);
    canvas.fillRoundRect(x + 10, y + 19, 18, 6, 3, buttonTop);
    canvas.fillRect(x + 14, y + 20, 6, 2, buttonLight);
    canvas.fillRect(x + 10 + contentOffset, y + 31, 18, 4, rgb565(58, 48, 30));
    canvas.fillRect(x + 17 + contentOffset, y + 22, 4, 11, dark);
    canvas.fillRect(x + 10 + contentOffset, y + 14, 16, 12, dark);
    canvas.fillRect(x + 12 + contentOffset, y + 16, 12, 8, mid);
    canvas.fillRect(x + 24 + contentOffset, y + 17, 7, 6, mid);
    canvas.fillRect(x + 14 + contentOffset, y + 18, 6, 3, light);
    canvas.fillRect(x + 17 + contentOffset, y + 18, 2, 2, TFT_BLACK);
    canvas.fillRect(x + 10 + contentOffset, y + 27, 6, 3, mid);
    canvas.fillRect(x + 20 + contentOffset, y + 27, 6, 3, mid);
  } else {
    uint16_t dark = rgb565(118, 74, 35);
    uint16_t mid = rgb565(164, 108, 56);
    uint16_t light = rgb565(210, 160, 98);
    uint16_t buttonSide = rgb565(28, 66, 128);
    uint16_t buttonTop = rgb565(60, 122, 224);
    uint16_t buttonLight = rgb565(156, 206, 255);
    canvas.fillRoundRect(x + 10, y + 22, 18, 10, 4, TFT_BLACK);
    canvas.fillRoundRect(x + 11, y + 24, 16, 7, 3, buttonSide);
    canvas.fillRoundRect(x + 9, y + 18, 20, 8, 4, TFT_BLACK);
    canvas.fillRoundRect(x + 10, y + 19, 18, 6, 3, buttonTop);
    canvas.fillRect(x + 14, y + 20, 6, 2, buttonLight);
    canvas.fillRect(x + 10 + contentOffset, y + 31, 18, 4, rgb565(58, 48, 30));
    canvas.fillRect(x + 11 + contentOffset, y + 11, 18, 20, dark);
    canvas.fillRect(x + 13 + contentOffset, y + 13, 14, 16, mid);
    canvas.fillRect(x + 15 + contentOffset, y + 14, 8, 4, light);
    canvas.fillRect(x + 16 + contentOffset, y + 20, 2, 2, TFT_BLACK);
    canvas.fillRect(x + 22 + contentOffset, y + 20, 2, 2, TFT_BLACK);
    canvas.drawLine(x + 16 + contentOffset, y + 26, x + 20 + contentOffset, y + 28, TFT_BLACK);
    canvas.drawLine(x + 20 + contentOffset, y + 28, x + 24 + contentOffset, y + 26, TFT_BLACK);
  }
}

template <typename CanvasT>
void drawSunIcon(CanvasT &canvas, int cx, int cy) {
  uint16_t ray = rgb565(255, 181, 48);
  uint16_t glow = rgb565(255, 218, 92);
  canvas.fillRect(cx - 2, cy - 18, 4, 8, ray);
  canvas.fillRect(cx - 2, cy + 10, 4, 8, ray);
  canvas.fillRect(cx - 18, cy - 2, 8, 4, ray);
  canvas.fillRect(cx + 10, cy - 2, 8, 4, ray);
  canvas.fillRect(cx - 12, cy - 12, 5, 5, ray);
  canvas.fillRect(cx + 7, cy - 12, 5, 5, ray);
  canvas.fillRect(cx - 12, cy + 7, 5, 5, ray);
  canvas.fillRect(cx + 7, cy + 7, 5, 5, ray);
  canvas.fillCircle(cx, cy, 10, glow);
  canvas.fillCircle(cx, cy, 6, rgb565(255, 238, 138));
}

template <typename CanvasT>
void drawZombieBannerIcon(CanvasT &canvas, int x, int y) {
  for (int r = 0; r < 64; r += 2) {
    const char *row = ZOMBIE_SPRITE[r];
    for (int c = 0; c < 64; c += 2) {
      char code = row[c];
      if (code == '.') {
        continue;
      }
      canvas.fillRect(x + c / 2, y + r / 2, 1, 1, spriteColor(SPRITE_ZOMBIE, code));
    }
  }
}

template <typename CanvasT>
void drawPlant(CanvasT &canvas, int row, int col, const Plant &plant) {
  int x = GRID_X + col * CELL_W;
  int y = GRID_Y + row * CELL_H;

  if (plant.type == PLANT_PEASHOOTER) {
    const int spriteX = x + 1;
    const int spriteY = y + 1;
    const int recoilShift = (millis() < plant.recoilUntilMs) ? -5 : 0;
    const int splitRow = 32;
    drawSpriteRows(canvas, spriteX + recoilShift, spriteY, SHOOTER_SPRITE, 0, splitRow, 1, SPRITE_SHOOTER);
    drawSpriteRows(canvas, spriteX, spriteY + splitRow, SHOOTER_SPRITE, splitRow, 64, 1, SPRITE_SHOOTER);
  } else if (plant.type == PLANT_WALLNUT) {
    drawSprite(canvas, x + 1, y + 1, plant.hp <= 8 ? WALLNUT_DAMAGED_SPRITE : WALLNUT_SPRITE, 64, 1, SPRITE_WALLNUT, plant.hp <= 8);
  }
}

template <typename CanvasT>
void drawZombie(CanvasT &canvas, const Zombie &zombie) {
  int y = GRID_Y + zombie.lane * CELL_H;
  int x = (int)zombie.x;
  drawSprite(canvas, x + 1, y + 1, ZOMBIE_SPRITE, 64, 1, SPRITE_ZOMBIE);
}

template <typename CanvasT>
void drawPea(CanvasT &canvas, const Pea &pea) {
  int x = (int)pea.x;
  int y = (int)pea.y;
  uint16_t peaColor = rgb565(80, 178, 80);
  uint16_t peaLight = rgb565(150, 238, 154);
  canvas.fillCircle(x, y, 8, TFT_BLACK);
  canvas.fillCircle(x, y, 6, peaColor);
  canvas.fillCircle(x - 2, y - 2, 2, peaLight);
}

template <typename CanvasT>
void drawHud(CanvasT &canvas) {
  uint16_t banner = rgb565(63, 92, 43);
  uint16_t panel = rgb565(240, 226, 176);

  canvas.fillRect(0, 0, SCREEN_W, BANNER_H, banner);
  canvas.drawFastHLine(0, BANNER_H - 1, SCREEN_W, rgb565(31, 58, 20));

  canvas.fillRoundRect(10, 7, 88, 36, 10, rgb565(249, 219, 88));
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(TFT_BLACK, rgb565(249, 219, 88));
  drawSunIcon(canvas, 28, 25);
  canvas.drawString(String(sunPoints), 56, 18, 2);

  drawPlantCard(canvas, 108, 4, PLANT_PEASHOOTER, false);
  drawPlantCard(canvas, 238, 4, PLANT_WALLNUT, false);

  canvas.fillRoundRect(372, 4, 98, 44, 10, panel);
  canvas.setTextColor(TFT_BLACK, panel);
  drawZombieBannerIcon(canvas, 381, 9);
  canvas.drawString(String(defeatedZombies) + "/" + String(targetDefeats), 419, 18, 2);
}

template <typename CanvasT>
void drawCursor(CanvasT &canvas) {
  int x = GRID_X + cursorCol * CELL_W;
  int y = GRID_Y + cursorRow * CELL_H;
  uint16_t border = rgb565(255, 248, 170);
  canvas.drawRect(x - 1, y - 1, CELL_W, CELL_H, border);
  canvas.drawRect(x - 2, y - 2, CELL_W + 2, CELL_H + 2, lighten565(border, 40));
}

template <typename CanvasT>
void drawPlaying(CanvasT &canvas) {
  drawBackground(canvas);
  drawHud(canvas);

  for (int r = 0; r < LANE_COUNT; r++) {
    for (int c = 0; c < GRID_COLS; c++) {
      if (plants[r][c].type != PLANT_NONE) {
        drawPlant(canvas, r, c, plants[r][c]);
      }
    }
  }

  for (int i = 0; i < MAX_PEAS; i++) {
    if (peas[i].active) {
      drawPea(canvas, peas[i]);
    }
  }

  for (int i = 0; i < MAX_ZOMBIES; i++) {
    if (zombies[i].active) {
      drawZombie(canvas, zombies[i]);
    }
  }

  drawCursor(canvas);
}

template <typename CanvasT>
void drawEnd(CanvasT &canvas, bool win) {
  drawBackground(canvas);
  drawHud(canvas);
  canvas.fillRoundRect(60, 92, 360, 124, 18, win ? rgb565(255, 231, 132) : rgb565(84, 48, 42));
  canvas.setTextColor(win ? TFT_BLACK : TFT_WHITE, win ? rgb565(255, 231, 132) : rgb565(84, 48, 42));
  canvas.drawCentreString(win ? "You Win" : "The Zombies Ate Your Lawn", SCREEN_W / 2, 118, 4);
  canvas.drawCentreString(win ? "Wave cleared." : "Try more wallnuts.", SCREEN_W / 2, 158, 2);
  canvas.drawCentreString("Updating board...", SCREEN_W / 2, 184, 2);
}

template <typename CanvasT>
void drawTitle(CanvasT &canvas) {
  uint16_t bg = rgb565(16, 24, 38);
  uint16_t panel = rgb565(36, 52, 74);
  uint16_t edge = rgb565(132, 180, 234);
  uint16_t grass = rgb565(64, 132, 78);

  canvas.fillScreen(bg);
  for (int y = 0; y < SCREEN_H; y += 18) {
    canvas.drawFastHLine(0, y, SCREEN_W, rgb565(20, 34, 52));
  }
  canvas.fillRoundRect(46, 28, SCREEN_W - 92, 232, 18, panel);
  canvas.drawRect(46, 28, SCREEN_W - 92, 232, edge);
  canvas.fillRect(56, 214, SCREEN_W - 112, 36, grass);

  canvas.setTextColor(rgb565(250, 226, 124), panel);
  canvas.drawCentreString("PLANTS VS ZOMBIES", SCREEN_W / 2, 54, 4);
  canvas.setTextColor(TFT_WHITE, panel);
  canvas.drawCentreString("Joystick: move cursor", SCREEN_W / 2, 108, 2);
  canvas.drawCentreString("Green: place pea shooter", SCREEN_W / 2, 134, 2);
  canvas.drawCentreString("Blue: start game", SCREEN_W / 2, 160, 2);

  drawSprite(canvas, 120, 170, SHOOTER_SPRITE, 64, 1, SPRITE_SHOOTER);
  drawSprite(canvas, 296, 170, ZOMBIE_SPRITE, 64, 1, SPRITE_ZOMBIE);

  uint16_t blink = ((millis() / 450) % 2) ? rgb565(255, 246, 178) : rgb565(136, 132, 94);
  canvas.setTextColor(blink, panel);
  canvas.drawCentreString("PRESS BLUE TO START", SCREEN_W / 2, 230, 2);
}

template <typename CanvasT>
void drawLeaderboard(CanvasT &canvas) {
  uint16_t bg = rgb565(16, 18, 30);
  uint16_t rowBg = rgb565(48, 54, 76);
  uint16_t title = rgb565(246, 212, 90);
  canvas.fillScreen(bg);
  canvas.fillRoundRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, 16, bg);
  canvas.drawRect(56, 24, SCREEN_W - 112, SCREEN_H - 48, rgb565(120, 150, 220));
  canvas.setTextColor(title, bg);
  canvas.drawCentreString("ZOMBIE BOARD", SCREEN_W / 2, 46, 4);

  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawCentreString(("Score " + String(defeatedZombies)).c_str(), SCREEN_W / 2, 78, 2);

  char buf[32];
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    int y = 112 + i * 28;
    uint16_t fillColor = (i == latestRank) ? rgb565(84, 66, 26) : rowBg;
    uint16_t textColor = (i == latestRank) ? rgb565(250, 224, 120) : TFT_WHITE;
    canvas.fillRoundRect(92, y - 8, 296, 24, 6, fillColor);
    canvas.setTextColor(textColor, fillColor);
    snprintf(buf, sizeof(buf), "%d. %d", i + 1, leaderboard[i]);
    canvas.drawString(buf, 156, y - 4, 2);
  }

  if (millis() - stateEnteredMs >= 2000) {
    canvas.setTextColor(TFT_WHITE, bg);
    canvas.drawCentreString("Press Green or Blue", SCREEN_W / 2, 286, 2);
  }
}

void renderSceneToCanvas(bool winState, WindowCanvas &left, WindowCanvas &right) {
  if (gameState == TITLE) {
    drawTitle(left);
    drawTitle(right);
  } else if (gameState == PLAYING) {
    drawPlaying(left);
    drawPlaying(right);
  } else if (gameState == LEADERBOARD) {
    drawLeaderboard(left);
    drawLeaderboard(right);
  } else {
    drawEnd(left, winState);
    drawEnd(right, winState);
  }
}

void render() {
  if (frameBufferReady) {
    WindowCanvas left = {&frameBufferLeft, 0};
    WindowCanvas right = {&frameBufferRight, FRAME_W};
    renderSceneToCanvas(gameState == WIN, left, right);
    frameBufferLeft.pushSprite(0, 0);
    frameBufferRight.pushSprite(FRAME_W, 0);
    return;
  }

  ScreenCanvas whole = {&tft};
  if (gameState == TITLE) {
    drawTitle(whole);
  } else if (gameState == PLAYING) {
    drawPlaying(whole);
  } else if (gameState == LEADERBOARD) {
    drawLeaderboard(whole);
  } else {
    drawEnd(whole, gameState == WIN);
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

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);
  initFrameBuffers();
  initAudio();

  enterTitle();
  render();
}

void loop() {
  readInput();

  if (gameState == TITLE) {
    if (bluePressed) {
      beginGame();
    }
  } else if (gameState == PLAYING) {
    updateCursor();
    if (greenPressed) {
      placePlant(PLANT_PEASHOOTER);
    }
    if (bluePressed) {
      placePlant(PLANT_WALLNUT);
    }

    unsigned long now = millis();
    if (now >= nextSunMs) {
      sunPoints += 25;
      nextSunMs = now + 3600;
    }

    if (spawnedThisWave < targetDefeats && now >= nextSpawnMs) {
      spawnZombie();
      nextSpawnMs = now + random(1800, 3400);
    }

    if (now - lastUpdateMs >= 16) {
      lastUpdateMs = now;
      updatePlants(now);
      updatePeas();
      updateZombies(now);
    }

    if (defeatedZombies >= targetDefeats) {
      gameState = WIN;
      stateEnteredMs = now;
    }
  } else if (gameState == WIN || gameState == LOSE) {
    if (!scoreSubmitted) {
      submitScoreToLeaderboard();
    }
    if (millis() - stateEnteredMs >= 2000) {
      gameState = LEADERBOARD;
      stateEnteredMs = millis();
    }
  } else {
    if (millis() - stateEnteredMs >= 2000 && (greenPressed || bluePressed)) {
      enterTitle();
    }
  }

  render();
  delay(8);
}
