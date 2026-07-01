#pragma once

#include <filesystem>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "ConfigStorage.h"

namespace Config {

// 字段单点声明在 ConfigFields.inl, X-macro 同步生成 Schema 与序列化注册表。
//
// 下游扩展: 通过 target_compile_definitions(... PUBLIC ...) 把 CONFIG_FIELDS_EXT
// 定义成 quoted include 路径
struct Schema {
#define CONFIG_GROUP(label)
#define CONFIG_FIELD(type, name, def)            type name = def;
#define CONFIG_ARR(type, name, n, ...)           type name[n] = __VA_ARGS__;
#define CONFIG_NOSAVE(type, name, def)           type name = def;
#define CONFIG_NOSAVE_ARR(type, name, n, ...)    type name[n] = __VA_ARGS__;
#include "ConfigFields.inl"
#ifdef CONFIG_FIELDS_EXT
#include CONFIG_FIELDS_EXT
#endif
#undef CONFIG_GROUP
#undef CONFIG_FIELD
#undef CONFIG_ARR
#undef CONFIG_NOSAVE
#undef CONFIG_NOSAVE_ARR
};

struct Field {
    std::string_view name;
    std::variant<bool*, int*, float*,
                 std::span<int>, std::span<float>> ref;
};

const std::vector<Field>& GetFields();

void Save(const std::filesystem::path& path);
void Load(const std::filesystem::path& path);

}  // namespace Config

extern Config::Schema CFG;
