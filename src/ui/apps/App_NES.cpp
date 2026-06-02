#include "App_NES.h"
#include "../../ui/theme_mgr.h"
#include "esp_log.h"
#include "sys_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "App_NES";

// ─── NES controller bit masks ──────────────────────────────────────────────
#define INP_PAD_A       0x01
#define INP_PAD_B       0x02
#define INP_PAD_SELECT  0x04
#define INP_PAD_START   0x08
#define INP_PAD_UP      0x10
#define INP_PAD_DOWN    0x20
#define INP_PAD_LEFT    0x40
#define INP_PAD_RIGHT   0x80

// ─── Globals shared with osd_esp32s3.c ────────────────────────────────────
extern "C" {
    extern char                  active_nes_rom_path[256];
    extern volatile uint16_t*         nes_frame_buffer;
    extern SemaphoreHandle_t     nes_frame_sem;
    extern volatile uint32_t     nes_input_state;
    extern volatile bool         nes_should_exit;
    extern volatile uint8_t      nes_state_cmd;

    typedef struct { int type; int data; } nesinput_t;
    extern nesinput_t joypad_p1;

    int osd_main(int argc, char* argv[]);
}

#define INP_JOYPAD0 0x0001

// ─── Mappers supported by this build of Nofrendo ─────────────────────────
// Keep in sync with mmclist.c and actual map*.c files in components/nofrendo/
static const int s_supported_mappers[] = {
    0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11,
    15, 16, 18, 19, 24,
    32, 33, 34, 40, 41, 42, 46, 50,
    64, 65, 66, 70, 73, 75, 78, 79,
    85, 87, 93, 94, 99,
    160, 229, 231,
    -1  // sentinel
};

static bool _is_mapper_supported(int mapper) {
    for (int i = 0; s_supported_mappers[i] != -1; i++) {
        if (s_supported_mappers[i] == mapper) return true;
    }
    return false;
}

// ─── Read iNES header mapper from ROM file ────────────────────────────────
static int _read_rom_mapper(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t header[16];
    if (fread(header, 1, 16, f) < 16) { fclose(f); return -1; }
    fclose(f);
    // iNES magic check: 'NES\x1A'
    if (header[0] != 0x4E || header[1] != 0x45 || header[2] != 0x53 || header[3] != 0x1A)
        return -1;
    int mapper = ((header[6] >> 4) & 0x0F) | (header[7] & 0xF0);
    return mapper;
}

// ─── Reset the boot-loop counter so a crash inside the emu doesn't
//     trigger the OTA splash after 3 reboots. ─────────────────────────────
static void _reset_boot_count(void) {
    nvs_handle_t h;
    if (nvs_open("boot_ctrl", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "boot_cnt", 0);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "boot_cnt reset before emulator launch");
    }
}

// ─── Emulator task ────────────────────────────────────────────────────────
static void _emu_task_func(void* arg) {
    App_NES* self = static_cast<App_NES*>(arg);
    ESP_LOGI(TAG, "Emulator task started on Core %d, stack: %u bytes",
             xPortGetCoreID(), NES_EMU_TASK_STACK);

    char* argv[] = { (char*)"nofrendo", active_nes_rom_path, nullptr };
    osd_main(2, argv);   // Blocks until emulator exits (normally or via nes_should_exit)

    ESP_LOGI(TAG, "osd_main returned — emulator done. Min free stack: %u bytes",
             uxTaskGetStackHighWaterMark(nullptr));

    // Signal the UI task that we are done
    self->_emu_task_done = true;
    vTaskDelete(nullptr);
}

// ─── Constructor / Destructor ─────────────────────────────────────────────
App_NES::App_NES()
    : _emu_task_done(false),
      _front_buffer(nullptr), _back_buffer(nullptr),
      _emu_task(nullptr), _buffer_mutex(nullptr),
      _nes_buttons(0), _select_held(false), _select_held_since(0),
      _exit_pending(false), _exit_prompt_shown_at(0)
{
    _rom_path[0] = '\0';
    onEmulatorExit = nullptr;
}

App_NES::~App_NES() {}

void App_NES::_cleanup_resources() {
    if (_buffer_mutex) { vSemaphoreDelete(_buffer_mutex); _buffer_mutex = nullptr; }
    if (_front_buffer) { heap_caps_free(_front_buffer);   _front_buffer = nullptr; }
    if (_back_buffer)  { heap_caps_free(_back_buffer);    _back_buffer  = nullptr; }

    nes_frame_buffer = nullptr;
    nes_frame_sem    = nullptr;
}

