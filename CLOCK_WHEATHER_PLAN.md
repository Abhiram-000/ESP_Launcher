# Clock & Weather App — Complete Implementation Plan

## 1. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                     App_ClockWeather (Core 1)                     │
│                                                                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │ Clock Screen  │  │Settings Screen│  │  UI_Keyboard overlay │   │
│  │ (default)     │  │ (SEL→open)   │  │  (city name entry)   │   │
│  └──────┬───────┘  └──────┬───────┘  └──────────┬───────────┘   │
│         │                  │                      │                │
│         └──────────────────┴──────────────────────┘                │
│                            │                                       │
│                     polls get_status()                             │
└────────────────────────────┬─────────────────────────────────────┘
                             │
          ┌──────────────────┴──────────────────┐
          │                                       │
          ▼                                       ▼
┌─────────────────────┐              ┌────────────────────────────┐
│    ntp_service       │              │     weather_service         │
│    (Core 0)          │              │     (Core 0)                │
│                      │              │                              │
│  esp_sntp (built-in) │              │  ip-api.com (HTTP, no key)  │
│  settimeofday()      │              │  Open-Meteo (HTTPS, no key) │
│  time() / localtime  │              │  cJSON parse                │
│  NVS: save/restore   │              │  LittleFS: config cache     │
│  epoch + timer_us    │              │  NVS: last known time       │
└─────────────────────┘              └────────────────────────────┘
```

---

## 2. Time Accuracy Strategy (Three-Layer Approach)

### Layer 1: NTP Sync (Primary — Exact)
- On boot, if Wi-Fi connected → SNTP sync from `pool.ntp.org`
- `settimeofday()` sets the system clock
- Re-sync every 3600 seconds (1 hour)
- **Accuracy:** Exact to the second

### Layer 2: NVS Persistence + esp_timer Delta (Secondary — ~1 min accuracy)
- Every 60 seconds, save to NVS:
  - `epoch_seconds` (current wall-clock time)
  - `timer_us` (current `esp_timer_get_time()` value)
- On boot, before NTP:
  - Load saved `epoch_seconds` and `timer_us`
  - Compute `delta_us = esp_timer_get_time() - saved_timer_us`
  - Restore: `restored_time = saved_epoch + (delta_us / 1000000)`
  - Call `settimeofday()` with restored time
- **Accuracy:** Within ~1 minute after days offline (drift from `esp_timer` is minimal — it uses LP system timer, not the RC oscillator)

### Layer 3: NTP Re-sync (Continuous Correction)
- After Layer 2 restores approximate time, NTP refines it to exact
- If Wi-Fi is offline, Layer 2 time is used as-is
- **Accuracy:** Exact when online, ~1 min when offline

### Time Data Flow
```
Boot
  │
  ├─ Wi-Fi connected?
  │    ├─ YES → NTP sync (2-3 sec) → exact time
  │    └─ NO  → Load NVS (epoch + timer_us) → compute delta → restore time
  │
  ├─ Every 60 sec → save epoch + esp_timer_get_time() to NVS
  │
  └─ Every 3600 sec → NTP re-sync (if connected)
```

---

## 3. Weather Accuracy Strategy

### Data Sources
| API | URL | Auth | HTTPS | Rate Limit |
|-----|-----|------|-------|------------|
| IP Geolocation | `http://ip-api.com/json/` | None | No (HTTP) | 45 req/min |
| Geocoding | `https://geocoding-api.open-meteo.com/v1/search` | None | Yes | Unlimited |
| Weather | `https://api.open-meteo.com/v1/forecast` | None | Yes | 10,000/day |

### Weather Fetch Strategy
- On boot, if Wi-Fi connected → fetch weather
- Refresh every 1800 seconds (30 min)
- Cache last successful response in LittleFS
- On boot without Wi-Fi → load cached data

