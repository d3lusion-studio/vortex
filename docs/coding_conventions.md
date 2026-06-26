# Vortex — Coding Conventions

## Language Standard

C++20. `CMAKE_CXX_EXTENSIONS OFF`. All new code must compile with `-Wall -Wextra -Werror` (enforced in Debug builds by `VORTEX_WARNINGS_AS_ERRORS`).

---

## Naming

| Category | Convention | Examples |
|---|---|---|
| Namespace | `snake_case` | `vortex`, `rhi`, `pf`, `jobs` |
| Type / struct / class | `PascalCase` | `Vec2`, `Handle<T>`, `IGraphicsDevice` |
| Interface (abstract class) | `IPascalCase` | `IWindow`, `ICommandList`, `IInputProvider` |
| Function / method | `camelCase` | `createBuffer()`, `isKeyDown()`, `beginRenderPass()` |
| Free math functions | `camelCase` | `dot()`, `length()`, `normalize()` |
| Variable / member | `camelCase` | `index`, `generation`, `viewportSize` |
| Constant / `constexpr` value | `kPascalCase` | `Handle::kInvalid`, `kMaxFramesInFlight` |
| Macro | `VORTEX_SCREAMING_SNAKE_CASE` | `VORTEX_ASSERT`, `VORTEX_INFO`, `VORTEX_ENABLE_ASSERTS` |
| CMake target (internal) | `vortex_snake_case` | `vortex_core`, `vortex_rhi` |
| CMake alias (public) | `Vortex::PascalCase` | `Vortex::core`, `Vortex::rhi` |
| CMake option / cache var | `VORTEX_SCREAMING_SNAKE_CASE` | `VORTEX_BUILD_TESTS`, `VORTEX_RHI_BACKEND` |

---

## File Layout

```
engine/<module>/
    include/vortex/<module>/   ← public headers only
    src/                        ← implementation (.cpp) + private headers
    CMakeLists.txt
```

- Public headers live under `include/vortex/<module>/` so consumers write `#include "vortex/core/types.hpp"`.
- Implementation files that are not part of the public API belong in `src/`.
- One logical concept per file pair (`.hpp` / `.cpp`). Do not dump unrelated things together.

---

## Headers

- Always use `#pragma once` — not `#ifndef` include guards.
- No `using namespace` in any header; only in `.cpp` files (and even there, prefer to be explicit).
- Headers must be self-contained: include everything they need, rely on nothing implicit.
- Forward-declare rather than include when only a pointer or reference is needed.

```cpp
// Good — forward declare
struct TextureDesc;
void upload(const TextureDesc&);

// Bad — includes entire type just for a pointer
#include "texture_desc.hpp"
```

---

## Types

Always use the engine's primitive aliases from `vortex/core/types.hpp`:

```cpp
u32 index;    // not unsigned int
f32 ratio;    // not float
usize count;  // not size_t
```

---

## Handles vs. Pointers

Resources (textures, buffers, pipelines, entities) are referenced by **handle**, never by raw pointer:

```cpp
// Good
rhi::TextureHandle albedo = device.createTexture(desc);

// Bad — raw pointer leaks lifetime & GPU ownership
VkImage* rawImg = ...;
```

`Handle<Tag> = {u32 index, u32 generation}`. Check validity with `.valid()` before use. Stale handles (generation mismatch) are caught by the resource manager.

---

## Classes & Components

- **Components are pure POD.** No virtual methods, no constructors with side effects, no game logic.
- Logic goes in **systems** that iterate over components, never inside the component itself.
- Prefer `struct` over `class` for data types. Use `class` only when encapsulation of invariants matters.
- No raw `new` / `delete` in engine code. Use allocators, `std::unique_ptr`, or pool/arena allocators.

```cpp
// Good — POD component
struct Transform2D {
    Vec2  position{};
    f32   rotation = 0.0f;
    Vec2  scale{1.0f, 1.0f};
};

// Bad — logic inside component
struct Transform2D {
    Vec2 position;
    virtual void update(float dt);  // NO
};
```

