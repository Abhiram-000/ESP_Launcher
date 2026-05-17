/*
 * osd_esp32s3.c — Nofrendo OSD (Operating-System Dependent) layer
 * =================================================================
 * Bridges the Nofrendo emulator core to the ESP32-S3 hardware via:
 *   - LittleFS ROM file I/O (no SD card)
 *   - Fixed-point 256×240 → 160×128 nearest-neighbour scaling
 *   - Double-buffer hand-off to App_NES (Core 1 UI task)
 *   - Disabled audio (setsound is a no-op)
 *   - esp_timer-based 60 Hz interrupt
 */

#include "osd.h"
#include "nofrendo.h"
#include "bitmap.h"
#include "nesinput.h"
#include "vid_drv.h"
#include "nesstate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG_OSD = "NES_OSD";

/* ── Globals shared with App_NES (extern declared in App_NES.cpp) ────────── */
char              active_nes_rom_path[256];  /* Set by App_NES before task start */
uint16_t         *nes_frame_buffer   = NULL; /* Back-buffer written by OSD       */
SemaphoreHandle_t nes_frame_sem      = NULL; /* Signals UI task: new frame ready  */
volatile uint32_t nes_input_state    = 0;    /* NES button bits from App_NES      */
volatile bool     nes_should_exit    = false;/* Set by App_NES to request quit    */
volatile uint8_t  nes_state_cmd      = 0;    /* 0=Idle, 1=Save, 2=Load            */

/* Internal nesinput structure – one player, joypad type 1 */
nesinput_t joypad_p1 = { 1, 0 };  /* type=INP_JOYPAD0, data=0 */

/* ── Palette ─────────────────────────────────────────────────────────────── */
static uint16_t s_palette565[256];

/* ── Video bitmap ────────────────────────────────────────────────────────── */
static bitmap_t *s_bitmap = NULL;

/* ── ROM streaming file handle ───────────────────────────────────────────── */
static FILE *s_rom_file = NULL;

/* ── Timer ───────────────────────────────────────────────────────────────── */
static esp_timer_handle_t s_timer      = NULL;
static void (*s_timer_cb)(void)        = NULL;

static void _timer_isr(void *arg)
{
    if (s_timer_cb) s_timer_cb();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Required OSD API — called by Nofrendo core
 * ═══════════════════════════════════════════════════════════════════════════*/

/* ── Init / Shutdown ──────────────────────────────────────────────────────── */
int osd_init(void)
{
    ESP_LOGI(TAG_OSD, "=== osd_init START ===");
    ESP_LOGI(TAG_OSD, "  Free heap internal : %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG_OSD, "  Free heap SPIRAM   : %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG_OSD, "  ROM path           : %s", active_nes_rom_path);

    /* Verify ROM file is accessible before starting emulator */
    FILE *test = fopen(active_nes_rom_path, "rb");
    if (!test) {
        ESP_LOGE(TAG_OSD, "  ROM file NOT accessible: %s", active_nes_rom_path);
        return -1;
    }
    fseek(test, 0, SEEK_END);
    long rom_size = ftell(test);
    fclose(test);
    ESP_LOGI(TAG_OSD, "  ROM file OK: %ld bytes", rom_size);

    /* Clean up any bitmap leftover from a previous emulator run */
    if (s_bitmap) {
        ESP_LOGW(TAG_OSD, "  s_bitmap was not NULL on init — cleaning up stale bitmap");
        bmp_destroy(&s_bitmap);
        s_bitmap = NULL;
    }

    joypad_p1.type = 1;  /* INP_JOYPAD0 */
    joypad_p1.data = 0;
    input_register(&joypad_p1);

    ESP_LOGI(TAG_OSD, "=== osd_init OK ===");
    return 0;
}

void osd_shutdown(void)
{
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
}

/* ── Timer ───────────────────────────────────────────────────────────────── */
int osd_installtimer(int frequency, void *func, int funcsize,
                     void *counter, int countersize)
{
    (void)funcsize; (void)counter; (void)countersize;
    s_timer_cb = (void(*)(void))func;

    esp_timer_create_args_t args = {
        .callback   = _timer_isr,
        .arg        = NULL,
        .name       = "nes_timer",
    };
    if (esp_timer_create(&args, &s_timer) != ESP_OK) return -1;
    return esp_timer_start_periodic(s_timer, 1000000 / frequency);
}

/* ── Palette ─────────────────────────────────────────────────────────────── */
void osd_setpalette(rgb_t *pal)
{
    if (!pal) return;
    for (int i = 0; i < 256; i++) {
        uint16_t c = ((pal[i].r & 0xF8) << 8)
                   | ((pal[i].g & 0xFC) << 3)
                   |  (pal[i].b >> 3);
        s_palette565[i] = c;
    }
}

/* ── Sound (disabled) ────────────────────────────────────────────────────── */
void osd_setsound(void (*playfunc)(void *buffer, int size))
{
    (void)playfunc; /* Sound explicitly disabled */
}

void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = 16000;
    info->bps         = 16;
}

