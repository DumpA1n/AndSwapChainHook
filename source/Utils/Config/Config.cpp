#include "Config.h"

#include <android/log.h>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#ifndef kANDROID_LOG_TAG
#define kANDROID_LOG_TAG "Config"
#endif
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, kANDROID_LOG_TAG, __VA_ARGS__))

namespace fs = std::filesystem;

Config::Schema CFG{};

namespace Config {

const std::vector<Field>& GetFields() {
    static const std::vector<Field> fields = []{
        std::vector<Field> r;
#define CONFIG_GROUP(label)
#define CONFIG_FIELD(type, name, def)         r.push_back({#name, &CFG.name});
#define CONFIG_ARR(type, name, n, ...)        r.push_back({#name, std::span<type>(CFG.name)});
#define CONFIG_NOSAVE(type, name, def)
#define CONFIG_NOSAVE_ARR(type, name, n, ...)
#include "ConfigFields.inl"
#if __has_include("ConfigFieldsExt.inl")
#include "ConfigFieldsExt.inl"
#endif
#undef CONFIG_GROUP
#undef CONFIG_FIELD
#undef CONFIG_ARR
#undef CONFIG_NOSAVE
#undef CONFIG_NOSAVE_ARR
        return r;
    }();
    return fields;
}

namespace {

const std::unordered_map<std::string_view, const Field*>& FieldIndex() {
    static const auto idx = []{
        std::unordered_map<std::string_view, const Field*> m;
        const auto& list = GetFields();
        m.reserve(list.size());
        for (const auto& f : list) m.emplace(f.name, &f);
        return m;
    }();
    return idx;
}

// Parse comma-separated tokens into span<T>; ignore excess tokens and retain missing values.
template <typename T>
void ParseArrayInto(std::string_view value, std::span<T> dst) {
    size_t i = 0, start = 0;
    while (i < dst.size() && start <= value.size()) {
        size_t comma = value.find(',', start);
        std::string_view tok = value.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        if (!tok.empty()) {
            std::string s(tok);  // std::sto* requires NUL termination.
            if constexpr (std::is_same_v<T, int>)        dst[i] = std::stoi(s);
            else if constexpr (std::is_same_v<T, float>) dst[i] = std::stof(s);
            ++i;
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
}

}  // namespace

void Save(const fs::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::out);
    if (!file.is_open()) {
        LOGI("Failed to open file for save config: %s", path.c_str());
        return;
    }

    const auto& fields = GetFields();
    for (const auto& f : fields) {
        file << f.name << '=';
        std::visit([&file](auto&& r) {
            using R = std::decay_t<decltype(r)>;
            if constexpr (std::is_pointer_v<R>) {
                file << *r;
            } else {
                for (size_t i = 0; i < r.size(); ++i) {
                    if (i) file << ',';
                    file << r[i];
                }
            }
        }, f.ref);
        file << '\n';
    }

    file.close();
    LOGI("Configuration saved (%zu fields) to %s", fields.size(), path.c_str());
}

void Load(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
        LOGI("Failed to open file for load config: %s", path.c_str());
        return;
    }

    const auto& fields = GetFields();
    const auto& fieldMap = FieldIndex();

    std::string line;
    size_t loaded = 0;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string_view key(line.data(), pos);
        std::string_view value(line.data() + pos + 1, line.size() - pos - 1);

        auto it = fieldMap.find(key);
        if (it == fieldMap.end()) continue;

        try {
            std::visit([value](auto&& r) {
                using R = std::decay_t<decltype(r)>;
                if constexpr (std::is_pointer_v<R>) {
                    using V = std::remove_pointer_t<R>;
                    std::string s(value);
                    if constexpr (std::is_same_v<V, bool>)       *r = static_cast<bool>(std::stoi(s));
                    else if constexpr (std::is_same_v<V, int>)   *r = std::stoi(s);
                    else if constexpr (std::is_same_v<V, float>) *r = std::stof(s);
                } else {  // span<T>
                    ParseArrayInto<typename R::element_type>(value, r);
                }
            }, it->second->ref);
            ++loaded;
        } catch (const std::exception& e) {
            LOGI("Failed to parse config field '%.*s': %s",
                 static_cast<int>(key.size()), key.data(), e.what());
        }
    }

    file.close();
    LOGI("Configuration loaded (%zu/%zu fields) from %s", loaded, fields.size(), path.c_str());
}

}  // namespace Config
