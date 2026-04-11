# AndSwapChainHook

[中文](#中文) | [English](#english)

---

<a id="中文"></a>

### 构建

**环境要求：**
- CMake 3.22.1+
- Android NDK（ARM64-v8a，API 27+）
- C++20

```bash
# 设置 NDK_HOME 环境变量（替换为你的 NDK 实际路径）
export NDK_HOME=/path/to/android-ndk

cmake -B build -G "Ninja"
cmake --build build
```

输出：`libAndSwapChainHook.so`

### 使用方法

使用 [AndKittyInjector v5.1.0](https://github.com/MJx0/AndKittyInjector) 注入到目标应用：

```bash
./AndKittyInjector --package <包名> -lib libAndSwapChainHook.so --memfd --hide --watch
```

---

<a id="english"></a>

### Build

**Requirements:**
- CMake 3.22.1+
- Android NDK (ARM64-v8a, API 27+)
- C++20

```bash
# Set NDK_HOME environment variable (replace with your actual NDK path)
export NDK_HOME=/path/to/android-ndk

cmake -B build -G "Ninja"
cmake --build build
```

Output: `libAndSwapChainHook.so`

### Usage

Inject into a target app using [AndKittyInjector v5.1.0](https://github.com/MJx0/AndKittyInjector):

```bash
./AndKittyInjector --package <package_name> -lib libAndSwapChainHook.so --memfd --hide --watch
```

## Credits

- [AndKittyInjector](https://github.com/MJx0/AndKittyInjector)
- [Dobby](https://github.com/jmpews/Dobby)