### Location Detection Strategy
- On first boot → auto-detect from IP geolocation
- Save detected location to LittleFS
- User can override via "Change Location" → keyboard → geocoding API
- On subsequent boots → load cached location (instant)
- If Wi-Fi connected → re-detect in background (in case user moved)

---

## 4. Edge Cases — Complete Enumeration

### Time Edge Cases

| # | Edge Case | Handling |
|---|-----------|----------|
| T1 | **First boot ever (no NVS data)** | NTP sync → show correct time. If no Wi-Fi → show 00:00:00 with "No time" indicator |
| T2 | **Power loss mid-NVS-write** | NVS write takes ~10ms. If interrupted, next boot reads stale (but valid) previous save. Worst case: time is off by up to 60 seconds until NTP corrects |
| T3 | **NVS corruption** | NVS init erases and reinitializes on corruption (existing `main.cpp` pattern). Time restore fails → falls back to NTP or 00:00:00 |
| T4 | **Wi-Fi disconnects mid-NTP-sync** | `esp_sntp_sync_status()` check. If not synced, use Layer 2 restored time |
| T5 | **NTP server unreachable** | SNTP retries automatically. If all retries fail, use Layer 2 restored time |
| T6 | **User moves to timezone** | IP geolocation detects new timezone. NTP re-sync with new tz. If offline → cached tz used |
| T7 | **Daylight saving time** | Open-Meteo returns `timezone_abbreviation` (e.g., "IST", "CEST"). NTP handles DST via POSIX timezone strings |
| T8 | **Multiple rapid reboots** | NVS write every 60s. Rapid reboots within 60s → use last saved value. Accuracy within 60 seconds |
| T9 | **esp_timer overflow** | `esp_timer_get_time()` is 64-bit microsecond → overflow at ~292,000 years. Not a concern |
| T10 | **Clock shows wrong time after restore** | If restored time is >24 hours old or in future → discard, show 00:00:00 until NTP |

### Weather Edge Cases

| # | Edge Case | Handling |
|---|-----------|----------|
| W1 | **No Wi-Fi on boot** | Load cached weather from LittleFS. Show "Offline" badge. Weather may be stale |
| W2 | **IP geolocation fails** | Fall back to cached location. If no cache → show "Set location" prompt |
| W3 | **IP geolocation returns wrong city** | Show detected city. User can override via "Change Location" |
| W4 | **Geocoding API returns no results** | Show "City not found" error. Keep previous location |
| W5 | **Weather API returns error** | Show cached weather with "Stale" indicator. Log error |
| W6 | **JSON parse failure** | Validate all cJSON pointers. On parse error → keep cached data |
| W7 | **HTTP timeout (10s)** | Abort request. Use cached data |
| W8 | **Memory pressure during HTTP** | PSRAM buffers for HTTP. Check allocation returns |
| W9 | **Weather data is stale (>2 hours)** | Show "Updated X min ago" indicator. Auto-refresh when connected |
| W10 | **LittleFS write fails** | Weather still works from memory. Cache is best-effort |
| W11 | **City name > 64 chars** | UI_Keyboard buffer is 64 bytes. Truncate gracefully |
| W12 | **WMO weather code unknown** | Map known codes (0-99). Unknown codes → "Unknown" |
| W13 | **Temperature is negative** | Display with minus sign. Handle -40°C to +60°C range |
| W14 | **Wind speed is 0** | Display "Calm" or "0 km/h" |
| W15 | **Multiple rapid refresh requests** | Debounce: ignore if last refresh < 60 seconds ago |

---

## 5. File Specifications

### New Files (6)

