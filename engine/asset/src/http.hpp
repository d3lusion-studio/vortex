#pragma once
#include "vortex/core/types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace vortex::assets {

// Fetch a URL into memory. Blocking, and called only from an IO thread — which is the whole reason
// a web asset costs the frame nothing: the network is just a slower disk, and the manager already
// knows how to wait on a slow disk without stalling the game.
//
// libcurl stops at this file. Nothing outside it includes a curl header, so the day this is
// replaced the blast radius is one function.
//
// Returns false and fills `error` on any failure, INCLUDING an HTTP status that is not 2xx. A 404
// page is bytes, and a decoder handed those bytes would report "corrupt PNG" — which sends you
// looking at the image instead of at the URL.
[[nodiscard]] bool httpGet(const std::string& url, u32 timeoutSeconds,
                           std::vector<std::byte>& out, std::string& error);

// Whether this build has HTTP at all.
[[nodiscard]] bool httpAvailable();

}
