#pragma once
#include "vortex/core/handle.hpp"

namespace vortex::rhi {

struct BufferTag {};
struct TextureTag {};
struct SamplerTag {};
struct PipelineTag {};
struct BindGroupTag {};

using BufferHandle    = Handle<BufferTag>;
using TextureHandle   = Handle<TextureTag>;
using SamplerHandle   = Handle<SamplerTag>;
using PipelineHandle  = Handle<PipelineTag>;
using BindGroupHandle = Handle<BindGroupTag>;

}
