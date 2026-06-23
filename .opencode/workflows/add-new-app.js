export const meta = {
  name: "add-new-app",
  description: "Autonomous new app creation: study patterns → web research → create → register → review → build"
};

export default async function(args, { agent, parallel, phase }) {
  const { app_name, description: app_desc, service_dependencies } = args;

  phase("1. Study Existing Patterns");

  const patterns = await agent({
    subagent_type: "explore",
    description: "Study app patterns deeply",
    prompt: `You are an ESP32-S3 app architecture researcher. Study the existing app patterns in this project THOROUGHLY.

Read these files COMPLETELY (every line):

1. src/ui/apps/IApp.h — interface contract (all virtual methods)
2. src/ui/apps/App_Torch.cpp — simplest app (no service dependency)
3. src/ui/apps/App_Pomodoro.cpp — app with service dependency
4. src/ui/apps/App_ClockWeather.cpp — app with async data loading
5. src/ui/apps/App_AICode.cpp — complex app with multiple states
6. src/ui/ui_task.cpp — how apps are registered and dispatched
7. src/ui/theme_mgr.h — theme color function signatures
8. src/managers/input_mgr.h — input_event_t definition

For each app, document:
- Complete class declaration (all members, all methods)
- How init() sets up state
- How exit() cleans up
- How loop() polls services and updates animation
- How render() handles ALL four states (loading/error/empty/normal)
- How handleInput() maps buttons to actions
- How service status is copied under mutex
- How _anim_time is used for animations
- How layout uses canvas->width()/height()

Also document:
- How apps are registered in ui_task.cpp (exact code pattern)
- How icons are loaded (64x64 PNG pattern)
- How switch_to_grid() is called
- What include paths are used

Return COMPLETE findings — this will be used to generate a new app.`
  });

  phase("2. Web Research");

  const webResearch = await agent({
    subagent_type: "general",
    description: "Research ESP32 patterns",
    prompt: `Research best practices for the type of app being created:

APP: ${app_name}
DESCRIPTION: ${app_desc}

Do EXTENSIVE web research:

1. Search: "ESP32-S3 ${app_desc} example ESP-IDF"
2. Search: "ESP32 ${app_desc} FreeRTOS implementation"
3. Search: "ESP-IDF v5 ${app_desc} tutorial 2024"
4. Search: "ESP32-S3 ${app_desc} best practices production"
5. Search: "embedded C ${app_desc} common patterns"

For each search, cite:
- Query used
- URL
- Key finding
- How it applies to this app

Also search for:
- Common bugs with this type of feature on ESP32
- Memory considerations
- Power consumption implications
- Security considerations (if applicable)`
  });

  phase("3. Create App Files");

  const created = await agent({
    subagent_type: "general",
    description: "Create complete app files",
    prompt: `You are an elite ESP32-S3 app developer. Create a COMPLETE, production-grade app.

APP NAME: ${app_name}
DESCRIPTION: ${app_desc}
SERVICE DEPENDENCIES: ${service_dependencies || "None"}

Based on these patterns:
${patterns}

And this web research:
${webResearch}

## Create These Files

### File 1: src/ui/apps/App_${app_name}.h

Complete header with:
- #pragma once
- Include IApp.h and any needed headers
- Class declaration with ALL member variables
- ALL virtual method declarations
- Any helper structs/enums needed

### File 2: src/ui/apps/App_${app_name}.cpp

Complete implementation with:

```cpp
#include "App_${app_name}.h"
#include "ui/theme_mgr.h"
// ... other includes

static const char* TAG = "APP_${app_name.toUpperCase()}";

void App_${app_name}::init() {
    // Initialize ALL member variables
    _anim_time = 0.0f;
    _sel_idx = 0;
    _scroll = 0;
    _is_loading = false;
    _has_error = false;
    _flash_frames = 0;
    _status[0] = '\\0';
}

void App_${app_name}::exit() {
    // Cleanup if needed
}

void App_${app_name}::loop() {
    _anim_time += 0.025f;  // Frame-rate independent

    if (_is_loading) {
        // Poll service status under mutex
        service_status_t st;
        service_get_status(&st);
        _is_loading = st.is_busy;
        _has_error = (st.error_code != 0);
        strlcpy(_status, st.status_msg, sizeof(_status));
    }
}

void App_${app_name}::handleInput(input_event_t event) {
    switch (event) {
        case BTN_LEFT:
            switch_to_grid();  // MANDATORY
            return;
        case BTN_UP:
            if (_sel_idx > 0) { _sel_idx--; _flash_frames = 3; }
            break;
        case BTN_DOWN:
            _sel_idx++; _flash_frames = 3;
            break;
        case BTN_SELECT:
            if (!_is_loading) {
                _is_loading = true;
                _anim_time = 0.0f;
                _flash_frames = 3;
                service_start("param");
            }
            break;
        default: break;
    }
}

void App_${app_name}::render(LGFX_Sprite* canvas) {
    canvas->fillSprite(theme_get_bg());  // ALWAYS FIRST

    if (_flash_frames > 0) {
        _flash_frames--;
        canvas->fillSprite(theme_get_accent());
        return;
    }

    const int32_t W = canvas->width();
    const int32_t H = canvas->height();
    const int32_t CX = W / 2;
    const int32_t CY = H / 2;
    const int32_t MARGIN = W / 20;
    const int32_t HEADER_H = H / 9;
    const int32_t FOOTER_Y = H - 3;

    // Loading state
    if (_is_loading) {
        // spinner animation
        for (int i = 0; i < 8; i++) {
            float a = _anim_time * 4.0f + i * 0.785f;
            canvas->fillCircle(
                CX + (int)(cosf(a) * 10),
                CY + (int)(sinf(a) * 10),
                2, theme_get_accent());
        }
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString(_status[0] ? _status : "Loading...", CX, CY + 18);
        return;
    }

    // Error state
    if (_has_error) {
        canvas->setTextColor(theme_get_accent());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("Error", CX, CY - 8);
        canvas->setTextColor(theme_get_text_dim());
        canvas->drawString(_status, CX, CY + 4);
        canvas->drawString("SEL=Retry  L=Back", CX, CY + 16);
        return;
    }

    // Empty state
    if (_item_count == 0) {
        canvas->setTextColor(theme_get_text_dim());
        canvas->setTextDatum(textdatum_t::middle_center);
        canvas->drawString("Nothing here yet.", CX, CY);
        return;
    }

    // Normal state
    // ... complete implementation
}
```

## Requirements
- Every function COMPLETE — no TODOs, no placeholders
- Every ESP-IDF return value checked
- PSRAM for allocations >1KB
- All colors via theme_get_*()
- All layout from canvas dimensions
- Error handling for all failure modes
- strlcpy/snprintf with sizeof

Write the COMPLETE files. No omissions.`
  });

  phase("4. Register & Build");

  const registered = await agent({
    subagent_type: "general",
    description: "Register app and build",
    prompt: `Register the new ${app_name} app in the ESP32-S3 Launcher project and verify it compiles.

## Step 1: Register in ui_task.cpp

Read src/ui/ui_task.cpp to understand the registration pattern.
Then add:
1. #include for the new app header
2. App instance creation
3. Registration in the app list/array

## Step 2: Build

Run: pio run

If build fails:
- Read the error message
- Fix the issue
- Run pio run again
- Repeat until build passes

## Step 3: Verify Standards

Check:
- [ ] No new TODOs added
- [ ] All files <1000 lines
- [ ] No hardcoded pins/pixels/colors
- [ ] Naming conventions followed
- [ ] All includes correct

## Output Format

### Files Modified
- [file] — [what changed]

### Build Result
- Status: PASS / FAIL
- Warnings: [list]
- Errors: [list, if any]

### Icon Needed
- Name: ${app_name}.png
- Size: 64x64
- Description: [what the icon should depict]
- Run: python3 convert_icons.py icons/ src/assets/icons/

### Hardware Verification
- [ ] [What to test on device]`
  });

  return { patterns, webResearch, created, registered };
}
