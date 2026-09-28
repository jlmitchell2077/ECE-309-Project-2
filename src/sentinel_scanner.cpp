#include "core/sentinel_scanner.h"
#include <stdexcept>

SentinelScanner::SentinelScanner(std::string sentinel) {
    if (sentinel.empty()) {
        throw std::invalid_argument("Sentinel must not be empty");
    }
    sentinel_ = sentinel;
    pending_ = "";
    found_ = false;
}

SentinelScanner::Out SentinelScanner::feed(std::string_view chunk) {
    Out result;
    result.safe_text = "";
    result.sentinel_found = found_;

    if (found_) {
        return result;
    }

    // Include the end of the previous chunk in this search.
    std::string text = pending_;
    text += chunk;
    std::size_t position = text.find(sentinel_);

    if (position != std::string::npos) {
        // Everything before the marker is printable. Ignore everything after it.
        result.safe_text = text.substr(0, position);
        result.sentinel_found = true;
        pending_.clear();
        found_ = true;
        return result;
    }

    // A future chunk could complete a marker starting in these last characters.
    // Holding at most length - 1 characters is enough to catch that case.
    std::size_t keep = sentinel_.size() - 1;
    if (text.size() < keep) {
        keep = text.size();
    }
    std::size_t safe_count = text.size() - keep;
    result.safe_text = text.substr(0, safe_count);
    pending_ = text.substr(safe_count);
    return result;
}

SentinelScanner::Out SentinelScanner::flush() {
    Out result;
    result.safe_text = pending_;
    result.sentinel_found = found_;
    pending_.clear();
    return result;
}
