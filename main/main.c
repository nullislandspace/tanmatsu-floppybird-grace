#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/audio.h"
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "bsp/led.h"
#include "bsp/power.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "hal/lcd_types.h"
#include "pax_gfx.h"

#include "flappy_sounds.h"
#include "hershey_font.h"

static char const TAG[] = "floppybird";

// Screen dimensions (after 270° rotation: 480x800 buffer -> 800x480 screen)
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

// Game tuning constants (calibrated for ~60fps)
#define GRAVITY       0.35f
#define FLAP_VELOCITY -6.5f
#define MAX_VELOCITY  10.0f
#define PIPE_WIDTH    60
#define PIPE_GAP      140
#define PIPE_SPACING  250
#define PIPE_SPEED    3.0f
#define GROUND_HEIGHT 60
#define BIRD_WIDTH    30
#define BIRD_HEIGHT   36
#define BIRD_X        150.0f
#define MAX_PIPES     6

// Pipe gap Y range (center of gap)
#define MIN_GAP_Y 100
#define MAX_GAP_Y (SCREEN_HEIGHT - GROUND_HEIGHT - 100)

// Colors (ARGB for PAX)
#define SKY_COLOR      0xFF87CEEB
#define GROUND_COLOR   0xFFDEB887
#define GROUND_EDGE    0xFF8B4513
#define GROUND_STRIPE  0xFFC8A86E
#define PIPE_COLOR     0xFF228B22
#define PIPE_CAP_COLOR 0xFF1A6B1A
#define FLOPPY_BODY    0xFF222222
#define FLOPPY_LABEL   0xFF4169E1
#define FLOPPY_SLIDER  0xFFC0C0C0
#define FLOPPY_HUB     0xFF444444
#define WING_COLOR     0xFFAAAAAA

// Sound indices
#define SOUND_FLAP  0
#define SOUND_SCORE 1
#define SOUND_DIE   2

// --- Game types ---

typedef enum { STATE_TITLE, STATE_PLAYING, STATE_GAME_OVER } game_state_t;

typedef struct {
    float y, velocity;
} floppy_t;

typedef struct {
    float x, gap_y;
    bool  scored, active;
} pipe_t;

typedef struct {
    game_state_t state;
    floppy_t     floppy;
    pipe_t       pipes[MAX_PIPES];
    int          score, high_score;
    uint32_t     frame_count;
    float        ground_scroll;
    uint32_t     game_over_timer;
    uint32_t     led_timer;
} game_data_t;

// --- Audio types ---

extern void bsp_audio_initialize(uint32_t rate);

#define MAX_ACTIVE_SOUNDS 4
#define FRAMES_PER_WRITE  64
#define SAMPLE_RATE       44100

typedef struct {
    const int16_t *sample_data;
    uint32_t       sample_length;
    uint32_t       playback_position;
    bool           active;
    float          volume;
} active_sound_t;

// --- Globals ---

static size_t                       display_h_res        = 0;
static size_t                       display_v_res        = 0;
static lcd_color_rgb_pixel_format_t display_color_format = LCD_COLOR_PIXEL_FORMAT_RGB888;
static lcd_rgb_data_endian_t        display_data_endian  = LCD_RGB_DATA_ENDIAN_LITTLE;
static pax_buf_t                    fb                   = {0};
static QueueHandle_t                input_event_queue    = NULL;
static game_data_t                  game                 = {0};

// Audio globals
static i2s_chan_handle_t i2s_handle                      = NULL;
static active_sound_t    active_sounds[MAX_ACTIVE_SOUNDS];
static volatile bool     sound_trigger[3] = {false, false, false};

// NVS
#define NVS_NAMESPACE "floppybirds"
#define NVS_KEY_HIGHSCORE "highscore"

// Simple xorshift32 PRNG (seeded from timer)
static uint32_t rng_state = 1;

static uint32_t game_rand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

// --- Audio task (runs on Core 1) ---

