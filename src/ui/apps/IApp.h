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

    /**
     * @brief Returns true if this instance was heap-allocated and MUST be
     *        deleted by switch_to_app() after the transition completes.
     *
     *        All static singleton apps (App_Grid, App_OTA, App_Settings, …)
     *        inherit the default 'false'.
     *
     *        App_NES overrides to 'true' — every game launch allocates a
     *        fresh App_NES on the heap inside App_GameStore::_launch_game(),
     *        and switch_to_app() is the only safe point to delete it (after
     *        exit() has run and the new app's init() is in progress).
     */
    virtual bool isDynamic() const { return false; }
};
