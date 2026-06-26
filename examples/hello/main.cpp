#include "vortex/core/version.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/handle.hpp"
#include "vortex/core/math/vec2.hpp"

using namespace vortex;

struct TextureTag {};
using TextureHandle = Handle<TextureTag>;

int main() {
    VORTEX_INFO("App", "Vortex Engine v%s", VORTEX_VERSION_STRING);

    Vec2 a{3.0f, 4.0f};
    VORTEX_INFO("Math", "length({3,4}) = %.1f", length(a));

    TextureHandle tex{};
    VORTEX_INFO("Res", "default handle valid? %s", tex.valid() ? "yes" : "no");

    tex = TextureHandle{.index = 7, .generation = 1};
    VORTEX_INFO("Res", "handle{7,1} valid? %s", tex.valid() ? "yes" : "no");
    return 0;
}

