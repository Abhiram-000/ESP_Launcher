#include "dirty_rect.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG = "DIRTY_RECT";

static inline int16_t min_i16(int16_t a, int16_t b) { return a < b ? a : b; }
static inline int16_t max_i16(int16_t a, int16_t b) { return a > b ? a : b; }

static bool rects_overlap(const dirty_rect_t* a, const dirty_rect_t* b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x ||
             a->y + a->h <= b->y || b->y + b->h <= a->y);
}

static void merge_rects(dirty_rect_t* dst, const dirty_rect_t* a, const dirty_rect_t* b) {
    int16_t x1 = min_i16(a->x, b->x);
    int16_t y1 = min_i16(a->y, b->y);
    int16_t x2 = max_i16(a->x + a->w, b->x + b->w);
    int16_t y2 = max_i16(a->y + a->h, b->y + b->h);
    dst->x = x1;
    dst->y = y1;
    dst->w = x2 - x1;
    dst->h = y2 - y1;
}

void dirty_rect_init(dirty_rect_manager_t* mgr) {
    memset(mgr, 0, sizeof(dirty_rect_manager_t));
}

void dirty_rect_mark_dirty(dirty_rect_manager_t* mgr, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (mgr->full_redraw) return;
    
    if (w <= 0 || h <= 0) return;
    
    if (mgr->count >= DIRTY_RECT_MAX) {
        mgr->full_redraw = true;
        return;
    }
    
    mgr->rects[mgr->count].x = x;
    mgr->rects[mgr->count].y = y;
    mgr->rects[mgr->count].w = w;
    mgr->rects[mgr->count].h = h;
    mgr->count++;
}

void dirty_rect_mark_full(dirty_rect_manager_t* mgr) {
    mgr->full_redraw = true;
    mgr->count = 0;
}

void dirty_rect_clear(dirty_rect_manager_t* mgr) {
    mgr->count = 0;
    mgr->full_redraw = false;
}

bool dirty_rect_is_dirty(const dirty_rect_manager_t* mgr) {
    return mgr->full_redraw || mgr->count > 0;
}

bool dirty_rect_needs_full_redraw(const dirty_rect_manager_t* mgr) {
    return mgr->full_redraw;
}

uint8_t dirty_rect_get_count(const dirty_rect_manager_t* mgr) {
    return mgr->count;
}

const dirty_rect_t* dirty_rect_get(const dirty_rect_manager_t* mgr, uint8_t index) {
    if (index >= mgr->count) return NULL;
    return &mgr->rects[index];
}

void dirty_rect_optimize(dirty_rect_manager_t* mgr) {
    if (mgr->count <= 1) return;
    
    bool merged = true;
    while (merged && mgr->count > 1) {
        merged = false;
        for (uint8_t i = 0; i < mgr->count - 1; i++) {
            for (uint8_t j = i + 1; j < mgr->count; j++) {
                if (rects_overlap(&mgr->rects[i], &mgr->rects[j])) {
                    dirty_rect_t merged_rect;
                    merge_rects(&merged_rect, &mgr->rects[i], &mgr->rects[j]);
                    mgr->rects[i] = merged_rect;
                    
                    // Remove rect j by shifting
                    for (uint8_t k = j; k < mgr->count - 1; k++) {
                        mgr->rects[k] = mgr->rects[k + 1];
                    }
                    mgr->count--;
                    merged = true;
                    break;
                }
            }
            if (merged) break;
        }
    }
}
