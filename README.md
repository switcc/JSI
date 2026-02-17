# JSI – Switchable JavaScript Engine Interface

A C++17 abstraction layer that provides a unified interface over multiple JavaScript engines, with the ability to switch between them at runtime.

## Supported Engines

| Engine  | Status | Notes |
|---------|--------|-------|
| QuickJS | Full   | Lightweight, embeddable, ES2023 compliant |
| V8      | Full   | High-performance, used in Chrome and Node.js |

## Architecture

```
┌─────────────────────────────────┐
│         Your Application        │
├─────────────────────────────────┤
│   JSRuntime  (engine switcher)  │
├─────────────────────────────────┤
│          JSEngine (ABC)         │
├───────────────┬─────────────────┤
│ QuickJSEngine │    V8Engine     │
└───────────────┴─────────────────┘
```

- **`jsi::JSValue`** — Engine-agnostic value type (undefined, null, bool, number, string, object, array)
- **`jsi::JSEngine`** — Abstract base class every backend implements
- **`jsi::JSRuntime`** — Owns a `JSEngine` instance and allows hot-swapping via `switchEngine()`

## Quick Start

### 1. Get Dependencies

```sh
# Fetch QuickJS source into third_party/ (downloads tarball, no git auth needed)
./setup.sh quickjs

# V8 – install via your package manager
sudo apt-get install libv8-dev          # Debian/Ubuntu
brew install v8                         # macOS
```

If `setup.sh` can't reach the network, you can manually download QuickJS from
[bellard.org/quickjs](https://bellard.org/quickjs/) and extract it into `third_party/quickjs/`.

### 2. Build

```sh
cmake -B build
cmake --build build
```

CMake will auto-detect which engines are available and enable them. You can also point to specific paths:

```sh
cmake -B build \
  -DQUICKJS_SOURCE_DIR=/path/to/quickjs \
  -DV8_ROOT=/path/to/v8
```

### 3. Run the Example

```sh
./build/jsi_example
```

## Usage

### Basic Evaluation

```cpp
#include "jsi/jsi.h"

jsi::JSRuntime runtime(jsi::EngineType::QuickJS);

auto result = runtime.engine().evaluate("1 + 2 + 3");
std::cout << result.asNumber(); // 6
```

### Switching Engines at Runtime

```cpp
jsi::JSRuntime runtime(jsi::EngineType::QuickJS);
runtime.engine().evaluate("var x = 42");

// Switch to V8 — previous context is destroyed, fresh engine is created
runtime.switchEngine(jsi::EngineType::V8);
runtime.engine().evaluate("var y = 100");

// Switch back
runtime.switchEngine(jsi::EngineType::QuickJS);
```

### Global Variables

```cpp
auto& engine = runtime.engine();

engine.setGlobal("name", jsi::JSValue::from("world"));
auto result = engine.evaluate("'Hello, ' + name + '!'");
// result.asString() == "Hello, world!"

engine.evaluate("var pi = Math.PI");
auto pi = engine.getGlobal("pi");
// pi.asNumber() == 3.14159...
```

### Calling JS Functions

```cpp
engine.evaluate("function factorial(n) {"
                "  return n <= 1 ? 1 : n * factorial(n - 1);"
                "}");

auto result = engine.call("factorial", {jsi::JSValue::from(10)});
// result.asNumber() == 3628800
```

### Registering Native (C++) Functions

```cpp
engine.registerNativeFunction("add", [](const std::vector<jsi::JSValue>& args) {
    return jsi::JSValue::from(args[0].asNumber() + args[1].asNumber());
});

auto result = engine.evaluate("add(100, 200)");
// result.asNumber() == 300
```

### Querying Available Engines

```cpp
for (auto type : jsi::JSRuntime::availableEngines()) {
    std::cout << jsi::engineTypeName(type) << "\n";
}

if (jsi::JSRuntime::isAvailable(jsi::EngineType::V8)) {
    // V8 was compiled in
}
```

### Error Handling

```cpp
try {
    engine.evaluate("throw new Error('oops')");
} catch (const jsi::JSError& e) {
    std::cerr << e.what(); // "QuickJS eval error: Error: oops"
}
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `JSI_ENABLE_QUICKJS` | `ON` | Build the QuickJS backend |
| `JSI_ENABLE_V8` | `ON` | Build the V8 backend |
| `JSI_BUILD_EXAMPLES` | `ON` | Build example programs |
| `JSI_FETCH_QUICKJS` | `ON` | Auto-download QuickJS via FetchContent |
| `QUICKJS_SOURCE_DIR` | — | Path to QuickJS source tree |
| `V8_ROOT` | — | Path to V8 installation |

## Design Notes

- **State does not transfer** when switching engines. Each `switchEngine()` call creates a fresh context. This is intentional — it avoids leaking engine-specific internals and keeps the abstraction clean.
- **`JSValue` is a bridge type**, not a reference into an engine's heap. Primitive values (bools, numbers, strings, arrays) are copied when crossing the boundary. Complex objects are represented as opaque placeholders.
- Both backends are **conditionally compiled** via `JSI_HAS_QUICKJS` / `JSI_HAS_V8` defines. The library compiles and links even with zero backends — it will just throw at runtime if you try to create an engine.

## Project Structure

```
include/jsi/jsi.h                  Public interface
src/runtime.cpp                    JSRuntime + factory
src/quickjs/quickjs_engine.h/.cpp  QuickJS backend
src/v8/v8_engine.h/.cpp            V8 backend
examples/main.cpp                  Demo application
cmake/FindV8.cmake                 CMake V8 finder
setup.sh                           Dependency fetcher
```