void audio_task(void *arg) {
    int16_t output_buffer[FRAMES_PER_WRITE * 2];  // Stereo
    size_t  bytes_written;

    while (1) {
        // Check for new sound triggers
        for (int i = 0; i < 3; i++) {
            if (sound_trigger[i]) {
                for (int slot = 0; slot < MAX_ACTIVE_SOUNDS; slot++) {
                    if (!active_sounds[slot].active || active_sounds[slot].sample_data == flappy_samples[i]) {
                        active_sounds[slot].sample_data      = flappy_samples[i];
                        active_sounds[slot].sample_length     = flappy_lengths[i];
                        active_sounds[slot].playback_position = 0;
                        active_sounds[slot].volume            = 0.3f;
                        active_sounds[slot].active            = true;
                        break;
                    }
                }
                sound_trigger[i] = false;
            }
        }

        // Mix active sounds into output buffer
        for (int frame = 0; frame < FRAMES_PER_WRITE; frame++) {
            float mix = 0.0f;

            for (int i = 0; i < MAX_ACTIVE_SOUNDS; i++) {
                if (active_sounds[i].active) {
                    int16_t sample = active_sounds[i].sample_data[active_sounds[i].playback_position];
                    mix += (sample / 32768.0f) * active_sounds[i].volume;
                    active_sounds[i].playback_position++;
                    if (active_sounds[i].playback_position >= active_sounds[i].sample_length) {
                        active_sounds[i].active = false;
                    }
                }
            }

            // Soft clip
            mix = fminf(1.0f, fmaxf(-1.0f, mix));
            int16_t out                = (int16_t)(mix * 32767.0f);
            output_buffer[frame * 2]     = out;
            output_buffer[frame * 2 + 1] = out;
        }

        // Write stereo PCM to I2S
        if (i2s_handle != NULL) {
            i2s_channel_write(i2s_handle, output_buffer, sizeof(output_buffer), &bytes_written, portMAX_DELAY);
        }
    }
}

static void play_sound(int sound_id) {
    if (sound_id >= 0 && sound_id < 3) {
        sound_trigger[sound_id] = true;
    }
}

// --- NVS high score ---

static void load_high_score(void) {
    nvs_handle_t handle;
    esp_err_t    res = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (res != ESP_OK) {
        game.high_score = 0;
        return;
    }
    int32_t value = 0;
    res           = nvs_get_i32(handle, NVS_KEY_HIGHSCORE, &value);
    if (res == ESP_OK) {
        game.high_score = (int)value;
    } else {
        game.high_score = 0;
    }
    nvs_close(handle);
}

static void save_high_score(void) {
    nvs_handle_t handle;
    esp_err_t    res = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(res));
        return;
    }
    res = nvs_set_i32(handle, NVS_KEY_HIGHSCORE, (int32_t)game.high_score);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write high score: %s", esp_err_to_name(res));
        nvs_close(handle);
        return;
    }
    nvs_commit(handle);
    nvs_close(handle);
}

// --- Display ---

static void blit(void) {
    bsp_display_blit(0, 0, display_h_res, display_v_res, pax_buf_get_pixels(&fb));
}

// --- Game logic ---

static void reset_game(void) {
    int saved_high_score = game.high_score;
    game.floppy.y        = SCREEN_HEIGHT / 2.0f;
    game.floppy.velocity = 0;
    game.score           = 0;
    game.high_score      = saved_high_score;
    game.frame_count     = 0;
    game.ground_scroll   = 0;
    game.game_over_timer = 0;
    for (int i = 0; i < MAX_PIPES; i++) {
        game.pipes[i].active = false;
    }
}

static void spawn_pipes_if_needed(void) {
    // Find rightmost active pipe
    float rightmost_x = 0;
    bool  has_active   = false;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (game.pipes[i].active && game.pipes[i].x > rightmost_x) {
            rightmost_x = game.pipes[i].x;
            has_active   = true;
        }
    }

    float next_x = has_active ? rightmost_x + PIPE_SPACING : (float)SCREEN_WIDTH;

    // Keep spawning until we have pipes past the right edge
    while (next_x < SCREEN_WIDTH + PIPE_SPACING) {
        int slot = -1;
        for (int i = 0; i < MAX_PIPES; i++) {
            if (!game.pipes[i].active) {
                slot = i;
                break;
            }
        }
        if (slot < 0) break;

        game.pipes[slot].x      = next_x;
        game.pipes[slot].gap_y  = MIN_GAP_Y + (float)(game_rand() % (MAX_GAP_Y - MIN_GAP_Y + 1));
        game.pipes[slot].scored = false;
        game.pipes[slot].active = true;

        next_x += PIPE_SPACING;
    }
}

