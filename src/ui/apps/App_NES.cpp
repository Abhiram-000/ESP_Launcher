#include "App_NES.h"
#include "../../ui/theme_mgr.h"
#include "esp_log.h"
#include "sys_config.h"
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
    extern uint16_t*             nes_frame_buffer;
    extern SemaphoreHandle_t     nes_frame_sem;
    extern volatile uint32_t     nes_input_state;
    extern volatile bool         nes_should_exit;

    // Nofrendo input struct (defined by nesinput.c as an extern extern)
    typedef struct { int type; int data; } nesinput_t;
    extern nesinput_t joypad_p1;

    int osd_main(int argc, char* argv[]);
}

// Nofrendo JOYPAD0 type constant
#define INP_JOYPAD0 0x0001

// ─── Emulator task ────────────────────────────────────────────────────────
static void _emu_task_func(void* arg) {
    ESP_LOGI(TAG, "Emulator task on Core %d", xPortGetCoreID());

    char* argv[] = { (char*)"nofrendo", active_nes_rom_path, NULL };
    osd_main(2, argv);          // blocks until emulator finishes

    ESP_LOGI(TAG, "osd_main returned — emulator done");
    vTaskDelete(NULL);
}

// ─── Constructor / Destructor ─────────────────────────────────────────────
App_NES::App_NES()
    : _front_buffer(nullptr), _back_buffer(nullptr),
      _emu_task(nullptr), _buffer_mutex(nullptr),
      _nes_buttons(0), _select_held(false), _select_held_since(0),
      _exit_pending(false), _exit_prompt_shown_at(0)
{
    _rom_path[0] = '\0';
}

App_NES::~App_NES() {}

void App_NES::setRomPath(const char* path) {
    strncpy(_rom_path, path, sizeof(_rom_path) - 1);
    _rom_path[sizeof(_rom_path) - 1] = '\0';
}

// ─── IApp interface ───────────────────────────────────────────────────────
void App_NES::init() {
    ESP_LOGI(TAG, "init — ROM: %s", _rom_path);

    // Allocate double-buffer in PSRAM (160×128 × 2 bytes = 40 KB each)
    _front_buffer = (uint16_t*)heap_caps_malloc(160 * 128 * 2, MALLOC_CAP_SPIRAM);
    _back_buffer  = (uint16_t*)heap_caps_malloc(160 * 128 * 2, MALLOC_CAP_SPIRAM);

    if (!_front_buffer || !_back_buffer) {
        ESP_LOGE(TAG, "Framebuffer allocation failed!");
        return;
    }
    memset(_front_buffer, 0, 160 * 128 * 2);
    memset(_back_buffer,  0, 160 * 128 * 2);

    _buffer_mutex = xSemaphoreCreateMutex();

    // Set globals for OSD layer
    strncpy(active_nes_rom_path, _rom_path, 255);
    nes_frame_buffer = _back_buffer;
    nes_frame_sem    = _buffer_mutex;
    nes_input_state  = 0;
    nes_should_exit  = false;

    joypad_p1.type = INP_JOYPAD0;
    joypad_p1.data = 0;

    _nes_buttons        = 0;
    _select_held        = false;
    _exit_pending       = false;
    _exit_prompt_shown_at = 0;

    xTaskCreatePinnedToCore(
        _emu_task_func,
        "emu_task",
        NES_EMU_TASK_STACK,
        this,
        NES_EMU_TASK_PRIORITY,
        &_emu_task,
        NES_EMU_TASK_CORE
    );
}

