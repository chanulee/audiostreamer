#include "Diagnostics.h"

#include <mutex>  // for mutex, scoped_lock

namespace {
std::mutex errorMutex;
std::string errorMessage;
}  // namespace

void cspot::setLastError(const std::string& message) {
  std::scoped_lock lock(errorMutex);
  errorMessage = message;
}

std::string cspot::lastError() {
  std::scoped_lock lock(errorMutex);
  return errorMessage;
}
