# Vortex — Tech Stack

## Language & Standard

| Item | Choice | Reason |
|---|---|---|
| Language | C++20 | Concepts, `std::span`, designated initialisers, `[[likely]]`, coroutines (future) |
| Extensions | OFF | Strict portability (`CMAKE_CXX_EXTENSIONS OFF`) |
| Build standard | C++20 minimum; C++23 features used where available | `std::print`, `std::expected` |

---

## Build System

| Tool | Version | Role |
|---|---|---|
| **CMake** | ≥ 3.23 | Primary build system |
| **CMakePresets.json** | v6 | Canonical configure/build presets (`debug`, `release`, `relwithdebinfo`) |
| **Ninja** | latest | Default generator (fast incremental builds) |
| `compile_commands.json` | auto-generated | clangd / language server integration |

### CMake Options

| Option | Default | Description |
|---|---|---|
| `VORTEX_BUILD_EXAMPLES` | ON (top-level) | Build `examples/` |
| `VORTEX_BUILD_TESTS` | ON (top-level) | Build `tests/` (requires doctest) |
| `VORTEX_INSTALL` | ON (top-level) | Generate install/export rules |
| `VORTEX_WARNINGS_AS_ERRORS` | OFF | Treat `-Wall -Wextra` warnings as errors |
| `VORTEX_RHI_BACKEND` | `Vulkan` | Active graphics backend (`Vulkan`, `DX12`, `Metal`, `WebGPU`) |

---

## Core Dependencies (active)

| Library | Version | How acquired | Role |
|---|---|---|---|
| **doctest** | 2.4.11 | `FetchContent` | Unit testing framework (header-friendly, fast compile) |

---

## Planned Dependencies (Phase 1–6)

These are referenced in the roadmap and interface designs. Integration happens when the corresponding phase begins.

### Platform & Windowing

| Library | Phase | Role |
|---|---|---|
| **GLFW** | 1 | Window creation, input events, Vulkan surface — lives exclusively in `platform/glfw/` |

### Graphics

| Library | Phase | Role |
|---|---|---|
| **vk-bootstrap** | 2 | Vulkan instance, device, queue family selection boilerplate |
| **VMA** (Vulkan Memory Allocator) | 2 | GPU memory allocation (`vk_memory.cpp` wrapper) |
| **glslc / slangc** | 2 | Compile `.glsl` / `.slang` shaders to SPIR-V at build time |

### Assets & Media

| Library | Phase | Role |
|---|---|---|
| **stb_image** | 3/5 | PNG/JPEG/BMP decode — single-header, no dependencies |
| **stb_truetype** | 6 | Font rasterisation → texture atlas |
| **msdf-gen** | 6 (optional) | MSDF font atlas for crisp text at any size |

### Audio

| Library | Phase | Role |
|---|---|---|
| **miniaudio** | 6 | Cross-platform audio (SFX, music, loop, volume) — single `.h` file behind `IAudioBackend` |

### Physics

| Library | Phase | Role |
|---|---|---|
| **Box2D** | 6 | 2D rigid body physics, collision callbacks into ECS |

### ECS

| Library | Phase | Role |
|---|---|---|
| **EnTT** | 4 | Sparse-set ECS (header-only, excellent DX, very fast iteration) |

> EnTT is the recommended default. A custom archetype ECS may replace it later if profiler data justifies it.

### Math

| Option | Notes |
|---|---|
| **Custom (`core/math/`)** | Engine-owned `Vec2/3/4`, `Mat3/4`, `Quat`, `Rect` — already started |
| **glm** (optional fallback) | Can be included for functions not yet in `core/math/`; keep behind a `core/math/glm_compat.hpp` shim |

### Profiling & Debug

| Library | Phase | Role |
|---|---|---|
| **Tracy** | 7 | CPU frame profiler (zone macros → `PROFILE_SCOPE`), GPU timestamp integration |
| **Dear ImGui** | 8 (optional) | Runtime entity/component inspector, profiler overlay |

---

## Compiler Support

| Compiler | Minimum Version | Notes |
|---|---|---|
| **Clang** | 16+ | Primary development compiler on Linux |
| **GCC** | 12+ | Secondary Linux target |
| **MSVC** | 19.35+ (VS 2022 17.5) | Windows; `/W4 /WX` instead of `-Wall -Wextra -Werror` |

---

## Platform Targets

| Platform | Status | Backend |
|---|---|---|
| Linux (x86-64) | Active | GLFW + Vulkan |
| Windows | Planned (Phase 10) | GLFW or Win32 + DX12 or Vulkan |
| macOS | Planned | GLFW + Metal (via MoltenVK as interim) |
| Web (WASM) | Long-term | Emscripten + WebGPU |

---

## Tooling

| Tool | Role |
|---|---|
| **clang-format** | Code formatting (`.clang-format` at repo root — to be added) |
| **clang-tidy** | Static analysis; enforces abstraction boundary rules |
| **GitHub Actions** | CI: configure, build, run tests, grep abstraction violations |
| **Asset cooker CLI** | Offline import pipeline: raw files → engine binary format (Phase 8) |
| **Shader compiler pipeline** | `glslc`/`slangc` invoked by CMake custom commands; `.spv` output to build dir |

---

## Dependency Acquisition Policy

- **`FetchContent`** — preferred for small, header-only or source-only libraries that build quickly (doctest, stb, EnTT).
- **`find_package`** — preferred for large system-installed libraries (Vulkan SDK, Box2D installed via package manager).
- **Git submodule** — avoid unless the library requires deep CMake integration not achievable via FetchContent.
- All third-party integration lives in `third_party/CMakeLists.txt`. Module-level CMakeLists never call `FetchContent_Declare`.
