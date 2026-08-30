#include "cwassistant/core/adif.hpp"

#include <string_view>

namespace cwassistant::core {
namespace {

void append_field(std::string& output,
                  const std::string_view name,
                  const std::string_view value) {
  if (value.empty()) {
    return;
  }
  output += '<';
  output += name;
  output += ':';
  output += std::to_string(value.size());
  output += '>';
  output += value;
  output += ' ';
}

}  // namespace

std::string to_adif(const QsoRecord& record) {
  std::string output;
  output.reserve(192);
  append_field(output, "CALL", record.callsign);
  append_field(output, "QSO_DATE", record.qso_date);
  append_field(output, "TIME_ON", record.time_on);
  append_field(output, "BAND", record.band);
  append_field(output, "MODE", record.mode);
  append_field(output, "FREQ", record.frequency_mhz);
  append_field(output, "RST_SENT", record.rst_sent);
  append_field(output, "RST_RCVD", record.rst_received);
  append_field(output, "STATION_CALLSIGN", record.station_callsign);
  output += "<EOR>";
  return output;
}

}  // namespace cwassistant::core
