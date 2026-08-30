#include <iostream>

#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/transmit_guard.hpp"

int main() {
  cwassistant::core::CallsignPolicy callsign_policy;
  cwassistant::core::TransmitGuard transmit_guard(callsign_policy);
  std::cout << "CW Assistant development host\n"
            << "TX safety state: " << transmit_guard.state_name() << '\n';
  return 0;
}
