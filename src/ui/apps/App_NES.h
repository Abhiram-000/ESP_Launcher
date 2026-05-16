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
    void loop()    override;                          // IApp pure virtual
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

    void setRomPath(const char* path);

private:
    char          _rom_path[256];
    uint16_t*     _front_buffer;   // Shown by UI task (Core 1)
    uint16_t*     _back_buffer;    // Written by emulator (Core 0)
    TaskHandle_t  _emu_task;
    SemaphoreHandle_t _buffer_mutex;

    // 5-button NES input state
    uint32_t      _nes_buttons;          // Accumulated NES button bits
    bool          _select_held;
    TickType_t    _select_held_since;

    // Exit confirmation overlay
    bool          _exit_pending;
    TickType_t    _exit_prompt_shown_at;
};