#### 5.1 `src/services/ntp_service.h` (30 lines)
```c
#pragma once
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NTP_ERR_OK = 0,
    NTP_ERR_NOT_SYNCED,
    NTP_ERR_WIFI_NOT_CONNECTED,
} ntp_err_t;

// Initialize SNTP client. Call once from app_main().
ntp_err_t ntp_service_init(void);

// Force immediate NTP sync. Non-blocking, returns immediately.
// Status available via ntp_service_is_synced().
ntp_err_t ntp_service_sync(void);

// Check if NTP has successfully synced at least once.
bool ntp_service_is_synced(void);

// Get current wall-clock time. Works offline after first sync.
time_t ntp_service_get_time(void);

// Get local time struct (with timezone applied).
void ntp_service_get_local_time(struct tm* out);

// Save current time to NVS for persistence across reboots.
// Called internally every 60 seconds.
void ntp_service_save_to_nvs(void);

// Restore time from NVS on boot (before NTP sync).
// Returns true if time was restored, false if no saved time.
bool ntp_service_restore_from_nvs(void);

#ifdef __cplusplus
}
#endif
```

#### 5.2 `src/services/ntp_service.c` (180 lines)
Key implementation details:
- Uses `esp_sntp.h` (built into ESP-IDF v5.x)
- NVS namespace: `"clock_time"`, keys: `"epoch"` (u32), `"timer_us"` (u64)
- SNTP callback sets `s_ntp_synced` flag
- `ntp_service_save_to_nvs()`: saves `time(NULL)` + `esp_timer_get_time()` to NVS
- `ntp_service_restore_from_nvs()`: loads saved values, computes delta, calls `settimeofday()`
- Validates restored time: must be > year 2020 and < year 2035

#### 5.3 `src/services/weather_service.h` (80 lines)
```c
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float  temperature;      // °C
    float  wind_speed;       // km/h
    int    humidity;         // %
    int    weather_code;     // WMO code
    int    is_day;           // 1=day, 0=night
} weather_current_t;

typedef struct {
    char   city_name[32];
    float  latitude;
    float  longitude;
    char   timezone[64];
} weather_location_t;

typedef struct {
    bool                is_busy;
    bool                has_data;
    bool                is_stale;           // true if data > 2 hours old
    weather_current_t   current;
    weather_location_t  location;
    char                status_msg[64];
    int64_t             last_update_s;      // esp_timer timestamp
} weather_status_t;

typedef enum {
    WEATHER_ERR_OK = 0,
    WEATHER_ERR_NO_WIFI,
    WEATHER_ERR_NETWORK,
    WEATHER_ERR_NO_LOCATION,
    WEATHER_ERR_PARSE,
    WEATHER_ERR_BUSY,
} weather_err_t;

weather_err_t weather_service_init(void);
weather_err_t weather_service_refresh(void);
weather_err_t weather_service_set_location(const char* city_name, float lat, float lon, const char* tz);
weather_err_t weather_service_get_location(weather_location_t* out);
void          weather_service_get_status(weather_status_t* out);
bool          weather_service_is_busy(void);
const char*   weather_code_to_description(int code);
const char*   weather_code_to_icon(int code);

#ifdef __cplusplus
}
#endif
```

#### 5.4 `src/services/weather_service.c` (450 lines)
Key implementation details:
- Config file: `/storage/ai/clock_location.json` (atomic write via .tmp+rename)
- JSON format: `{"city":"Mahabubnagar","lat":16.74,"lon":77.98,"tz":"Asia/Kolkata"}`
- IP geolocation: HTTP GET to `http://ip-api.com/json/?fields=status,country,city,lat,lon,timezone`
- Weather fetch: HTTPS GET to `api.open-meteo.com/v1/forecast?lat=X&lon=Y&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m,is_day&timezone=auto`
- WMO code mapping: 0=Clear, 1-3=Partly Cloudy, 45/48=Fog, 51-55=Drizzle, 61-65=Rain, 71-75=Snow, 80-82=Showers, 95-99=Thunderstorm
- Mutex-protected status struct, polled by UI

