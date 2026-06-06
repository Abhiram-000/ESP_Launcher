import re

with open('src/ui/apps/App_Grid.cpp', 'r') as f:
    content = f.read()

# 1. init() remove _icon_bounce
content = content.replace("    _icon_bounce = 0.0f;\n", "")

# 2. loop() remove _icon_bounce
content = re.sub(r"    // Float animation for the selected icon\n    _icon_bounce = sinf\(anim_time \* 2\.5f\) \* 4\.0f;\n", "", content)

# 3. render() remove _icon_bounce usage
content = content.replace("            y += _icon_bounce;\n", "")

# 4. render() replace abs_dist logic
# Replace the sprite creation and deletion
sprite_creation = """    if (!_sp64) {
        _sp64 = new LGFX_Sprite(canvas);
        _sp64->setPsram(true);
        _sp64->createSprite(64, 64);
    }
    if (!_sp32) {
        _sp32 = new LGFX_Sprite(canvas);
        _sp32->setPsram(true);
        _sp32->createSprite(32, 32);
    }

    auto apply_corners = [&](LGFX_Sprite* sp, int w, int h, int r, uint16_t color) {
        for (int y=0; y<r; y++) {
            for (int x=0; x<r; x++) {
                if ((r-x)*(r-x) + (r-y)*(r-y) >= r*r) {
                    sp->drawPixel(x, y, color);
                    sp->drawPixel(w-1-x, y, color);
                    sp->drawPixel(x, h-1-y, color);
                    sp->drawPixel(w-1-x, h-1-y, color);
                }
            }
        }
    };"""

content = re.sub(
    r"    LGFX_Sprite sp64\(canvas\);\n    sp64\.setPsram\(false\);\n    bool has_sp64 = sp64\.createSprite\(64, 64\);\n\n    LGFX_Sprite sp32\(canvas\);\n    sp32\.setPsram\(false\);\n    bool has_sp32 = sp32\.createSprite\(32, 32\);\n",
    sprite_creation + "\n",
    content
)

# 5. Fix the selected item rendering
sel_render_old = """            if (apps[idx].icon_rgb565) {
                float scale = 0.75f + (1.0f - abs_dist*2.0f) * 0.1f;
                
                if (has_sp64) {
                    sp64.pushImage(0, 0, 64, 64, apps[idx].icon_rgb565);
                    sp64.pushRotateZoom(icon_cx, y, 0, scale, scale);
                } else if (apps[idx].cached_icon32) {
                    canvas->pushImage(icon_cx - 16, y - 16, 32, 32, apps[idx].cached_icon32);
                }
            }"""

sel_render_new = """            if (apps[idx].icon_rgb565) {
                float scale = 0.75f + (1.0f - abs_dist*2.0f) * 0.1f;
                if (_sp64) {
                    _sp64->pushImage(0, 0, 64, 64, apps[idx].icon_rgb565);
                    apply_corners(_sp64, 64, 64, 12, blend_with_bg(bg, 190));
                    _sp64->pushRotateZoom(icon_cx, y, 0, scale, scale);
                } else if (apps[idx].cached_icon32) {
                    canvas->pushImage(icon_cx - 16, y - 16, 32, 32, apps[idx].cached_icon32);
                }
            }"""

content = content.replace(sel_render_old, sel_render_new)

# 6. Fix the unselected item rendering
unsel_render_old = """            if (apps[idx].cached_icon32) {
                 float scale = 1.0f - abs_dist * 0.15f;
                 if (scale < 0.5f) scale = 0.5f;
                 
                 if (has_sp32) {
                     sp32.pushImage(0, 0, 32, 32, apps[idx].cached_icon32);
                     sp32.pushRotateZoom(icon_cx, y, 0, scale, scale);
                 } else {
                     canvas->pushImage(icon_cx - 16, y - 16, 32, 32, apps[idx].cached_icon32);
                 }
            }"""

unsel_render_new = """            if (apps[idx].cached_icon32) {
                 float scale = 1.0f - abs_dist * 0.15f;
                 if (scale < 0.5f) scale = 0.5f;
                 
                 if (_sp32) {
                     _sp32->pushImage(0, 0, 32, 32, apps[idx].cached_icon32);
                     apply_corners(_sp32, 32, 32, 6, bg); // Use pure background for unselected corners over wallpaper
                     _sp32->pushRotateZoom(icon_cx, y, 0, scale, scale);
                 } else {
                     canvas->pushImage(icon_cx - 16, y - 16, 32, 32, apps[idx].cached_icon32);
                 }
            }"""

content = content.replace(unsel_render_old, unsel_render_new)

# 7. Remove deleteSprite at end
content = re.sub(r"    if \(has_sp64\) sp64\.deleteSprite\(\);\n    if \(has_sp32\) sp32\.deleteSprite\(\);\n", "", content)

# 8. Change selected item logic to strictly use abs_dist
content = content.replace("if (abs_dist < 0.5f && idx == selected_index)", "if (abs_dist <= 0.5f)")

with open('src/ui/apps/App_Grid.cpp', 'w') as f:
    f.write(content)
