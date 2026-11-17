#pragma once
// Which place a new viewer takes (rtsp_server.cpp accept_). The server has a few places (MAX_VIEWERS). A viewer that
// plays keeps its place. A connection that only asked, or asked nothing yet, is a place held for nobody: Home
// Assistant's go2rtc opens several while it finds out how to play a stream, and a still it cannot make leaves one
// behind each time. Such a connection was let go only after 30 s of silence, so a few stills in a row filled every
// place and the next viewer, a person opening the video, was refused ("3 watch already", a Guition 10.1, 2026-11-16).
//
// The rule: a free place first; else the place of the connection that does not play and has been silent longest; and
// only when every place plays is a newcomer refused. Whoever really watches is never pushed out.
// Pure, so a PC tests it (tests/test_viewer_room.cpp).
#include <cstddef>
#include <cstdint>

namespace esphome::screen_video::viewer_room {

constexpr int NONE = -1;

struct Place {
  bool taken = false;         // a connection holds it
  bool playing = false;       // it asked to play and gets the stream
  uint32_t silent_since = 0;  // ms: when it connected, or last asked something
};

// The place for a new viewer, NONE when every place plays. `now` is in the same ms as silent_since, across its wrap.
inline int place_for(const Place *places, size_t count, uint32_t now) {
  int idle = NONE;
  uint32_t longest = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!places[i].taken) return static_cast<int>(i);
    if (places[i].playing) continue;
    const uint32_t silent = now - places[i].silent_since;
    if (idle == NONE || silent > longest) {
      idle = static_cast<int>(i);
      longest = silent;
    }
  }
  return idle;
}

}  // namespace esphome::screen_video::viewer_room