#### 5.5 `src/ui/apps/App_ClockWeather.h` (50 lines)
```cpp
#pragma once
#include "IApp.h"
#include "../components/UI_Keyboard.h"

class App_ClockWeather : public IApp {
private:
    enum Screen { SCREEN_CLOCK, SCREEN_SETTINGS };
    
    Screen      _screen;
    float       _anim_time;
    bool        _show_keyboard;
    bool        _is_loading;
    char        _status_msg[64];
    
    // Cached service data (polled in loop(), read in render())
    struct tm   _time_info;
    bool        _ntp_synced;
    bool        _weather_has_data;
    bool        _weather_is_stale;
    float       _temperature;
    float       _wind_speed;
    int         _humidity;
    int         _weather_code;
    int         _is_day;
    char        _city_name[32];
    
    // Settings screen
    int         _settings_cursor;
    
    // Keyboard
    UI_Keyboard _keyboard;
    
    // Layout cache
    int32_t     _cx;
    int32_t     _w;
    int32_t     _h;
    
public:
    App_ClockWeather();
    ~App_ClockWeather() override;
    
    void init()   override;
    void exit()   override;
    void loop()   override;
    void render(LGFX_Sprite* canvas) override;
    void handleInput(input_event_t event) override;

private:
    void _draw_clock_screen(LGFX_Sprite* canvas);
    void _draw_settings_screen(LGFX_Sprite* canvas);
    void _draw_weather_panel(LGFX_Sprite* canvas, int32_t y);
    void _draw_digit_pair(LGFX_Sprite* canvas, int value, int32_t x, int32_t y, bool blink_colon);
};
```

#### 5.6 `src/ui/apps/App_ClockWeather.cpp` (500 lines)
Key implementation details:
- `init()`: Reset all state, poll services for initial data
- `loop()`: Poll ntp_service and weather_service status, advance `_anim_time += 0.05f`
- `handleInput()`: BTN_LEFT → back, BTN_SELECT → settings/keyboard, BTN_UP/DOWN → settings navigation
- `render()`: Start with `fillSprite(theme_get_bg())`, draw clock digits with Font2, weather panel with Font0
- Colon blink: `sinf(_anim_time * 6.28f) > 0` → show/hide colon
- Weather panel: rounded rect with semi-transparent fill, temperature in Font2, condition/wind/humidity in Font0
- "Offline" badge when Wi-Fi disconnected
- "Stale" indicator when weather data > 2 hours old

### Modified Files (4)

#### 5.7 `src/config/sys_config.h` — Add ~25 constants
```c
// NTP Service
#define NTP_SERVER_URL              "pool.ntp.org"
#define NTP_SYNC_INTERVAL_S        3600
#define NTP_RESTORE_MAX_AGE_S      86400      // discard if > 24 hours old
#define NTP_RESTORE_MAX_FUTURE_S   3600       // discard if > 1 hour in future
#define NTP_SAVE_INTERVAL_S        60
#define NTP_NVS_NAMESPACE          "clock_time"
#define NTP_NVS_KEY_EPOCH          "epoch"
#define NTP_NVS_KEY_TIMER_US       "timer_us"

// Weather Service
#define WEATHER_HTTP_TIMEOUT_MS    10000
#define WEATHER_REFRESH_INTERVAL_S 1800
#define WEATHER_STALE_THRESHOLD_S  7200       // 2 hours
#define WEATHER_CONFIG_PATH        "/storage/ai/clock_location.json"
#define WEATHER_IP_GEO_URL         "http://ip-api.com/json/?fields=status,country,city,lat,lon,timezone"
#define WEATHER_FORECAST_URL       "https://api.open-meteo.com/v1/forecast"
#define WEATHER_GEOCODING_URL      "https://geocoding-api.open-meteo.com/v1/search"
#define WEATHER_TASK_STACK         8192
#define WEATHER_TASK_PRIORITY      5
#define WEATHER_TASK_CORE          0

// Clock & Weather App
#define CLOCK_WEATHER_ANIM_SPEED   0.05f
```

#### 5.8 `src/ui/ui_task.cpp` — Register app
- Include `"apps/App_ClockWeather.h"`
- Declare `static App_ClockWeather app_clockweather;`
- Add `app_grid.registerApp("Clock & Weather", icon_clockweather, &app_clockweather);`