/* ── Input ───────────────────────────────────────────────────────────────── */
void osd_getinput(void)
{
    /* nes_input_state is set atomically by App_NES::handleInput on Core 1.
       The read here is safe: single-word read is atomic on Xtensa/LX7.    */
    joypad_p1.data = (int)nes_input_state;
}

void osd_getmouse(int *x, int *y, int *button)
{
    *x = *y = *button = 0;
}

/* ── Misc ────────────────────────────────────────────────────────────────── */
int  osd_makesnapname(char *buf, int len) { (void)buf; (void)len; return 0; }
void osd_fullname(char *fullname, const char *shortname) { strncpy(fullname, shortname, PATH_MAX - 1); }
char *osd_newextension(char *string, char *ext)
{
    char *dot = strrchr(string, '.');
    if (dot) strcpy(dot, ext);
    return string;
}

/* ── Video info ──────────────────────────────────────────────────────────── */
void osd_getvideoinfo(vidinfo_t *info)
{
    info->default_width  = 256;
    info->default_height = 240;
    info->driver         = NULL;
}

/* ── ROM data (memory-mapped path — return NULL to use streaming) ───────── */
char *osd_getromdata(void)    { return NULL; }
void  osd_release_romdata(void) {}

/* ── ROM streaming (file-based, LittleFS) ───────────────────────────────── */
int osd_rom_open(const char *path)
{
    (void)path;  /* Ignore Nofrendo's guess — always open our active path */
    if (s_rom_file) { fclose(s_rom_file); s_rom_file = NULL; }
    s_rom_file = fopen(active_nes_rom_path, "rb");
    if (!s_rom_file) {
        ESP_LOGE(TAG_OSD, "Cannot open ROM: %s", active_nes_rom_path);
        return -1;
    }
    return 0;
}

int osd_rom_read(void *dst, int len)
{
    if (!s_rom_file || !dst || len <= 0) return -1;
    return (int)fread(dst, 1, (size_t)len, s_rom_file);
}

void osd_rom_close(void)
{
    if (s_rom_file) { fclose(s_rom_file); s_rom_file = NULL; }
}