static bool check_collision(void) {
    float bx1 = BIRD_X - BIRD_WIDTH / 2.0f;
    float by1 = game.floppy.y - BIRD_HEIGHT / 2.0f;
    float bx2 = BIRD_X + BIRD_WIDTH / 2.0f;
    float by2 = game.floppy.y + BIRD_HEIGHT / 2.0f;

    // Ground
    if (by2 >= SCREEN_HEIGHT - GROUND_HEIGHT) {
        return true;
    }

    // Pipes
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!game.pipes[i].active) continue;

        float px1        = game.pipes[i].x;
        float px2        = game.pipes[i].x + PIPE_WIDTH;
        float gap_top    = game.pipes[i].gap_y - PIPE_GAP / 2.0f;
        float gap_bottom = game.pipes[i].gap_y + PIPE_GAP / 2.0f;

        // Bird overlaps pipe column?
        if (bx2 > px1 && bx1 < px2) {
            // Outside gap = collision
            if (by1 < gap_top || by2 > gap_bottom) {
                return true;
            }
        }
    }
    return false;
}

static void check_scoring(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!game.pipes[i].active || game.pipes[i].scored) continue;

        float pipe_center = game.pipes[i].x + PIPE_WIDTH / 2.0f;
        if (pipe_center < BIRD_X) {
            game.pipes[i].scored = true;
            game.score++;
            if (game.score > game.high_score) {
                game.high_score = game.score;
                save_high_score();
            }
            play_sound(SOUND_SCORE);
            // Green LED flash (turned off by led_timer)
            for (int j = 0; j < 6; j++) bsp_led_set_pixel(j, 0x00FF00);
            bsp_led_send();
            game.led_timer = 30;  // ~0.5s at 60fps
        }
    }
}

// --- Drawing ---

static void draw_floppy(float x, float y) {
    float left = x - BIRD_WIDTH / 2.0f;
    float top  = y - BIRD_HEIGHT / 2.0f;

    // Wing animation: wings up when rising, droop when falling
    float wing_dy = game.floppy.velocity * 1.5f;
    if (wing_dy < -10) wing_dy = -10;
    if (wing_dy > 10) wing_dy = 10;

    // Left wing
    pax_simple_tri(&fb, WING_COLOR, left, y - 5, left, y + 5, left - 12, y + wing_dy);

    // Right wing
    float right = x + BIRD_WIDTH / 2.0f;
    pax_simple_tri(&fb, WING_COLOR, right, y - 5, right, y + 5, right + 12, y + wing_dy);

    // Body (dark floppy disk)
    pax_simple_rect(&fb, FLOPPY_BODY, left, top, BIRD_WIDTH, BIRD_HEIGHT);

    // Label (colored rectangle on upper portion)
    pax_simple_rect(&fb, FLOPPY_LABEL, left + 3, top + 3, BIRD_WIDTH - 6, 16);

    // White text area on label
    pax_simple_rect(&fb, 0xFFFFFFFF, left + 5, top + 6, BIRD_WIDTH - 10, 4);

    // Metal slider at top
    pax_simple_rect(&fb, FLOPPY_SLIDER, left + 8, top, BIRD_WIDTH - 16, 3);

    // Hub/spindle hole
    pax_simple_circle(&fb, FLOPPY_HUB, x, top + 26, 5);
    pax_simple_circle(&fb, FLOPPY_BODY, x, top + 26, 3);

    // Write-protect notch
    pax_simple_rect(&fb, 0xFF444444, left + BIRD_WIDTH - 4, top, 4, 5);
}

static void draw_pipe(float x, float gap_y) {
    float gap_top        = gap_y - PIPE_GAP / 2.0f;
    float gap_bottom     = gap_y + PIPE_GAP / 2.0f;
    float playable_bottom = SCREEN_HEIGHT - GROUND_HEIGHT;

    // Top pipe body
    if (gap_top > 0) {
        pax_simple_rect(&fb, PIPE_COLOR, x, 0, PIPE_WIDTH, gap_top);
        // Cap at bottom of top pipe
        float cap_y = gap_top - 20;
        if (cap_y < 0) cap_y = 0;
        pax_simple_rect(&fb, PIPE_CAP_COLOR, x - 4, cap_y, PIPE_WIDTH + 8, gap_top - cap_y);
    }

    // Bottom pipe body
    if (gap_bottom < playable_bottom) {
        pax_simple_rect(&fb, PIPE_COLOR, x, gap_bottom, PIPE_WIDTH, playable_bottom - gap_bottom);
        // Cap at top of bottom pipe
        float cap_h = 20;
        if (gap_bottom + cap_h > playable_bottom) cap_h = playable_bottom - gap_bottom;
        pax_simple_rect(&fb, PIPE_CAP_COLOR, x - 4, gap_bottom, PIPE_WIDTH + 8, cap_h);
    }
}

