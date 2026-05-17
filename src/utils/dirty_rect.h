#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIRTY_RECT_MAX 8

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} dirty_rect_t;

typedef struct {
    dirty_rect_t rects[DIRTY_RECT_MAX];
    uint8_t count;
    bool full_redraw;
} dirty_rect_manager_t;

void dirty_rect_init(dirty_rect_manager_t* mgr);
void dirty_rect_mark_dirty(dirty_rect_manager_t* mgr, int16_t x, int16_t y, int16_t w, int16_t h);
void dirty_rect_mark_full(dirty_rect_manager_t* mgr);
void dirty_rect_clear(dirty_rect_manager_t* mgr);
bool dirty_rect_is_dirty(const dirty_rect_manager_t* mgr);
bool dirty_rect_needs_full_redraw(const dirty_rect_manager_t* mgr);
uint8_t dirty_rect_get_count(const dirty_rect_manager_t* mgr);
const dirty_rect_t* dirty_rect_get(const dirty_rect_manager_t* mgr, uint8_t index);

// Merge overlapping rectangles to reduce draw calls
void dirty_rect_optimize(dirty_rect_manager_t* mgr);

#ifdef __cplusplus
}
#endif
