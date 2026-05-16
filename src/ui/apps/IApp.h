#pragma once

#include "../../managers/input_mgr.h"
#include "../../include/LGFX_Config.h"

class IApp {
public:
    virtual ~IApp() {}

    /**
     * @brief Called when the app is brought to the foreground.
     */
    virtual void init() = 0;

    /**
     * @brief Called when the app is backgrounded.
     */
    virtual void exit() = 0;

    /**
     * @brief Non-blocking loop called every frame before rendering.
     */
    virtual void loop() = 0;

    /**
     * @brief Renders the app onto the provided canvas.
     */
    virtual void render(LGFX_Sprite* canvas) = 0;

    /**
     * @brief Handle user input cleanly.
     */
    virtual void handleInput(input_event_t event) = 0;
};
