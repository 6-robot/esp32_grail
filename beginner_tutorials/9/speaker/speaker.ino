#include <driver/i2s.h>

static int MAX98357_LRC_Pin =   13;
static int MAX98357_BCLK_PIn =  14;
static int MAX98357_DIN_Pin =   4;
#define SAMPLE_RATE 44100

i2s_config_t i2sOut_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(16),
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024
};

const i2s_pin_config_t i2sOut_pin_config = {
    .bck_io_num = MAX98357_BCLK_PIn,
    .ws_io_num = MAX98357_LRC_Pin,
    .data_out_num = MAX98357_DIN_Pin,
    .data_in_num = -1
};

void setup() {
    i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);
}

#define DO_1 262  // Do
#define RE_2 294  // Re
#define MI_3 330  // Mi
#define FA_4 349  // Fa
#define SO_5 392  // So
#define LA_6 440  // La

void generateTone(int16_t* buffer, int frequency, int duration, int amplitude) {
    const float pi = 3.14159;
    float period = SAMPLE_RATE / float(frequency);
    int samples = (SAMPLE_RATE * duration) / 1000;
    for(int i = 0; i < samples; i++) {
        buffer[i] = amplitude * sin(2 * pi * i / period);
    }
}

int16_t music_buffer[SAMPLE_RATE * 2];

void loop() {
    delay(3000);
    
    int notes[] = {DO_1, RE_2, MI_3, FA_4, SO_5, LA_6};
    int duration = 300;
    int amplitude = 10000;
    
    int notes_length = sizeof(notes) / sizeof(notes[0]);
    for(int i = 0; i < notes_length; i++) {
        generateTone(music_buffer, notes[i], duration, amplitude);
        size_t bytes_written;
        i2s_write(I2S_NUM_1, music_buffer, duration * (SAMPLE_RATE/1000) * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    }
    
    i2s_zero_dma_buffer(I2S_NUM_1);
}