static void draw_ground(void) {
    float ground_y = SCREEN_HEIGHT - GROUND_HEIGHT;

    // Main ground fill
    pax_simple_rect(&fb, GROUND_COLOR, 0, ground_y, SCREEN_WIDTH, GROUND_HEIGHT);

    // Dark edge at top
    pax_simple_rect(&fb, GROUND_EDGE, 0, ground_y, SCREEN_WIDTH, 3);

    // Scrolling stripes for motion effect
    int offset = -((int)game.ground_scroll % 40);
    for (int x = offset; x < SCREEN_WIDTH; x += 40) {
        pax_simple_rect(&fb, GROUND_STRIPE, x, ground_y + 3, 20, GROUND_HEIGHT - 3);
    }
}

static void draw_outlined_text(const char *text, int screen_x, int screen_y, float font_height, uint8_t r,
                               uint8_t g, uint8_t b) {
    uint8_t *fb_pixels = (uint8_t *)pax_buf_get_pixels(&fb);

    // Black outline in 4 directions
    hershey_draw_string(fb_pixels, display_h_res, display_v_res, screen_x - 1, screen_y, text, font_height, 0, 0, 0);
    hershey_draw_string(fb_pixels, display_h_res, display_v_res, screen_x + 1, screen_y, text, font_height, 0, 0, 0);
    hershey_draw_string(fb_pixels, display_h_res, display_v_res, screen_x, screen_y - 1, text, font_height, 0, 0, 0);
    hershey_draw_string(fb_pixels, display_h_res, display_v_res, screen_x, screen_y + 1, text, font_height, 0, 0, 0);

    // Foreground text
    hershey_draw_string(fb_pixels, display_h_res, display_v_res, screen_x, screen_y, text, font_height, r, g, b);
}

// --- Main ---

