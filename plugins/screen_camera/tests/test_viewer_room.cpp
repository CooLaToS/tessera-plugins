// clang++ -std=c++17 -Wall -Wextra -Werror -I components tests/test_viewer_room.cpp -o /tmp/test_viewer_room && /tmp/test_viewer_room
// (from plugins/screen_camera). Which place a new viewer of the video takes (components/screen_video/viewer_room.h):
// a free one, else the one of the connection that does not play and has been silent longest, never a viewer's who plays.
#include <cstdio>

#include "screen_video/viewer_room.h"

using namespace esphome::screen_video::viewer_room;

static int failures = 0;
static void expect(int got, int want, const char *what) {
  if (got != want) {
    printf("FAIL %s: %d, not %d\n", what, got, want);
    ++failures;
  }
}

int main() {
  Place places[3];
  expect(place_for(places, 3, 1000), 0, "nobody there: the first place");
  places[0] = {true, true, 1000};
  expect(place_for(places, 3, 2000), 1, "one watches: the next free place");
  // Three connections that asked and never played (stills Home Assistant could not make): the one silent longest goes.
  places[0] = {true, false, 5000};
  places[1] = {true, false, 3000};
  places[2] = {true, false, 4000};
  expect(place_for(places, 3, 6000), 1, "all idle: the one silent longest");
  // One of them plays now: it is never the one to go, however long ago it last asked.
  places[1] = {true, true, 3000};
  expect(place_for(places, 3, 6000), 2, "a viewer who plays keeps its place");
  // Two play, one idles: the idle one.
  places[2] = {true, true, 4000};
  expect(place_for(places, 3, 6000), 0, "the only idle connection");
  // Everyone plays: no place.
  places[0] = {true, true, 5000};
  expect(place_for(places, 3, 6000), NONE, "everyone plays: refused");
  // The clock wraps: silent since just before the wrap is longer than since just after it.
  places[0] = {true, false, 0xFFFFFF00u};
  places[1] = {true, false, 100};
  places[2] = {true, true, 50};
  expect(place_for(places, 3, 200), 0, "across the clock's wrap");
  // A free place always comes before pushing an idle connection out.
  places[1] = Place{};
  expect(place_for(places, 3, 200), 1, "a free place before an idle one");
  if (failures) return 1;
  printf("PASS: a new viewer takes a free place, else the idle connection silent longest, never one that plays\n");
  return 0;
}