#### 5.9 `main.cpp` — Init services
- Include `"services/ntp_service.h"` and `"services/weather_service.h"`
- After `wifi_mgr_init()`: call `ntp_service_init()` and `weather_service_init()`

#### 5.10 `sdkconfig.defaults` — SNTP config
```
CONFIG_LWIP_SNTP_MAX_SERVERS=3
```

### New Asset (1)

#### 5.11 `assets/icons/clockweather.png` (64x64)
- Clock face icon with weather element (sun/cloud)
- Run `python convert_icons.py` to generate C array

---

## 6. NVS Key Map

| Namespace | Key | Type | Purpose |
|-----------|-----|------|---------|
| `"clock_time"` | `"epoch"` | `u32` | Last saved Unix timestamp |
| `"clock_time"` | `"timer_us"` | `u64` | `esp_timer_get_time()` at save time |

---

## 7. LittleFS File Map

| Path | Format | Purpose |
|------|--------|---------|
| `/storage/ai/clock_location.json` | JSON | Cached location: `{"city","lat","lon","tz"}` |
| `/storage/ai/weather_cache.json` | JSON | Cached weather response (for offline use) |

---

## 8. Commit Sequence

| # | Commit | Files |
|---|--------|-------|
| 1 | `feat(service): add NTP time service with three-layer accuracy` | ntp_service.h, ntp_service.c, sys_config.h |
| 2 | `feat(service): add weather service with Open-Meteo + IP geolocation` | weather_service.h, weather_service.c, sys_config.h |
| 3 | `feat(ui): add Clock & Weather app with auto-location and settings` | App_ClockWeather.h, App_ClockWeather.cpp, icon, ui_task.cpp, main.cpp |
| 4 | `chore(build): add SNTP config to sdkconfig.defaults` | sdkconfig.defaults |

---

## 9. Hardware Validation Checklist

```
TIME ACCURACY
□ Cold boot + Wi-Fi → NTP syncs within 3 seconds → exact time
□ Cold boot + NO Wi-Fi → restored from NVS → within 1 minute accuracy
□ First boot ever (no NVS) → shows 00:00:00 until NTP or manual set
□ Power cycle → time restored from NVS → near-correct immediately
□ 24 hours offline → time drifts < 2 minutes (esp_timer is stable)
□ 7 days offline → time drifts < 10 minutes
□ Timezone change → NTP re-sync adjusts correctly

WEATHER ACCURACY
□ Boot + Wi-Fi → weather loads within 10 seconds
□ Weather refreshes every 30 minutes
□ Cached weather loads offline
□ "Stale" indicator appears after 2 hours without refresh
□ City auto-detected from IP on first boot
□ "Change Location" → keyboard → city name → coordinates update
□ Weather API failure → cached data shown, no crash
□ JSON parse error → cached data shown, no crash

UI/UX
□ Clock shows 24h format with blinking colon
□ Weather panel shows temp, condition, humidity, wind
□ "Offline" badge when Wi-Fi disconnected
□ Settings screen shows current location
□ BTN_LEFT → back to grid from both screens
□ Theme colors applied correctly to all elements

MEMORY
□ Heap stays above 20KB during all operations
□ PSRAM stays above 500KB
□ No memory leaks after 10 refresh cycles
```

---

## 10. Performance Budgets

| Operation | Budget | Notes |
|-----------|--------|-------|
| NTP sync | 2-3 seconds | First boot only |
| IP geolocation | 1-2 seconds | HTTP, non-blocking task |
| Weather fetch | 2-3 seconds | HTTPS with TLS |
| JSON parse | <10ms | cJSON is fast |
| NVS save (time) | ~10ms | Every 60 seconds |
| NVS save (config) | ~10ms | On location change |
| UI frame render | <15ms | Standard 40fps budget |
| Weather task stack | 8KB | Verified adequate |