void app_main(void) {
    gpio_install_isr_service(0);

    // Initialize NVS
    esp_err_t nvs_res = nvs_flash_init();
    if (nvs_res == ESP_ERR_NVS_NO_FREE_PAGES || nvs_res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_res = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_res);

    // Initialize BSP
    const bsp_configuration_t bsp_configuration = {
        .display =
            {
                .requested_color_format = LCD_COLOR_PIXEL_FORMAT_RGB888,
                .num_fbs                = 1,
            },
    };
    esp_err_t res = bsp_device_initialize(&bsp_configuration);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BSP: %d", res);
        return;
    }

    // Get display parameters
    res = bsp_display_get_parameters(&display_h_res, &display_v_res, &display_color_format, &display_data_endian);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get display parameters: %d", res);
        return;
    }

    // PAX buffer format
    pax_buf_type_t format = PAX_BUF_24_888RGB;
    switch (display_color_format) {
        case LCD_COLOR_PIXEL_FORMAT_RGB565:
            format = PAX_BUF_16_565RGB;
            break;
        case LCD_COLOR_PIXEL_FORMAT_RGB888:
            format = PAX_BUF_24_888RGB;
            break;
        default:
            break;
    }

    // Display rotation
    bsp_display_rotation_t display_rotation = bsp_display_get_default_rotation();
    pax_orientation_t      orientation      = PAX_O_UPRIGHT;
    switch (display_rotation) {
        case BSP_DISPLAY_ROTATION_90:
            orientation = PAX_O_ROT_CCW;
            break;
        case BSP_DISPLAY_ROTATION_180:
            orientation = PAX_O_ROT_HALF;
            break;
        case BSP_DISPLAY_ROTATION_270:
            orientation = PAX_O_ROT_CW;
            break;
        default:
            break;
    }

    // Initialize framebuffer
    pax_buf_init(&fb, NULL, display_h_res, display_v_res, format);
    pax_buf_reversed(&fb, display_data_endian == LCD_RGB_DATA_ENDIAN_BIG);
    pax_buf_set_orientation(&fb, orientation);

    // Input queue
    ESP_ERROR_CHECK(bsp_input_get_queue(&input_event_queue));

    // LEDs off
    for (int i = 0; i < 6; i++) bsp_led_set_pixel(i, 0x000000);
    bsp_led_send();
    bsp_led_set_mode(false);

    // Audio init
    bsp_audio_initialize(SAMPLE_RATE);
    bsp_audio_get_i2s_handle(&i2s_handle);
    bsp_audio_set_amplifier(true);
    bsp_audio_set_volume(100);
    memset(active_sounds, 0, sizeof(active_sounds));

    // Audio mixing task on Core 1
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, configMAX_PRIORITIES - 2, NULL, 1);

    // Enable vsync
    SemaphoreHandle_t vsync_sem = NULL;
    esp_err_t         te_err    = bsp_display_set_tearing_effect_mode(BSP_DISPLAY_TE_V_BLANKING);
    if (te_err == ESP_OK) {
        te_err = bsp_display_get_tearing_effect_semaphore(&vsync_sem);
    }
    if (te_err != ESP_OK || vsync_sem == NULL) {
        ESP_LOGW(TAG, "Vsync not available - animation may stutter");
        vsync_sem = NULL;
    }

    // Seed PRNG from timer
    rng_state = (uint32_t)esp_timer_get_time();
    if (rng_state == 0) rng_state = 1;

    // Initialize game
    reset_game();
    load_high_score();
    game.state = STATE_TITLE;

    // === Main game loop ===

    while (1) {
        // --- Process input ---
        bsp_input_event_t event;
        bool              space_pressed = false;

        while (xQueueReceive(input_event_queue, &event, 0) == pdTRUE) {
            if (event.type == INPUT_EVENT_TYPE_SCANCODE) {
                if (event.args_scancode.scancode == BSP_INPUT_SCANCODE_ESC) {
                    bsp_device_restart_to_launcher();
                }
            } else if (event.type == INPUT_EVENT_TYPE_KEYBOARD) {
                if (event.args_keyboard.ascii == ' ') {
                    space_pressed = true;
                }
            }
        }

        // --- Update game state ---

        switch (game.state) {
            case STATE_TITLE:
                // Bob the floppy up and down
                game.floppy.y        = SCREEN_HEIGHT / 2.0f + sinf(game.frame_count * 0.05f) * 20.0f;
                game.floppy.velocity = 0;

                // Scroll ground for visual effect
                game.ground_scroll += PIPE_SPEED;
                if (game.ground_scroll >= 40) game.ground_scroll -= 40;

                if (space_pressed) {
                    reset_game();
                    game.state           = STATE_PLAYING;
                    game.floppy.velocity = FLAP_VELOCITY;
                    play_sound(SOUND_FLAP);
                }
                break;

            case STATE_PLAYING:
                // Gravity
                game.floppy.velocity += GRAVITY;
                if (game.floppy.velocity > MAX_VELOCITY) game.floppy.velocity = MAX_VELOCITY;
                game.floppy.y += game.floppy.velocity;

                // Ceiling clamp (don't kill, just stop)
                if (game.floppy.y < BIRD_HEIGHT / 2.0f) {
                    game.floppy.y = BIRD_HEIGHT / 2.0f;
                    if (game.floppy.velocity < 0) game.floppy.velocity = 0;
                }

                // Flap
                if (space_pressed) {
                    game.floppy.velocity = FLAP_VELOCITY;
                    play_sound(SOUND_FLAP);
                }

                // Move pipes
                for (int i = 0; i < MAX_PIPES; i++) {
                    if (game.pipes[i].active) {
                        game.pipes[i].x -= PIPE_SPEED;
                        if (game.pipes[i].x < -PIPE_WIDTH - 10) {
                            game.pipes[i].active = false;
                        }
                    }
                }

                spawn_pipes_if_needed();
                check_scoring();

                // Ground scroll
                game.ground_scroll += PIPE_SPEED;
                if (game.ground_scroll >= 40) game.ground_scroll -= 40;

                // Collision check
                if (check_collision()) {
                    game.state           = STATE_GAME_OVER;
                    game.game_over_timer = 0;
                    play_sound(SOUND_DIE);
                    // Red LED flash
                    for (int i = 0; i < 6; i++) bsp_led_set_pixel(i, 0xFF0000);
                    bsp_led_send();
                }
                break;

            case STATE_GAME_OVER:
                // Let floppy fall to ground
                game.floppy.velocity += GRAVITY;
                if (game.floppy.velocity > MAX_VELOCITY) game.floppy.velocity = MAX_VELOCITY;
                game.floppy.y += game.floppy.velocity;

                // Stop at ground
                {
                    float ground_stop = SCREEN_HEIGHT - GROUND_HEIGHT - BIRD_HEIGHT / 2.0f;
                    if (game.floppy.y > ground_stop) {
                        game.floppy.y        = ground_stop;
                        game.floppy.velocity = 0;
                    }
                }

                game.game_over_timer++;

                if (space_pressed && game.game_over_timer > 30) {
                    // LEDs off
                    for (int i = 0; i < 6; i++) bsp_led_set_pixel(i, 0x000000);
                    bsp_led_send();
                    reset_game();
                    game.state = STATE_TITLE;
                }
                break;
        }

        game.frame_count++;

        // LED timer: turn off after timeout
        if (game.led_timer > 0) {
            game.led_timer--;
            if (game.led_timer == 0) {
                for (int i = 0; i < 6; i++) bsp_led_set_pixel(i, 0x000000);
                bsp_led_send();
            }
        }

        // --- Render ---

        // Sky
        pax_background(&fb, SKY_COLOR);

        // Pipes
        for (int i = 0; i < MAX_PIPES; i++) {
            if (game.pipes[i].active) {
                draw_pipe(game.pipes[i].x, game.pipes[i].gap_y);
            }
        }

        // Ground
        draw_ground();

        // Floppy disk character
        draw_floppy(BIRD_X, game.floppy.y);

        // Score display (during play and game over)
        if (game.state == STATE_PLAYING || game.state == STATE_GAME_OVER) {
            char score_str[16];
            snprintf(score_str, sizeof(score_str), "%d", game.score);
            int sw = hershey_string_width(score_str, 48);
            draw_outlined_text(score_str, (SCREEN_WIDTH - sw) / 2, 20, 48, 255, 255, 255);
        }

        // Title screen overlay
        if (game.state == STATE_TITLE) {
            uint8_t *fb_pixels = (uint8_t *)pax_buf_get_pixels(&fb);

            // "FLOPPY BIRD" title with shadow
            int tw = hershey_string_width("FLOPPY BIRD", 60);
            int tx = (SCREEN_WIDTH - tw) / 2;
            hershey_draw_string(fb_pixels, display_h_res, display_v_res, tx + 2, 62, "FLOPPY BIRD", 60, 0, 0, 0);
            hershey_draw_string(fb_pixels, display_h_res, display_v_res, tx, 60, "FLOPPY BIRD", 60, 255, 255, 0);

            // High score
            if (game.high_score > 0) {
                char hs_str[32];
                snprintf(hs_str, sizeof(hs_str), "Best: %d", game.high_score);
                int hw = hershey_string_width(hs_str, 24);
                draw_outlined_text(hs_str, (SCREEN_WIDTH - hw) / 2, 240, 24, 255, 255, 255);
            }

            // "Press SPACE to play"
            int pw = hershey_string_width("Press SPACE to play", 24);
            draw_outlined_text("Press SPACE to play", (SCREEN_WIDTH - pw) / 2, 280, 24, 255, 255, 255);

            // "ESC: Back to launcher"
            int ew = hershey_string_width("ESC: Back to launcher", 16);
            hershey_draw_string(fb_pixels, display_h_res, display_v_res, (SCREEN_WIDTH - ew) / 2, 320,
                                "ESC: Back to launcher", 16, 255, 255, 255);
        }

        // Game over overlay
        if (game.state == STATE_GAME_OVER) {
            uint8_t *fb_pixels = (uint8_t *)pax_buf_get_pixels(&fb);

            // "GAME OVER"
            int gow = hershey_string_width("GAME OVER", 50);
            int gox = (SCREEN_WIDTH - gow) / 2;
            hershey_draw_string(fb_pixels, display_h_res, display_v_res, gox + 2, 152, "GAME OVER", 50, 0, 0, 0);
            hershey_draw_string(fb_pixels, display_h_res, display_v_res, gox, 150, "GAME OVER", 50, 255, 50, 50);

            // Score
            char score_str[32];
            snprintf(score_str, sizeof(score_str), "Score: %d", game.score);
            int sw = hershey_string_width(score_str, 30);
            draw_outlined_text(score_str, (SCREEN_WIDTH - sw) / 2, 220, 30, 255, 255, 255);

            // High score
            snprintf(score_str, sizeof(score_str), "Best: %d", game.high_score);
            sw = hershey_string_width(score_str, 30);
            draw_outlined_text(score_str, (SCREEN_WIDTH - sw) / 2, 260, 30, 255, 255, 255);

            // Restart prompt (after debounce)
            if (game.game_over_timer > 30) {
                int rw = hershey_string_width("Press SPACE to play again", 20);
                draw_outlined_text("Press SPACE to play again", (SCREEN_WIDTH - rw) / 2, 340, 20, 255, 255, 255);
            }
        }

        // Blit to display
        blit();

        // Wait for vsync
        if (vsync_sem != NULL) {
            xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(50));
        }
    }
}
