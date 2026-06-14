#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize and start the UI task. */
void ui_task_init(void);

/** Set TFT brightness (0-255) and persist to NVS. */
void ui_set_brightness(uint8_t val);

/** Get current TFT brightness (0-255). */
uint8_t ui_get_brightness(void);

/** Get current launcher theme (0=Carousel, 1=Tile, 2=Grid). */
uint8_t ui_get_launcher_theme(void);

/** Set launcher theme and persist to NVS. */
void ui_set_launcher_theme(uint8_t theme);

/**
 * Return to the launcher grid.
 *
 * All apps MUST use this declaration via #include "ui_task.h".
 * Never use a file-local extern — that bypasses the layer contract.
 */
void switch_to_grid(void);

#ifdef __cplusplus
}
#endif

/*
 * C++ only — requires IApp.h in scope.
 * Apps that call switch_to_app() must #include "IApp.h" before this header.
 */
#ifdef __cplusplus
class IApp;
void switch_to_app(IApp* app);
#endif