void App_NES::exit() {
    ESP_LOGI(TAG, "exit — stopping emulator");
    nes_should_exit = true;

    // Give the emulator task ~150 ms to wrap up, then forcibly delete it
    if (_emu_task) {
        vTaskDelay(pdMS_TO_TICKS(150));
        eTaskState st = eTaskGetState(_emu_task);
        if (st != eDeleted) vTaskDelete(_emu_task);
        _emu_task = nullptr;
    }

    if (_buffer_mutex) { vSemaphoreDelete(_buffer_mutex); _buffer_mutex = nullptr; }
    if (_front_buffer) { heap_caps_free(_front_buffer);   _front_buffer = nullptr; }
    if (_back_buffer)  { heap_caps_free(_back_buffer);    _back_buffer  = nullptr; }

    nes_frame_buffer = nullptr;
    nes_frame_sem    = nullptr;
}

void App_NES::loop() {
    // If SELECT has been held for 2 s, arm the exit confirmation
    if (_select_held) {
        TickType_t held_for = xTaskGetTickCount() - _select_held_since;
        if (!_exit_pending && held_for > pdMS_TO_TICKS(2000)) {
            _exit_pending        = true;
            _exit_prompt_shown_at = xTaskGetTickCount();
        }
    }

    // Auto-confirm exit after 3 s on the overlay (no input needed once armed)
    if (_exit_pending) {
        TickType_t shown_for = xTaskGetTickCount() - _exit_prompt_shown_at;
        if (shown_for > pdMS_TO_TICKS(3000)) {
            // Trigger exit — the App_Grid switch must be called from ui_task
            // We set a flag; ui_task.cpp checks it via nes_should_exit
            nes_should_exit = true;
        }
    }
}

void App_NES::render(LGFX_Sprite* canvas) {
    if (!_buffer_mutex || !_front_buffer || !_back_buffer) return;

    // Try to swap buffers. Skip frame if emulator hasn't produced one yet.
    if (xSemaphoreTake(_buffer_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        uint16_t* tmp  = _front_buffer;
        _front_buffer  = _back_buffer;
        _back_buffer   = tmp;
        nes_frame_buffer = _back_buffer;   // hand the empty back-buffer to OSD
        xSemaphoreGive(_buffer_mutex);
    }

    // Blit NES frame to canvas (160×128, top-left aligned)
    canvas->pushImage(0, 0, 160, 128, _front_buffer);

    // Exit-confirmation overlay
    if (_exit_pending) {
        canvas->fillRoundRect(20, 44, 120, 40, 6, canvas->color565(5, 5, 20));
        canvas->drawRoundRect(20, 44, 120, 40, 6, theme_get_accent());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(theme_get_text());
        canvas->drawString("HOLD: Exit Game", 80, 56);
        canvas->setTextColor(theme_get_text_dim());
        canvas->drawString("SELECT to cancel", 80, 68);
    }
}

void App_NES::handleInput(input_event_t event) {
    // ── D-Pad: always direct
    uint32_t nes = 0;
    if (event == BTN_UP)    nes |= INP_PAD_UP;
    if (event == BTN_DOWN)  nes |= INP_PAD_DOWN;
    if (event == BTN_LEFT)  nes |= INP_PAD_LEFT;
    if (event == BTN_RIGHT) nes |= INP_PAD_RIGHT;

    // ── SELECT: short press → NES A, hold → arm exit
    if (event == BTN_SELECT) {
        if (!_select_held) {
            _select_held       = true;
            _select_held_since = xTaskGetTickCount();
        }
        if (!_exit_pending) {
            nes |= INP_PAD_A;   // Short-press behavior while not yet held 2 s
        } else {
            // User pressed SELECT while exit overlay is showing → cancel exit
            _exit_pending = false;
            _select_held  = false;
            nes_should_exit = false;
        }
    } else {
        // Any other button released the SELECT hold
        _select_held = false;
    }

    // ── Shift layer: UP+SELECT → NES Start, DOWN+SELECT → NES Select
    if (_select_held && event == BTN_UP)   { nes = INP_PAD_START;  }
    if (_select_held && event == BTN_DOWN) { nes = INP_PAD_SELECT; }

    _nes_buttons     = nes;
    joypad_p1.data   = nes;
    nes_input_state  = nes;
}
