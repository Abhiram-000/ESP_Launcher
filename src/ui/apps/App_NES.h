#pragma once

#include "IApp.h"
#include <LovyanGFX.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class App_NES : public IApp {
public:
    App_NES();
    ~App_NES();

    void init()    override;
    void exit()    override;
    void loop()    override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

    // Always heap-allocated by App_GameStore — must be deleted after transition.
    bool isDynamic() const override { return true; }

    void setRomPath(const char* path);

    // Called by loop() when the emulator task exits (clean finish, error, or user quit).
    // Wire this up before calling init() so the caller can switch apps cleanly.
    void (*onEmulatorExit)(void) = nullptr;

    // Returns true once the emulator task has finished for any reason.
    bool isEmulatorDone() const { return _emu_task_done; }

    // Public so the static _emu_task_func (which has no 'this' via C linkage) can set it.
    volatile bool _emu_task_done;

private:
    char              _rom_path[256];
    uint16_t*         _front_buffer;   // Displayed by UI task (Core 1)
    uint16_t*         _back_buffer;    // Written by emulator (Core 0)
    TaskHandle_t      _emu_task;
    SemaphoreHandle_t _buffer_mutex;

    // Input state
    uint32_t   _nes_buttons;
    bool       _select_held;
    TickType_t _select_held_since;

    // Exit-confirmation overlay
    bool       _exit_pending;
    TickType_t _exit_prompt_shown_at;
};
