#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::plane_radar {

// What is known of one aircraft beyond its position: its photo, its route and what it is. Asked by the screen for the
// aircraft it shows (fetches the screen asks for, plugin API 0.27), kept for the last few.
struct Details {
  char hex[8] = "";
  uint32_t asked[3] = {};   // the questions of its photo, its route and its aircraft
  bool answered[3] = {};
  char photo[160] = "", by[48] = "";
  char airline[48] = "", from[8] = "", from_city[40] = "", to[8] = "", to_city[40] = "";
  char maker[32] = "", model[40] = "", owner[48] = "";
};

// The plugin: an ESPHome component and a tessera::Plugin. It registers the radar tile, its card and its popup, and
// keeps the details of the aircraft its parts showed last.
class PlaneRadar : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;
  // The details of an aircraft, asked for once when they are not known yet; nullptr for no aircraft.
  const Details *details(const char *hex, const char *callsign);
  // What the tile that opens the card hands it (a card gets the tile's data, not its options).
  int card_range = 13;
  bool card_aviation = false;

 private:
  static constexpr int KEPT = 8;
  Details kept_[KEPT];
  int next_ = 0;
};

}  // namespace esphome::plane_radar
