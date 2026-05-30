# Plain Text

> Source: https://raw.githubusercontent.com/bmorcelli/Launcher/main/src/app_registry.h
> Cached: 2026-05-30T06:35:34.483Z

---

#ifndef LAUNCHER_APP_REGISTRY_H
#define LAUNCHER_APP_REGISTRY_H

#include "partition_table_model.h"
#include <Arduino.h>
#include <vector>

struct LauncherAppMetadata {
    String name;
    String label;
    std::vector<String> fatLabels;
};

std::vector<LauncherAppMetadata> launcherLoadAppRegistry();
std::vector<LauncherAppMetadata> launcherListInstalledApps();
bool launcherSaveAppMetadata(const LauncherAppMetadata &app);
bool launcherRemoveAppMetadata(const char *label);
std::vector<String> launcherAppFatLabelsForLabel(const char *label);
void launcherShowAppLauncher();
void launcherShowAppActions(const char *label);
String launcherAppDisplayNameForLabel(const char *label);
String launcherSelectedBootAppName();
bool launcherBootCurrentApp();
bool launcherBootInstalledAppOrShowMenu();
bool launcherBootAppByLabel(const char *label);
bool launcherDeleteAppByLabel(const char *label);
bool launcherRenameAppByLabel(const char *label);
String launcherAppNameFromFile(const String &source);

#endif
