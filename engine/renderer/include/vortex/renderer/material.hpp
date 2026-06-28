#pragma once
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::renderer {

struct Material {
    rhi::PipelineHandle  pipeline;
    rhi::BindGroupHandle bindGroup;

    [[nodiscard]] bool valid() const { return pipeline.valid(); }
    [[nodiscard]] bool operator==(const Material& o) const {
        return pipeline == o.pipeline && bindGroup == o.bindGroup;
    }
};

}
