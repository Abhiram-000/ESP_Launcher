#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    THEME_DARK_CYBERPUNK = 0,
    THEME_LIGHT_MINIMAL,
    THEME_HACKER_MATRIX,
    THEME_RETRO_OUTRUN,
    THEME_DEEP_SPACE,
    THEME_COUNT
} theme_type_t;

void theme_init(void);
void theme_set(theme_type_t theme);
theme_type_t theme_get_current(void);
const char* theme_get_name(theme_type_t theme);

// Returns smoothly interpolated colors matching current theme
uint16_t theme_get_bg(void);
uint16_t theme_get_accent(void);
uint16_t theme_get_text(void);
uint16_t theme_get_text_dim(void);
uint16_t theme_get_success(void);
uint16_t theme_get_fail(void);

// Call periodically (e.g. 40fps in ui_task) to animate theme switching
void theme_update(void);

#ifdef __cplusplus
}
#endif