/* ── Memory allocator ────────────────────────────────────────────────────── */
void *mem_alloc(int size, bool prefer_fast_memory)
{
    void *p = NULL;
    if (prefer_fast_memory) {
        /* Try fast internal SRAM first, fall back to PSRAM */
        p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!p) {
        /* Always succeeds via PSRAM on ESP32-S3-N16R8 */
        p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (p) memset(p, 0, size);
    return p;
}

void *nes_mem_calloc(size_t nmemb, size_t size) {
    return mem_alloc(nmemb * size, false);
}

void *nes_mem_malloc(size_t size) {
    return mem_alloc(size, false);
}

void nes_mem_free(void *ptr) {
    if (ptr) {
        heap_caps_free(ptr);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * vid_drv API — called by vid_drv.c / nofrendo.c
 * ═══════════════════════════════════════════════════════════════════════════*/

int vid_init(int width, int height, viddriver_t *driver)
{
    (void)width; (void)height; (void)driver;
    ESP_LOGI(TAG_OSD, "vid_init: creating 256x240 bitmap. PSRAM free: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (!s_bitmap) {
        s_bitmap = bmp_create(256, 240, 0);  /* 8-bit paletted */
    }
    if (!s_bitmap || !s_bitmap->data) {
        ESP_LOGE(TAG_OSD, "vid_init: bmp_create FAILED! PSRAM free: %u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return -1;
    }
    ESP_LOGI(TAG_OSD, "vid_init: bitmap OK @ %p", (void*)s_bitmap);
    return 0;
}

void vid_shutdown(void)
{
    if (s_bitmap) { bmp_destroy(&s_bitmap); s_bitmap = NULL; }
}

int  vid_setmode(int w, int h)    { (void)w; (void)h; return 0; }
void vid_setpalette(rgb_t *pal)   { osd_setpalette(pal); }

bitmap_t *vid_getbuffer(void)
{
    if (!s_bitmap) s_bitmap = bmp_create(256, 240, 0);
    return s_bitmap;
}

/*
 * vid_flush — called by Nofrendo once per emulated frame (~60 Hz).
 *
 * Performs fixed-point nearest-neighbour downscale:
 *   256×240 NES frame  →  160×128 display frame
 * X ratio: 256/160 = 1.6  → fixed-point step: (256<<8)/160 = 409
 * Y ratio: 240/128 = 1.875 → fixed-point step: (240<<8)/128 = 480
 *
 * Result is written into nes_frame_buffer (back-buffer).
 * A semaphore give signals App_NES::render() to swap buffers.
 */
void IRAM_ATTR vid_flush(void)
{
    /* Check exit flag every frame */
    if (nes_should_exit) {
        ESP_LOGI(TAG_OSD, "vid_flush: nes_should_exit set — calling main_quit");
        main_quit();
        return;
    }

    if (nes_state_cmd) {
        if (nes_state_cmd == 1) {
            ESP_LOGI(TAG_OSD, "Saving state...");
            state_save();
        } else if (nes_state_cmd == 2) {
            ESP_LOGI(TAG_OSD, "Loading state...");
            state_load();
        }
        nes_state_cmd = 0;
    }

    if (!s_bitmap || !nes_frame_buffer) {
        ESP_LOGW(TAG_OSD, "vid_flush: s_bitmap=%p nes_frame_buffer=%p — skipping frame",
                 (void*)s_bitmap, (void*)nes_frame_buffer);
        return;
    }

    uint16_t *dst   = nes_frame_buffer;
    uint32_t  y_acc = 0;

    for (int y = 0; y < 128; y++) {
        uint32_t src_y = y_acc >> 8;
        if (src_y > 239) src_y = 239;
        const uint8_t *src_row = s_bitmap->line[src_y];

        uint32_t x_acc = 0;
        for (int x = 0; x < 160; x++) {
            uint32_t src_x = x_acc >> 8;
            if (src_x > 255) src_x = 255;
            *dst++ = s_palette565[src_row[src_x]];
            x_acc += 409;
        }
        y_acc += 480;
    }

    if (nes_frame_sem) {
        xSemaphoreGive(nes_frame_sem);
    }

    vTaskDelay(pdMS_TO_TICKS(1));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * osd_main — entry point called by nofrendo_main() (which App_NES invokes)
 * ═══════════════════════════════════════════════════════════════════════════*/
int osd_main(int argc, char *argv[])
{
    (void)argc;

    /* argv[1] is the ROM path passed by App_NES _emu_task_func.
       We also have active_nes_rom_path set directly, but honour argv[1]
       if provided (they should match). */
    if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
        strncpy(active_nes_rom_path, argv[1], sizeof(active_nes_rom_path) - 1);
    }

    ESP_LOGI(TAG_OSD, "osd_main: starting ROM %s", active_nes_rom_path);

    return main_loop(active_nes_rom_path, system_autodetect);
}

/* ── Nofrendo Logging Stubs ────────────────────────────────────────────────── */

int nofrendo_log_init(void) { return 0; }
void nofrendo_log_shutdown(void) {}

int nofrendo_log_print(const char *string) {
    ESP_LOGI(TAG_OSD, "%s", string);
    return 0;
}

int nofrendo_log_printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    char buf[256];
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    ESP_LOGI(TAG_OSD, "%s", buf);
    return 0;
}

void nofrendo_log_chain_logfunc(int (*logfunc)(const char *string)) { (void)logfunc; }

void nofrendo_log_assert(int expr, int line, const char *file, char *msg) {
    if (!expr) {
        ESP_LOGE(TAG_OSD, "ASSERT %s:%d - %s", file, line, msg ? msg : "unknown");
        abort();
    }
}