void App_NES::setRomPath(const char* path) {
    strncpy(_rom_path, path, sizeof(_rom_path) - 1);
    _rom_path[sizeof(_rom_path) - 1] = '\0';
}

// ─── IApp interface ───────────────────────────────────────────────────────
void App_NES::init() {
    ESP_LOGI(TAG, "init — ROM: %s", _rom_path);

    _emu_task_done = false;

    // ── Validate ROM mapper before allocating anything ─────────────────
    int mapper = _read_rom_mapper(_rom_path);
    if (mapper < 0) {
        ESP_LOGE(TAG, "Cannot read ROM header or invalid iNES file: %s", _rom_path);
        _emu_task_done = true;  // Signal immediate exit
        return;
    }
    if (!_is_mapper_supported(mapper)) {
        ESP_LOGE(TAG, "Unsupported mapper %d for ROM: %s", mapper, _rom_path);
        _emu_task_done = true;
        return;
    }
    ESP_LOGI(TAG, "ROM mapper %d — supported. Launching emulator.", mapper);

    // ── Allocate double-buffers in PSRAM (160x128 x 2 bytes = 40KB each) ─
    _front_buffer = (uint16_t*)heap_caps_malloc(160 * 128 * 2, MALLOC_CAP_SPIRAM);
    _back_buffer  = (uint16_t*)heap_caps_malloc(160 * 128 * 2, MALLOC_CAP_SPIRAM);

    if (!_front_buffer || !_back_buffer) {
        ESP_LOGE(TAG, "Framebuffer alloc failed! PSRAM free: %u",
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        _cleanup_resources();
        _emu_task_done = true;
        return;
    }
    memset(_front_buffer, 0, 160 * 128 * 2);
    memset(_back_buffer,  0, 160 * 128 * 2);

    _buffer_mutex = xSemaphoreCreateBinary();
    if (!_buffer_mutex) {
        ESP_LOGE(TAG, "Semaphore creation failed");
        _cleanup_resources();
        _emu_task_done = true;
        return;
    }
    /* Give once so the first render() can swap immediately (binary sem starts empty) */
    xSemaphoreGive(_buffer_mutex);

    // ── Set globals for OSD layer ────────────────────────────────────────
    strncpy(active_nes_rom_path, _rom_path, 255);
    nes_frame_buffer = (volatile uint16_t*)_back_buffer;
    nes_frame_sem    = _buffer_mutex;
    nes_input_state  = 0;
    nes_should_exit  = false;

    joypad_p1.type = INP_JOYPAD0;
    joypad_p1.data = 0;

    _nes_buttons          = 0;
    _select_held          = false;
    _exit_pending         = false;
    _exit_prompt_shown_at = 0;

    // ── Reset boot-loop counter so emulator crashes don't trigger OTA ───
    _reset_boot_count();

    // ── Spawn emulator on Core 0 ─────────────────────────────────────────
    BaseType_t ret = xTaskCreatePinnedToCore(
        _emu_task_func,
        "emu_task",
        NES_EMU_TASK_STACK,
        this,                    // pass 'this' so task can set _emu_task_done
        NES_EMU_TASK_PRIORITY,
        &_emu_task,
        NES_EMU_TASK_CORE
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create emulator task!");
        _cleanup_resources();
        _emu_task_done = true;
    }
}

void App_NES::exit() {
    ESP_LOGI(TAG, "exit — stopping emulator");
    nes_should_exit = true;

    // Give emulator up to 1s to wrap up (it self-deletes via vTaskDelete(NULL))
    if (_emu_task) {
        for (int i = 0; i < 100; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
            if (_emu_task_done) break;
        }
        if (!_emu_task_done) {
            ESP_LOGW(TAG, "Emulator task did not exit in time");
        }
        _emu_task = nullptr;
    }

    _cleanup_resources();
}

void App_NES::loop() {
    // ── Detect emulator task finishing naturally ─────────────────────────
    // If the task has exited on its own (ROM finished, unsupported mapper,
    // or OSD returned), fire the exit callback and let caller switch app.
    if (_emu_task_done && onEmulatorExit) {
        // Clear the flag first so we don't fire twice
        _emu_task_done = false;
        // Clean up resources before handing control back
        _cleanup_resources();
        _emu_task        = nullptr;
        onEmulatorExit();   // <-- switches app (called on UI task / Core 1)
        return;
    }

    // ── SELECT held for 2 s arms the exit confirmation overlay ──────────
    if (_select_held) {
        TickType_t held_for = xTaskGetTickCount() - _select_held_since;
        if (!_exit_pending && held_for > pdMS_TO_TICKS(2000)) {
            _exit_pending         = true;
            _exit_prompt_shown_at = xTaskGetTickCount();
        }
    }

    // ── Auto-confirm exit after 3 s on the overlay ───────────────────────
    if (_exit_pending) {
        TickType_t shown_for = xTaskGetTickCount() - _exit_prompt_shown_at;
        if (shown_for > pdMS_TO_TICKS(3000)) {
            nes_should_exit = true;
        }
    }
}

void App_NES::render(LGFX_Sprite* canvas) {
    // If init() failed (bad mapper, OOM), show an error and return
    if (!_buffer_mutex || !_front_buffer || !_back_buffer) {
        canvas->fillScreen(canvas->color565(10, 0, 0));
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(canvas->color565(220, 60, 60));
        canvas->drawString("ROM unsupported or", 80, 52);
        canvas->drawString("failed to load.", 80, 64);
        canvas->setTextColor(canvas->color565(100, 100, 100));
        canvas->drawString("Press any key", 80, 84);
        return;
    }

    // ── Swap buffers — try to get new frame, skip if emulator is slow ───
    if (xSemaphoreTake(_buffer_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        uint16_t* tmp  = _front_buffer;
        _front_buffer  = _back_buffer;
        _back_buffer   = tmp;
        nes_frame_buffer = (volatile uint16_t*)_back_buffer;
        xSemaphoreGive(_buffer_mutex);
    }

    canvas->pushImage(0, 0, 160, 128, _front_buffer);

    // ── Exit-confirmation overlay ─────────────────────────────────────────
    if (_exit_pending) {
        canvas->fillRoundRect(20, 44, 120, 40, 6, canvas->color565(5, 5, 20));
        canvas->drawRoundRect(20, 44, 120, 40, 6, theme_get_accent());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(theme_get_text());
        canvas->drawString("Hold: Exit Game", 80, 56);
        canvas->setTextColor(theme_get_text_dim());
        canvas->drawString("SELECT to cancel", 80, 68);
    }
}

void App_NES::handleInput(input_event_t event) {
    // If ROM failed to load, any button returns to previous app
    if (_emu_task_done && onEmulatorExit) {
        onEmulatorExit();
        return;
    }

    uint32_t nes = 0;

    // ── D-Pad (always pass through unless SELECT chord overrides below) ───
    if (event == BTN_UP)    nes |= INP_PAD_UP;
    if (event == BTN_DOWN)  nes |= INP_PAD_DOWN;
    if (event == BTN_LEFT)  nes |= INP_PAD_LEFT;
    if (event == BTN_RIGHT) nes |= INP_PAD_RIGHT;

    // ── SELECT: smart tap vs. chord detection ─────────────────────────────
    // A QUICK TAP  (SELECT released < 200 ms after press) → A button only.
    // CHORD MODE   (SELECT held ≥ 200 ms, then another key pressed)
    //              → START / SELECT-NES / Save / Load via combos.
    // This stops accidental chord activation during fast button presses.
    if (event == BTN_SELECT) {
        if (!_select_held) {
            _select_held       = true;
            _select_held_since = xTaskGetTickCount();
        }

        if (_exit_pending) {
            // Any SELECT press during exit-overlay cancels the exit
            _exit_pending   = false;
            _select_held    = false;
            nes_should_exit = false;
        } else {
            // Always deliver A on the press frame so games feel responsive;
            // chord override happens only when a SECOND key arrives while held
            nes |= INP_PAD_A;
        }

    } else if (_select_held) {
        // Another key arrived while SELECT is still held → chord mode.
        // Only activate chord if SELECT has been down ≥ 200 ms (intentional).
        TickType_t held_ms = (xTaskGetTickCount() - _select_held_since) * portTICK_PERIOD_MS;
        if (held_ms >= 200) {
            // Override: cancel the A-button, deliver the mapped chord function
            nes &= ~INP_PAD_A;
            if (event == BTN_UP)    { nes  = INP_PAD_START;  }   // NES Start
            if (event == BTN_DOWN)  { nes  = INP_PAD_SELECT; }   // NES Select
            if (event == BTN_LEFT)  { nes_state_cmd = 1; nes = 0; } // Save state
            if (event == BTN_RIGHT) { nes_state_cmd = 2; nes = 0; } // Load state
        }
        // If held < 200 ms, treat as independent A + D-pad press (normal play)
    } else {
        // SELECT not involved — ensure held flag stays clear
        _select_held = false;
    }

    _nes_buttons    = nes;
    joypad_p1.data  = (int)nes;
    nes_input_state = nes;
}
