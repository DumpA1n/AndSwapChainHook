#include "ConfigStorage.h"

#include <algorithm>
#include <android/log.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "Config.h"

#ifndef kANDROID_LOG_TAG
#define kANDROID_LOG_TAG "Config"
#endif
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, kANDROID_LOG_TAG, __VA_ARGS__))

namespace fs = std::filesystem;

namespace Config {

fs::path GetSavePath() {
    const char* ext = getenv("EXTERNAL_STORAGE");
    fs::path base = ext ? fs::path(ext) : fs::path("/sdcard");
    return base / "Android" / "data" / getprogname() / "cache";
}

namespace {

constexpr std::string_view kFilePrefix   = "." kPROJECT_NAME "_";
constexpr std::string_view kActiveMarker = "." kPROJECT_NAME "_active";

std::string s_ActiveName = std::string(kFilePrefix) + "0";

fs::path LastActiveFilePath() {
    return GetSavePath() / kActiveMarker;
}

void SaveLastActiveName() {
    fs::path p = LastActiveFilePath();
    fs::path dir = p.parent_path();
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    std::ofstream file(p, std::ios::out | std::ios::trunc);
    if (file.is_open()) {
        file << s_ActiveName;
        file.close();
    } else {
        LOGI("Failed to save last active config name to: %s", p.c_str());
    }
}

std::string LoadLastActiveName() {
    std::string fallback = std::string(kFilePrefix) + "0";
    fs::path p = LastActiveFilePath();
    if (!fs::exists(p)) {
        return fallback;
    }
    std::ifstream file(p, std::ios::in);
    if (!file.is_open()) {
        return fallback;
    }
    std::string name;
    std::getline(file, name);
    file.close();
    if (name.empty()) {
        return fallback;
    }
    return name;
}

}  // namespace

std::vector<std::string> ScanFiles() {
    std::vector<std::string> result;
    fs::path dir = GetSavePath();
    if (!fs::exists(dir)) return result;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.starts_with(kFilePrefix)) {
            result.push_back(name);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

const std::string& GetActiveName() {
    return s_ActiveName;
}

void SetActiveName(const std::string& name) {
    s_ActiveName = name;
    LOGI("Active config set to: %s", name.c_str());
    SaveLastActiveName();
}

fs::path GetActiveFullPath() {
    return GetSavePath() / s_ActiveName;
}

std::string CreateNewFile() {
    fs::path dir = GetSavePath();
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }

    int maxNum = -1;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.starts_with(kFilePrefix)) {
            try {
                int num = std::stoi(name.substr(kFilePrefix.size()));
                if (num > maxNum) maxNum = num;
            } catch (...) {}
        }
    }

    std::string newName = std::string(kFilePrefix) + std::to_string(maxNum + 1);
    fs::path newPath = dir / newName;
    Save(newPath);
    LOGI("Created new config file: %s", newName.c_str());
    return newName;
}

void SaveToActive() {
    fs::path path = GetActiveFullPath();
    Save(path);
    SaveLastActiveName();
}

void LoadFromActive() {
    fs::path path = GetActiveFullPath();
    if (!fs::exists(path)) {
        LOGI("Config file does not exist, saving defaults: %s", path.c_str());
        Save(path);
    }
    Load(path);
    SaveLastActiveName();
}

void AutoLoadOnStartup() {
    std::string lastName = LoadLastActiveName();
    LOGI("[Config] Last active config: %s", lastName.c_str());

    fs::path configPath = GetSavePath() / lastName;
    if (!fs::exists(configPath)) {
        LOGI("[Config] Config file not found, skipping auto-load: %s", configPath.c_str());
        return;
    }

    // Assign directly to avoid overwriting the last-active marker before loading.
    s_ActiveName = lastName;
    Load(configPath);

    if (!CFG.bAutoLoadConfigOnStartup) {
        LOGI("[Config] bAutoLoadConfigOnStartup is false, reverting to defaults");
        CFG = Schema{};
        return;
    }

    LOGI("[Config] Auto-loaded config: %s", lastName.c_str());
}

}  // namespace Config
