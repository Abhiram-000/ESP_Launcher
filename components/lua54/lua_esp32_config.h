/*
 * lua_esp32_config.h — ESP32-S3 Lua runtime notes (documentation only)
 *
 * LUA_32BITS is now injected globally via platformio.ini -DLUA_32BITS=1
 * so all translation units see identical type definitions.
 *
 * The PSRAM allocator is set at runtime by calling:
 *   lua_newstate(lua_psram_alloc, nullptr)
 * in lua_engine.cpp. No compile-time macro needed.
 *
 * GC tuning and path defaults are set via lua_setallocf / lua_sethook
 * calls in LuaEngine::init().
 */
#pragma once
// No compile-time overrides needed here — all done via build_flags + runtime init.
