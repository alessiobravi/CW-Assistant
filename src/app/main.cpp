#include <iostream>

#include "cwassistant/core/transmit_guard.hpp"

int main() {
  cwassistant::core::TransmitGuard transmit_guard;
  std::cout << "CW Assistant development host\n"
            << "TX safety state: " << transmit_guard.state_name() << '\n';
  return 0;
}
