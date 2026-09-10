// X-macro field registry generates the schema and serialization registration.
//
// CONFIG_GROUP(label): group label.
// CONFIG_FIELD(type, name, default): persisted scalar.
// CONFIG_ARR(type, name, N, ...): persisted array with variadic defaults.
// CONFIG_NOSAVE / CONFIG_NOSAVE_ARR: runtime-only fields.
//
// No include guard; callers define and undefine the expansion macros.

// Default macro definitions.
#ifndef CONFIG_FIELD
#define CONFIG_GROUP(label)
#define CONFIG_FIELD(type, name, ...)
#define CONFIG_ARR(type, name, n, ...)
#define CONFIG_NOSAVE(type, name, ...)
#define CONFIG_NOSAVE_ARR(type, name, n, ...)
#define _CONFIG_FIELDS_INL_SELFGUARD
#endif

CONFIG_GROUP("配置系统")
CONFIG_FIELD(bool,  bAutoLoadConfigOnStartup, true)

CONFIG_GROUP("界面")
CONFIG_FIELD(int,   InjectionMode,         0)      // 0 = SwapHook, 1 = Overlay
CONFIG_FIELD(int,   RenderBackend,         1)      // 0 = OpenGL, 1 = Vulkan
CONFIG_FIELD(bool,  bShowImGuiDraw,        true)
CONFIG_FIELD(float, FontScale,             1.0f)

#ifdef _CONFIG_FIELDS_INL_SELFGUARD
#undef CONFIG_GROUP
#undef CONFIG_FIELD
#undef CONFIG_ARR
#undef CONFIG_NOSAVE
#undef CONFIG_NOSAVE_ARR
#undef _CONFIG_FIELDS_INL_SELFGUARD
#endif
