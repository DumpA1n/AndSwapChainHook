#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Config {

// /sdcard/Android/data/<pkg>/cache
std::filesystem::path GetSavePath();

// Filename only, without a directory, such as .conf_0.
std::vector<std::string> ScanFiles();

const std::string& GetActiveName();
void               SetActiveName(const std::string& name);
std::filesystem::path GetActiveFullPath();

// Select the next unused numeric suffix.
std::string CreateNewFile();

void SaveToActive();
void LoadFromActive();

// Load the last active configuration recorded in the TU-private .conf_active marker.
void AutoLoadOnStartup();

}  // namespace Config
