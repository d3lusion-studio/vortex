# Vortex — Getting Started

## Prerequisites

| Tool | Minimum | Notes |
|---|---|---|
| CMake | 3.23 | Required for CMakePresets v6 |
| Ninja | any | Default generator; install via package manager |
| C++ compiler | Clang 16 / GCC 12 / MSVC 19.35 | See [tech_stack.md](tech_stack.md) |
| Vulkan SDK | 1.3+ | Required for the Vulkan RHI backend |
| Git | any | `FetchContent` fetches dependencies at configure time |

### Linux (Arch / CachyOS)

```bash
sudo pacman -S cmake ninja clang vulkan-devel
```

### Linux (Ubuntu / Debian)

```bash
sudo apt install cmake ninja-build clang libvulkan-dev
# Install Vulkan SDK from https://vulkan.lunarg.com if the package is outdated
```

### Windows

1. Install [Vulkan SDK](https://vulkan.lunarg.com).
2. Install Visual Studio 2022 (MSVC) or LLVM/Clang.
3. Install Ninja via `winget install Ninja-build.Ninja` or use the bundled CMake Ninja.

---

## Clone

```bash
git clone https://github.com/<you>/vortex.git
cd vortex
```

Dependencies are fetched automatically by CMake via `FetchContent`; no submodule init needed.

---

## Configure

Use CMake presets. Three presets are defined:

| Preset | Build type | Asserts | Warnings-as-errors |
|---|---|---|---|
| `debug` | Debug | ON | ON |
| `relwithdebinfo` | RelWithDebInfo | ON | OFF |
| `release` | Release | OFF | OFF |

```bash
# Debug (recommended for development)
cmake --preset debug

# Release
cmake --preset release
```

Build output goes to `build/<preset-name>/`.

### Optional CMake variables

```bash
# Disable examples
cmake --preset debug -DVORTEX_BUILD_EXAMPLES=OFF

# Choose a different RHI backend (future)
cmake --preset debug -DVORTEX_RHI_BACKEND=WebGPU
```

---

## Build

```bash
# Build everything in the debug preset
cmake --build --preset debug

# Build a specific target
cmake --build --preset debug --target hello
```

---

## Run

```bash
./build/debug/examples/hello
```

Expected output:

```
[INFO ][App] Vortex Engine v0.1.0
[INFO ][Math] length({3,4}) = 5.0
[INFO ][Res] default handle valid? no
[INFO ][Res] handle{7,1} valid? yes
```

---

## Run Tests

```bash
cmake --preset debug -DVORTEX_BUILD_TESTS=ON
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

---

## IDE Setup

### CLion

CLion detects CMakePresets automatically. Open the repo root, select a preset from the CMake profiles panel, and build/run normally. CLion places its build output in `cmake-build-<config>/` (separate from the preset `build/` directory).

### VS Code

Install the **CMake Tools** extension. Open the repo root; CMake Tools reads `CMakePresets.json` and presents the presets in the status bar. Select `debug`, configure, then build.

For IntelliSense, point **clangd** at `build/debug/compile_commands.json`:

```json
// .vscode/settings.json
{
  "clangd.arguments": ["--compile-commands-dir=build/debug"]
}
```

### Vim / Neovim

Use `clangd` (via `nvim-lspconfig` or `lsp-zero`) with the same `compile_commands.json` path.

---

## Project Layout

```
vortex/
├── cmake/              CMake helper macros (VortexModule.cmake) and package config
├── docs/               Architecture, conventions, tech stack, this file
├── engine/
│   └── core/           Foundation layer (types, math, log, assert, handle)
├── examples/
│   └── hello/          Minimal usage example
├── games/              Placeholder for sample games (e.g. pong)
├── tests/              Unit tests (enabled with VORTEX_BUILD_TESTS)
├── third_party/        FetchContent declarations for external libraries
├── tools/              Offline tools (shader compiler, asset cooker — Phase 8)
├── CMakeLists.txt      Root build file
└── CMakePresets.json   Configure / build / test presets
```

See [architecture.md](architecture.md) for the full module dependency diagram.

---

## Writing a Game

Once the engine reaches Phase 4+ (ECS + Renderer), game code looks like this:

```cpp
#include <vortex/engine.hpp>
using namespace vortex;

int main() {
    Engine engine({ .width = 1280, .height = 720, .title = "My Game" });

    Scene scene;
    TextureHandle playerTex = engine.assets().loadTexture("sprites/player.png");

    Entity player = scene.spawn();
    player.add<Transform2D>({ .position = {0, 0}, .scale = {1, 1} });
    player.add<SpriteComp>({ .tex = playerTex, .color = {1, 1, 1, 1} });

    engine.input().bind("MoveX", Axis{ Key::A, Key::D });

    scene.system([&](float dt) {
        float x = engine.input().axis("MoveX");
        player.get<Transform2D>().position.x += x * 300.0f * dt;
    });

    engine.run(scene);
}
```

Place game code under `games/<game-name>/` and add a `CMakeLists.txt` that links against `Vortex::vortex`.

---

## Adding a New Engine Module

1. Create `engine/<module>/include/vortex/<module>/` and `engine/<module>/src/`.
2. Add `engine/<module>/CMakeLists.txt`:

```cmake
vortex_add_module(<module>
    SOURCES
        src/foo.cpp
    PUBLIC_DEPS
        Vortex::core          # headers consumers need
    PRIVATE_DEPS
        Vortex::rhi           # implementation detail
)
```

3. Add `add_subdirectory(<module>)` in `engine/CMakeLists.txt`.
4. Add `Vortex::<module>` to `target_link_libraries(vortex_all …)` in `engine/CMakeLists.txt`.
