#pragma once
#include "vortex/core/handle.hpp"

namespace vortex::ecs {

struct EntityTag {};
using Entity = Handle<EntityTag>;

}
