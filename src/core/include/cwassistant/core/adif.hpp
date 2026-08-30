#pragma once

#include <string>

namespace cwassistant::core {

struct QsoRecord {
  std::string callsign;
  std::string qso_date;
  std::string time_on;
  std::string band;
  std::string mode{"CW"};
  std::string frequency_mhz;
  std::string rst_sent;
  std::string rst_received;
  std::string station_callsign;
};

// Serializes one ADIF record suitable for Log4OM's inbound ADIF UDP service.
[[nodiscard]] std::string to_adif(const QsoRecord& record);

}  // namespace cwassistant::core
