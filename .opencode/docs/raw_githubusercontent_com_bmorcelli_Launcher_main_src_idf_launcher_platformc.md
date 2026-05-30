# Plain Text

> Source: https://raw.githubusercontent.com/bmorcelli/Launcher/main/src/idf/launcher_platform.cpp
> Cached: 2026-05-30T06:35:43.773Z

---

#include "launcher_platform.h"

#include <HardwareSerial.h>
#include <cstdarg>

void launcherConsolePrintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Serial.vprintf(fmt, args);
    va_end(args);
}

void launcherConsolePrint(const char *text) { Serial.print(text); }

void launcherConsolePrintln(const char *text) { Serial.println(text); }

void launcherConsoleBegin(unsigned long baud) { Serial.begin(baud); }

void launcherConsoleFlush() { Serial.flush(); }

void launcherConsoleEnd() { Serial.end(); }
