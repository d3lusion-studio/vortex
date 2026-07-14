#include "http.hpp"

#include "vortex/core/log.hpp"

#ifdef VORTEX_ASSET_HTTP
#include <curl/curl.h>
#include <mutex>
#endif

namespace vortex::assets {

#ifdef VORTEX_ASSET_HTTP

namespace {

// curl_global_init() is NOT thread-safe and must run before any easy handle exists. The manager
// starts several IO threads and any of them may be the first to want a URL, so the init is forced
// to happen exactly once, whoever gets there first.
void ensureGlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            VORTEX_ERROR("Asset", "curl_global_init failed; web assets will not load");
    });
}

usize onBytes(char* data, usize size, usize count, void* userdata) {
    const usize total = size * count;
    auto* out = static_cast<std::vector<std::byte>*>(userdata);

    const auto* begin = reinterpret_cast<const std::byte*>(data);
    out->insert(out->end(), begin, begin + total);
    return total;
}

}

bool httpAvailable() { return true; }

bool httpGet(const std::string& url, u32 timeoutSeconds, std::vector<std::byte>& out,
             std::string& error) {
    ensureGlobalInit();

    // An easy handle per call. Sharing one across the IO threads would be a data race — curl is
    // explicit that a handle belongs to one thread at a time — and pooling them would buy nothing
    // next to the network round-trip we are about to pay for anyway.
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    out.clear();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onBytes);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "vortex-engine");

    // Fail on a bad status rather than handing 404's HTML to a PNG decoder.
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    // curl installs a SIGPIPE handler by default; in a process with its own signal handling that is
    // rude, and in a threaded one it is a bug waiting to happen.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode result = curl_easy_perform(curl);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = curl_easy_strerror(result);
        if (status != 0) error += " (HTTP " + std::to_string(status) + ")";
        out.clear();
        return false;
    }

    if (out.empty()) {
        error = "server returned no data";
        return false;
    }

    VORTEX_TRACE("Asset", "fetched %zu bytes from %s (HTTP %ld)", out.size(), url.c_str(), status);
    return true;
}

#else

bool httpAvailable() { return false; }

bool httpGet(const std::string& url, u32, std::vector<std::byte>&, std::string& error) {
    // Not a silent no-op. A build without curl that quietly returned empty bytes would look, from
    // the game's side, exactly like a server that was down.
    error = "this build has no HTTP support (libcurl was not found at configure time)";
    (void)url;
    return false;
}

#endif

}