---

## Virtual Interfaces

Used **only** at layer boundaries (RHI, Platform, AssetImporter, AudioBackend). The rule:

> Virtual at the boundary (called rarely). DOD + inlining in the hot loop (called 10k+ times / frame).

Pure interface classes follow this pattern:

```cpp
class IWindow {
public:
    virtual ~IWindow() = default;
    virtual bool shouldClose() const = 0;
    // …
};
```

No data members in interface classes. No non-pure virtual methods unless providing a useful default.

---

## `constexpr` and `[[nodiscard]]`

- Mark every function `constexpr` if it can be evaluated at compile time.
- Mark every function `[[nodiscard]]` if ignoring the return value is almost certainly a bug (factory functions, `valid()`, capacity queries).

```cpp
[[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalid; }
```

---

## Error Handling

- No exceptions. The engine is built with a `-fno-exceptions`-compatible mindset.
- **Programmer errors** (wrong API usage, broken invariants) → `VORTEX_ASSERT(cond, msg)`.
  Active in Debug and RelWithDebInfo; compiled out in Release.
- **Runtime errors** (file not found, GPU OOM) → return `bool` / optional / result type; log with `VORTEX_WARN` or `VORTEX_ERROR`.
- Never silently swallow errors.

```cpp
// Programmer error — assert
VORTEX_ASSERT(handle.valid(), "Attempted to bind invalid texture handle");

// Runtime error — log + return failure
if (!file.open(path)) {
    VORTEX_ERROR("Assets", "Cannot open: %s", path);
    return false;
}
```

---

## Logging

Use the category macros everywhere. Category is a short `const char*` that identifies the subsystem:

```cpp
VORTEX_INFO("RHI",    "Vulkan device created: %s", deviceName);
VORTEX_WARN("Assets", "Texture not found, using fallback: %s", path);
VORTEX_ERROR("ECS",   "Exceeded max component count");
```

Never use `printf` / `std::cout` directly in engine code.

---

## Performance-critical Code

- **No `new` / `delete` / `malloc` in hot paths.** Use frame allocator, pool, or pre-allocated buffers.
- **No virtual dispatch in inner loops.** Batch at a coarse-grained level; call virtual once per frame, not once per entity.
- Prefer SoA (structure of arrays) layout for data iterated in bulk.
- Mark hot-path functions `inline` or keep them in headers if the compiler needs to see them.
- `[[likely]]` / `[[unlikely]]` for truly predictable branches in hot paths only.

---

## Comments

Write no comments by default. A comment is justified only when the **why** is non-obvious:

- A workaround for a specific driver/OS bug.
- A subtle invariant that must be maintained for correctness.
- An intentionally counter-intuitive choice.

Never describe *what* the code does (the code does that). Never reference a ticket number or PR (that belongs in git history).

```cpp
// Bad — restates the obvious
// Increment the index
++index;

// Good — non-obvious invariant
// Must call newFrame() before isKeyDown(); GLFW edge state is undefined otherwise.
provider.newFrame();
```

---

## CMake Conventions

- Use `vortex_add_module(name SOURCES … PUBLIC_DEPS … PRIVATE_DEPS …)` for every engine module.
- Never use `target_include_directories` with a bare source path in a module's own CMakeLists — the macro already sets the correct include roots.
- `PUBLIC_DEPS` = headers consumer needs. `PRIVATE_DEPS` = implementation detail.
- Options are `VORTEX_` prefixed and `CACHE BOOL` with a description.
- `FetchContent` dependencies go in `third_party/CMakeLists.txt` only; never in module-level files.

---

## Abstraction Boundary Discipline

These are caught by CI and must never appear in commits:

```
# Forbidden in any file outside rhi/vulkan/
#include <vulkan/vulkan.h>

# Forbidden in any file outside platform/glfw/
#include <GLFW/glfw3.h>
```

Engine-side enums (`rhi::Format`, `rhi::BufferUsage`) shadow Vulkan/DX12 enums. The mapping exists only inside the backend implementation.
