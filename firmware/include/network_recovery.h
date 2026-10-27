#pragma once

#include <stdint.h>

namespace sntch_fw {

// Hardware-independent startup policy: API retries do not restart a healthy AP.
class NetworkRecovery {
 public:
  enum class Action { kNone, kStartAccessPoint, kStartApi };

  Action nextAction(uint32_t nowMs, uint32_t retryIntervalMs) const {
    if (ready() || (hasAttempt_ && nowMs - lastAttemptMs_ < retryIntervalMs)) {
      return Action::kNone;
    }
    return apReady_ ? Action::kStartApi : Action::kStartAccessPoint;
  }

  void noteAttempt(uint32_t nowMs) { lastAttemptMs_ = nowMs; hasAttempt_ = true; }
  void setAccessPointReady(bool ready) { apReady_ = ready; }
  void setApiReady(bool ready) { apiReady_ = ready; }
  bool accessPointReady() const { return apReady_; }
  bool apiReady() const { return apiReady_; }
  bool ready() const { return apReady_ && apiReady_; }

 private:
  uint32_t lastAttemptMs_ = 0;
  bool hasAttempt_ = false;
  bool apReady_ = false;
  bool apiReady_ = false;
};

}  // namespace sntch_fw
