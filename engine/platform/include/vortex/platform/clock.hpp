#pragma once
#include "vortex/core/types.hpp"
#include <memory>

namespace vortex::pf {

class IClock {
public:
    virtual ~IClock() = default;

    [[nodiscard]] virtual f64 time()      const = 0;

    [[nodiscard]] virtual f64 deltaTime() const = 0;

    virtual void tick() = 0;
};

[[nodiscard]] std::unique_ptr<IClock> createClock();

}
