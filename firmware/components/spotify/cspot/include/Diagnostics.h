#pragma once

#include <string>  // for string

namespace cspot {
/**
 * @brief Remembers why the last track failed to load, for the status API.
 * @remark An empty message clears it once a track loads again.
 */
void setLastError(const std::string& message);
std::string lastError();
}  // namespace cspot
