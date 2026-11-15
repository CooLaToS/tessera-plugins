#include "plane_radar.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::plane_radar {

using tessera::Font;
namespace ui = tessera::ui;

// ---------------------------------------------------------------------------------------------------------------------
// The sky: the aircraft of the tile's data, where they are now, and how to say their numbers
// ---------------------------------------------------------------------------------------------------------------------

constexpr int MOST = 16;                 // the fetch's `limit`
constexpr uint32_t AIRPLANE = 0xF001D;   // mdi:airplane, drawn pointing north-east
constexpr float PI = 3.14159265f;

struct Plane {
  char hex[8] = "", flight[10] = "", reg[10] = "", type[6] = "";
  float lat = 0, lon = 0, gs = 0, track = 0, rate = 0;
  int alt = 0;          // feet
  bool ground = false, moving = false;
  // Where it is now, from the place: east and north in nautical miles, its distance and bearing.
  float east = 0, north = 0, dist = 0, bearing = 0;
};

static void copy_trim(char *out, size_t size, const char *in) {
  snprintf(out, size, "%s", in ? in : "");
  for (size_t n = strlen(out); n && out[n - 1] == ' '; --n) out[n - 1] = 0;
}

struct Sky {
  Plane p[MOST];
  int n = 0;
  float lat0 = 0, lon0 = 0;
  bool placed = false;      // the place has a position
  bool waiting = true;      // no answer yet
  bool stale = false;
  uint32_t at = 0;          // when the positions were heard, on the screen's clock

  void read(JsonObjectConst data) {
    JsonObjectConst place = data["entities"]["place"]["attributes"];
    placed = place["latitude"].is<float>() && place["longitude"].is<float>();
    if (placed) lat0 = place["latitude"].as<float>(), lon0 = place["longitude"].as<float>();
    const char *wait = data["wait"] | "";
    stale = data["stale"] | false;
    JsonArrayConst items = data["items"];
    waiting = *wait && items.isNull();
    if (items.isNull()) return;
    n = 0;
    for (JsonObjectConst it : items) {
      if (n >= MOST) break;
      Plane &q = p[n];
      q = Plane();
      copy_trim(q.hex, sizeof(q.hex), it["hex"] | "");
      copy_trim(q.flight, sizeof(q.flight), it["flight"] | "");
      copy_trim(q.reg, sizeof(q.reg), it["reg"] | "");
      copy_trim(q.type, sizeof(q.type), it["type"] | "");
      if (!it["lat"].is<float>() || !it["lon"].is<float>() || !*q.hex) continue;
      q.lat = it["lat"].as<float>(), q.lon = it["lon"].as<float>();
      q.gs = it["gs"] | 0.0f, q.track = it["track"] | 0.0f, q.rate = it["rate"] | 0.0f;
      const char *alt = it["alt"] | "";
      // What stands on the ground (an aircraft at its gate, a car of the airport) is no part of the sky.
      if (!strcmp(alt, "ground") || !*alt) continue;
      q.ground = false;
      q.alt = q.ground ? 0 : atoi(alt);
      q.moving = !q.ground && q.gs > 30;
      ++n;
    }
    at = tessera::epoch();
  }

  // Every aircraft moved along its course for the seconds since it was heard (at most a minute), and where it then is.
  void now(uint32_t epoch) {
    const float dt = at && epoch > at ? std::min<float>(60, epoch - at) : 0;
    const float k = std::cos(lat0 * PI / 180);
    for (int i = 0; i < n; ++i) {
      Plane &q = p[i];
      float lat = q.lat, lon = q.lon;
      if (q.moving && dt > 0) {
        const float miles = q.gs * dt / 3600, rad = q.track * PI / 180;
        lat += miles * std::cos(rad) / 60;
        lon += miles * std::sin(rad) / (60 * std::max(0.1f, k));
      }
      q.east = (lon - lon0) * 60 * k;
      q.north = (lat - lat0) * 60;
      q.dist = std::sqrt(q.east * q.east + q.north * q.north);
      q.bearing = std::fmod(std::atan2(q.east, q.north) * 180 / PI + 360, 360);
    }
  }

  // The aircraft a part shows when nobody chose one: the nearest in the air, else the nearest; -1 for none.
  int nearest() const {
    int best = -1;
    for (int i = 0; i < n; ++i)
      if (best < 0 || (p[i].ground == p[best].ground ? p[i].dist < p[best].dist : !p[i].ground)) best = i;
    return best;
  }
  int find(const char *hex) const {
    for (int i = 0; i < n; ++i)
      if (!strcmp(p[i].hex, hex)) return i;
    return -1;
  }
};

// The numbers in the tile's units: metric (km, m, km/h, m/s) or aviation (nm, ft, kt, ft/min).
struct Units {
  bool aviation = false;
  std::string distance(float nm) const {
    char t[16];
    const float v = aviation ? nm : nm * 1.852f;
    snprintf(t, sizeof(t), v < 10 ? "%.1f" : "%.0f", v);
    return t;
  }
  const char *distance_unit() const { return aviation ? "nm" : "km"; }
  std::string altitude(const Plane &q) const {
    char t[16];
    if (aviation) snprintf(t, sizeof(t), "%d", (q.alt + 50) / 100 * 100);
    else snprintf(t, sizeof(t), "%d", static_cast<int>(lroundf(q.alt * 0.3048f / 10)) * 10);
    return t;
  }
  const char *altitude_unit() const { return aviation ? "ft" : "m"; }
  std::string speed(const Plane &q) const {
    char t[16];
    snprintf(t, sizeof(t), "%.0f", aviation ? q.gs : q.gs * 1.852f);
    return t;
  }
  const char *speed_unit() const { return aviation ? "kt" : "km/h"; }
  std::string climb(const Plane &q) const {
    char t[16];
    if (aviation) snprintf(t, sizeof(t), "%+.0f", std::round(q.rate / 100) * 100);
    else snprintf(t, sizeof(t), "%+.1f", q.rate * 0.00508f);
    return t;
  }
  const char *climb_unit() const { return aviation ? "ft/min" : "m/s"; }
  std::string range(int nm) const {
    char t[16];
    if (aviation) snprintf(t, sizeof(t), "%d nm", nm);
    else snprintf(t, sizeof(t), "%d km", static_cast<int>(lroundf(nm * 1.852f / 5)) * 5);
    return t;
  }
};

static const char *name_of(const Plane &q) { return *q.flight ? q.flight : *q.reg ? q.reg : q.hex; }

// ---------------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------------

static int lh(Font f) { return ui::line_height(f); }
static void show(lv_obj_t *o, bool on) {
  if (!o || on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void box(lv_obj_t *o, int x, int y, int w, int h) {
  if (!o) return;
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, std::max(1, w), std::max(1, h));
}
static void move(lv_obj_t *o, int x, int y) {
  if (o && (lv_obj_get_x(o) != x || lv_obj_get_y(o) != y)) lv_obj_set_pos(o, x, y);
}
static std::string worded(const std::string &value, const char *unit) {
  return value + (unit && *unit ? std::string(" ") + unit : std::string());
}

// The radar's face: a dark disc (the instrument roles, dark in both looks) with two rings, a cross, north, the place in
// its middle, and an airplane for every aircraft turned along its course. The one it is about glows, with its name.
struct Scope {
  lv_obj_t *disc{}, *ring[2]{}, *cross[2]{}, *north{}, *home{}, *range{}, *plane[MOST]{}, *dot[MOST]{}, *tag{};
  int size = 0, glyph = 0, pip = 0;
  bool tags = false;   // the name beside the chosen one
  bool dots = false;   // a small disc: every aircraft a dot, the chosen one its airplane

  void make(lv_obj_t *parent, int x, int y, int d) {
    size = d;
    disc = ui::block(parent, theme::INSTRUMENT);
    box(disc, x, y, d, d);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    const int line = std::max(1, ui::px(1));
    for (int i = 0; i < 2; ++i) {
      cross[i] = ui::block(disc, theme::INSTRUMENT_LINE);
      lv_obj_set_style_radius(cross[i], 0, 0);
    }
    box(cross[0], 0, d / 2, d, line);
    box(cross[1], d / 2, 0, line, d);
    for (int i = 0; i < 2; ++i) {
      ring[i] = ui::block(disc, theme::INSTRUMENT);
      const int r = d * (i + 1) / 3;
      box(ring[i], (d - r) / 2, (d - r) / 2, r, r);
      lv_obj_set_style_radius(ring[i], LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(ring[i], LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(ring[i], line, 0);
    }
    home = ui::block(disc, theme::INSTRUMENT_INK);
    const int hd = std::max(4, ui::px(6));
    box(home, (d - hd) / 2, (d - hd) / 2, hd, hd);
    lv_obj_set_style_radius(home, LV_RADIUS_CIRCLE, 0);
    // North and the range only where the disc has room for words beside its aircraft.
    const bool words = d >= ui::px(110);
    if (words) {
      north = ui::label(disc, Font::BODY, theme::INSTRUMENT_MUTED);
      ui::set_text(north, "N");
      const int nw = ui::text_width("N", Font::BODY) + 2;
      box(north, (d - nw) / 2, ui::px(3), nw, lh(Font::BODY));
      range = ui::label(disc, Font::BODY, theme::INSTRUMENT_MUTED);
      lv_obj_set_style_text_align(range, LV_TEXT_ALIGN_CENTER, 0);
      box(range, 0, d - lh(Font::BODY) - ui::px(4), d, lh(Font::BODY));
    }
    // An airplane for each where the disc is large enough for their shapes to read; on a small one dots, and the
    // chosen aircraft alone its airplane.
    const Font face = Font::ICON_SMALL;
    glyph = lh(face);
    dots = d < ui::px(170);
    pip = std::max(4, ui::px(7));
    for (int i = 0; i < MOST; ++i) {
      if (dots) {
        dot[i] = ui::block(disc, theme::INSTRUMENT_INK);
        lv_obj_set_style_radius(dot[i], LV_RADIUS_CIRCLE, 0);
        box(dot[i], 0, 0, pip, pip);
        show(dot[i], false);
      }
      if (dots && i) continue;   // one airplane: the chosen one's
      plane[i] = ui::label(disc, face, theme::INSTRUMENT_INK);
      lv_obj_set_style_text_align(plane[i], LV_TEXT_ALIGN_CENTER, 0);
      ui::set_text(plane[i], ui::icon(AIRPLANE));
      box(plane[i], 0, 0, glyph, glyph);
      show(plane[i], false);
    }
    tags = d >= ui::px(130);
    if (tags) {
      tag = ui::label(disc, Font::BODY, theme::INSTRUMENT_GLOW);
      show(tag, false);
    }
    paint_colours();
  }

  void paint_colours() {
    lv_obj_set_style_bg_color(disc, ui::color(theme::INSTRUMENT), 0);
    for (auto *c : cross) lv_obj_set_style_bg_color(c, ui::color(theme::INSTRUMENT_LINE), 0);
    for (auto *r : ring) lv_obj_set_style_border_color(r, ui::color(theme::INSTRUMENT_LINE), 0);
    lv_obj_set_style_bg_color(home, ui::color(theme::INSTRUMENT_INK), 0);
    if (north) ui::set_color(north, theme::INSTRUMENT_MUTED);
    if (range) ui::set_color(range, theme::INSTRUMENT_MUTED);
    if (tag) ui::set_color(tag, theme::INSTRUMENT_GLOW);
  }

  // Where an aircraft stands on the disc, in its pixels; false when it lies beyond the range.
  bool spot(const Plane &q, int range_nm, int &x, int &y) const {
    const float r = (size / 2.0f) - glyph / 2.0f;
    if (q.dist > range_nm * 1.02f) return false;
    x = size / 2 + static_cast<int>(lroundf(q.east / range_nm * r));
    y = size / 2 - static_cast<int>(lroundf(q.north / range_nm * r));
    return true;
  }

  // `label`: the words beside the chosen one, "" for its flight.
  void paint(const Sky &sky, int range_nm, int chosen, const Units &units, const std::string &label = "") {
    if (range) ui::set_text(range, units.range(range_nm));
    int tag_x = -1, tag_y = -1;
    for (int i = 0; i < MOST; ++i) {
      int x = 0, y = 0;
      const bool on = i < sky.n && spot(sky.p[i], range_nm, x, y);
      const Plane *q = on ? &sky.p[i] : nullptr;
      if (dots) {
        show(dot[i], on && i != chosen);
        if (on && i != chosen) {
          move(dot[i], x - pip / 2, y - pip / 2);
          lv_obj_set_style_bg_color(dot[i], ui::color(theme::INSTRUMENT_INK), 0);
        }
        if (on && i == chosen) tag_x = x, tag_y = y;
        continue;
      }
      show(plane[i], on);
      if (!on) continue;
      move(plane[i], x - glyph / 2, y - glyph / 2);
      // The glyph points north-east: a course of 45 degrees is as it is drawn.
      ui::rotate(plane[i], q->track - 45);
      ui::set_color(plane[i], i == chosen ? theme::INSTRUMENT_GLOW : theme::INSTRUMENT_INK);
      if (i == chosen) tag_x = x, tag_y = y;
    }
    // On a small disc the chosen one is the one airplane.
    if (dots) {
      show(plane[0], tag_x >= 0);
      if (tag_x >= 0) {
        move(plane[0], tag_x - glyph / 2, tag_y - glyph / 2);
        ui::rotate(plane[0], sky.p[chosen].track - 45);
        ui::set_color(plane[0], theme::INSTRUMENT_GLOW);
      }
    }
    // The chosen one in front, its name beside it on the side with room.
    lv_obj_t *front = dots ? plane[0] : chosen >= 0 && chosen < MOST ? plane[chosen] : nullptr;
    if (front && tag_x >= 0) lv_obj_move_foreground(front);
    if (!tag) return;
    show(tag, tag_x >= 0);
    if (tag_x < 0) return;
    const std::string name = label.empty() ? std::string(name_of(sky.p[chosen])) : label;
    ui::set_text(tag, name);
    const int tw = ui::text_width(name, Font::BODY) + 2, gap = glyph / 2 + ui::px(4);
    const int x = tag_x + gap + tw <= size - ui::px(4) ? tag_x + gap : tag_x - gap - tw;
    box(tag, std::max(0, x), std::clamp(tag_y - lh(Font::BODY) / 2, 0, size - lh(Font::BODY)), tw, lh(Font::BODY));
    lv_obj_move_foreground(tag);
  }

  // The aircraft nearest a point of the disc (its own pixels), -1 for none within a finger's reach.
  int at(const Sky &sky, int range_nm, int px, int py) const {
    int best = -1, best_d = ui::mm(9) * ui::mm(9);
    for (int i = 0; i < sky.n; ++i) {
      int x = 0, y = 0;
      if (!spot(sky.p[i], range_nm, x, y)) continue;
      const int d = (x - px) * (x - px) + (y - py) * (y - py);
      if (d < best_d) best_d = d, best = i;
    }
    return best;
  }
};

// The words beside the chosen aircraft on the radar: where it goes ("to Chicago"), else nothing (its flight).
static std::string toward(const tessera::Plugin *plugin, const Details *d, int width) {
  if (!d || !*d->to) return "";
  std::string said = tessera::fill(plugin->text("toward"), "to", *d->to_city ? d->to_city : d->to);
  if (width > 0 && ui::text_width(said, Font::BODY) > width) said = tessera::fill(plugin->text("toward"), "to", d->to);
  return said;
}

// The photographer's credit in the words that fit: with the site, else the name.
static std::string credit_of(const tessera::Plugin *plugin, const Details *d, int width) {
  if (!d || !*d->by) return "";
  const std::string full = tessera::fill(plugin->text("photo_by"), "name", d->by);
  return width <= 0 || ui::text_width(full, Font::BODY) <= width ? full : tessera::fill(plugin->text("photo_by_short"), "name", d->by);
}

// Where an aircraft flies as words that fit `width` in `face`: its cities ("Milan → Chicago"), else its airports
// ("MXP → ORD"); "" while its route is not known.
static std::string route_of(const tessera::Plugin *plugin, const Details *d, Font face, int width) {
  if (!d || !*d->from || !*d->to) return "";
  // An arrow where the screen's font has one, else the words of the screen's language ("Milan to Chicago"); the
  // airports' codes with a dash where the cities do not fit.
  const bool arrow = ui::covers("\u2192", face);
  if (*d->from_city && *d->to_city) {
    const std::string cities = arrow ? std::string(d->from_city) + " \u2192 " + d->to_city
                                     : tessera::fill(tessera::fill(plugin->text("from_to"), "from", d->from_city).c_str(), "to", d->to_city);
    if (width <= 0 || ui::text_width(cities, face) <= width) return cities;
  }
  return std::string(d->from) + (arrow ? " \u2192 " : " \u2013 ") + d->to;
}
// What it is, in a few words: its airline, else its maker and model, else its type code.
static std::string kind_of(const Details *d, const Plane &q) {
  if (d && *d->airline) return d->airline;
  if (d && *d->model) return std::string(d->maker) + (*d->maker ? " " : "") + d->model;
  return q.type;
}

// The words about one aircraft: where it flies first, then its flight and airline, then its numbers; a photo above
// them where there is room, and how many aircraft the radar shows where the column has a line to spare.
struct About {
  lv_obj_t *head{}, *sub{}, *stat[3]{}, *caption[3]{}, *photo{}, *credit{}, *count{};
  bool grid = false;   // the numbers as three rows with their captions, else one line
  int x = 0, y = 0, w = 0, h = 0, lift = 0;   // lift: how far the words move up while there is no photo
  bool lifted = false;
  int base[9] = {};   // where each word stands under a photo (kept here: LVGL lays out later)
  Font head_face = Font::TITLE;

  void make(lv_obj_t *parent, int x_, int y_, int w_, int h_, bool with_photo) {
    x = x_, y = y_, w = w_, h = h_;
    const int lb = lh(Font::BODY), lt = lh(Font::TITLE), lhd = lh(Font::HEADLINE), g = ui::px(ui::large() ? 8 : 5);
    const int rows_h = 3 * (lt + ui::px(2));
    head_face = w >= ui::px(150) && h >= lhd + 2 * lb ? Font::HEADLINE : Font::TITLE;
    const int line_words = lh(head_face) + 2 * lb;   // head, sub, numbers on one line
    int top = y;
    // The photo first, at its own shape (3 by 2), where it leaves room for the words under it.
    if (with_photo && ui::pictures()) {
      const int ph = std::min(w * 2 / 3, h - line_words - lb - g);
      if (ph >= ui::px(60)) {
        const int pw = std::min(w, ph * 3 / 2);
        photo = ui::picture(parent);
        box(photo, x, top, pw, ph);
        lv_obj_set_style_radius(photo, ui::px(ui::large() ? 10 : 6), 0);
        credit = ui::label(parent, Font::BODY, theme::SUBTLE);
        box(credit, x, top + ph + ui::px(1), w, lb);
        lift = ph + lb + g;
        top += lift;
      }
    }
    head = ui::label(parent, head_face, theme::INK);
    box(head, x, top, w, lh(head_face));
    top += lh(head_face);
    sub = ui::label(parent, Font::BODY, theme::MUTED);
    box(sub, x, top, w, lb);
    top += lb;
    grid = y + h - top >= g / 2 + rows_h;
    if (grid) {
      top += g / 2;
      // A caption and its value stay near each other on a wide tile: the rows are at most so wide.
      const int gw = std::min(w, ui::px(ui::large() ? 280 : 180));
      for (int i = 0; i < 3; ++i) {
        caption[i] = ui::label(parent, Font::BODY, theme::MUTED);
        box(caption[i], x, top + i * (lt + ui::px(2)) + (lt - lb), gw * 2 / 5, lb);
        stat[i] = ui::label(parent, Font::TITLE, theme::INK);
        lv_obj_set_style_text_align(stat[i], LV_TEXT_ALIGN_RIGHT, 0);
        box(stat[i], x + gw * 2 / 5, top + i * (lt + ui::px(2)), gw - gw * 2 / 5, lt);
      }
      top += rows_h;
    } else {
      stat[0] = ui::label(parent, Font::BODY, theme::INK_SOFT);
      box(stat[0], x, top, w, lb);
      show(stat[0], top + lb <= y + h);
      top += lb;
    }
    // How many, last and only with a line to spare.
    if (y + h - top >= lb + g / 2) {
      count = ui::label(parent, Font::BODY, theme::SUBTLE);
      box(count, x, top + g / 2, w, lb);
    }
    lv_obj_t *const words[9] = {head, sub, stat[0], stat[1], stat[2], caption[0], caption[1], caption[2], count};
    for (int i = 0; i < 9; ++i) base[i] = words[i] ? static_cast<int>(lv_obj_get_style_y(words[i], LV_PART_MAIN)) : 0;
  }

  // Every word up by the photo's room while no photo shows, and back down when one does.
  void slide(bool photo_shows) {
    if (!lift || photo_shows == !lifted) return;
    lifted = !photo_shows;
    lv_obj_t *const words[9] = {head, sub, stat[0], stat[1], stat[2], caption[0], caption[1], caption[2], count};
    for (int i = 0; i < 9; ++i)
      if (words[i]) lv_obj_set_y(words[i], base[i] - (lifted ? lift : 0));
  }

  void paint(const tessera::Plugin *plugin, const Sky &sky, int chosen, const Units &units, int range_nm, const Details *d) {
    const bool any = chosen >= 0;
    const bool pictured = any && d && *d->photo;
    if (photo) {
      ui::set_picture(photo, pictured ? d->photo : "");
      show(photo, pictured);
      show(credit, pictured);
      ui::set_text(credit, pictured ? credit_of(plugin, d, w) : "");
      slide(pictured);
    }
    if (count) {
      int within = 0;   // what the radar shows: the aircraft within its range of the place
      for (int i = 0; i < sky.n; ++i)
        if (sky.p[i].dist <= range_nm * 1.02f) ++within;
      ui::set_text(count, sky.placed && !sky.waiting && any ? tessera::format(plugin->text("nearby"), within) : "");
    }
    if (!any) {
      ui::set_font(head, head_face);
      ui::set_text(head, plugin->text(!sky.placed ? "no_place" : sky.waiting ? "waiting" : "clear"));
      ui::set_text(sub, sky.placed && !sky.waiting ? tessera::fill(plugin->text("clear_hint"), "range", units.range(range_nm)) : "");
      for (int i = 0; i < 3; ++i) {
        if (stat[i]) ui::set_text(stat[i], "");
        if (caption[i]) ui::set_text(caption[i], "");
      }
      return;
    }
    const Plane &q = sky.p[chosen];
    // The head: where it flies, in the largest face it fits; its flight and airline under it. Without a known route
    // the flight is the head and what it is the line under it.
    // The cities in the large face, else the cities in a smaller one, else the airports' codes.
    Font face = head_face;
    std::string route = route_of(plugin, d, face, w);
    const std::string codes = route_of(plugin, d, face, 1);
    if (!route.empty() && (route == codes || ui::text_width(route, face) > w) && face == Font::HEADLINE) {
      const std::string smaller = route_of(plugin, d, Font::TITLE, w);
      if (smaller != route_of(plugin, d, Font::TITLE, 1) || ui::text_width(route, face) > w) face = Font::TITLE, route = smaller;
    }
    ui::set_font(head, face);
    std::string under;
    if (!route.empty()) {
      ui::set_text(head, route);
      under = name_of(q);
      const std::string kind = kind_of(d, q);
      if (!kind.empty() && ui::text_width(under + "  ·  " + kind, Font::BODY) <= w) under += "  ·  " + kind;
    } else {
      ui::set_text(head, name_of(q));
      under = kind_of(d, q);
      if (ui::text_width(under, Font::BODY) > w && d && *d->model) under = d->model;
      if (ui::text_width(under, Font::BODY) > w) under = q.type;
    }
    ui::set_text(sub, under);
    const std::string dist = worded(units.distance(q.dist), units.distance_unit());
    const std::string alt = worded(units.altitude(q), units.altitude_unit());
    const std::string speed = worded(units.speed(q), units.speed_unit());
    if (grid) {
      const char *keys[3] = {"distance", "altitude", "speed"};
      const std::string values[3] = {dist, alt, speed};
      for (int i = 0; i < 3; ++i) {
        ui::set_text(caption[i], plugin->text(keys[i]));
        ui::set_text(stat[i], values[i]);
      }
    } else if (stat[0]) {
      std::string line = dist + "  ·  " + alt;
      if (ui::text_width(line + "  ·  " + speed, Font::BODY) <= w) line += "  ·  " + speed;
      if (ui::text_width(line, Font::BODY) > w) line = dist;
      ui::set_text(stat[0], line);
    }
  }

  void paint_colours() {
    if (head) ui::set_color(head, theme::INK);
    if (sub) ui::set_color(sub, theme::MUTED);
    if (count) ui::set_color(count, theme::SUBTLE);
    for (auto *l : caption) if (l) ui::set_color(l, theme::MUTED);
    for (auto *l : stat) if (l) ui::set_color(l, grid ? theme::INK : theme::INK_SOFT);
    if (credit) ui::set_color(credit, theme::SUBTLE);
  }
};

// ---------------------------------------------------------------------------------------------------------------------
// The popup: one aircraft, large
// ---------------------------------------------------------------------------------------------------------------------

static Sky popup_sky;
static int popup_index = -1;
static Units popup_units;
static PlaneRadar *popup_plugin = nullptr;

class PlanePopup : public tessera::Popup {
 public:
  void open(const tessera::PopupContext &c) override {
    plugin_ = popup_plugin;
    sky_ = popup_sky;
    units_ = popup_units;
    if (popup_index < 0 || popup_index >= sky_.n) return;
    plane_ = sky_.p[popup_index];
    const bool large = ui::large();
    const int g = ui::px(large ? 12 : 8), lb = lh(Font::BODY), lt = lh(Font::TITLE), lhd = lh(Font::HEADLINE);
    width_ = c.width;
    // The first row: an airplane on the instrument's ground as large as the close key, where it flies, and its flight
    // and airline under that.
    disc_ = ui::block(c.parent, theme::INSTRUMENT);
    box(disc_, 0, 0, c.key, c.key);
    lv_obj_set_style_radius(disc_, LV_RADIUS_CIRCLE, 0);
    const Font glyph = lh(Font::ICON) <= c.key * 3 / 4 ? Font::ICON : Font::ICON_SMALL;
    icon_ = ui::label(disc_, glyph, theme::INSTRUMENT_GLOW);
    lv_obj_set_style_text_align(icon_, LV_TEXT_ALIGN_CENTER, 0);
    box(icon_, (c.key - lh(glyph)) / 2, (c.key - lh(glyph)) / 2, lh(glyph), lh(glyph));
    ui::set_text(icon_, ui::icon(AIRPLANE));
    ui::rotate(icon_, plane_.track - 45);
    const int tx = c.key + g;
    head_w_ = c.width - tx - c.corner;
    head_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    box(head_, tx, std::max(0, (c.key - lhd - lb) / 2), head_w_, lhd);
    sub_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    box(sub_, tx, std::max(0, (c.key - lhd - lb) / 2) + lhd, head_w_, lb);
    int y = std::max(c.key, lhd + lb) + g;
    // The photo, at its own shape, as large as the popup allows within the glass.
    if (ui::pictures()) {
      const int ph = std::min(c.width * 2 / 3, c.height - y - lb - 2 * (lt + lb) - 2 * g);
      if (ph >= ui::px(80)) {
        const int pw = std::min(c.width, ph * 3 / 2), r = ui::px(large ? 12 : 8);
        // Under the photo, and in its place while there is none: the aircraft on the instrument's ground.
        stage_ = ui::block(c.parent, theme::INSTRUMENT);
        box(stage_, (c.width - pw) / 2, y, pw, ph);
        lv_obj_set_style_radius(stage_, r, 0);
        big_ = ui::label(stage_, Font::DIGITS, theme::INSTRUMENT_MUTED);
        const Font face = lh(Font::ICON) * 2 <= ph ? Font::ICON : Font::ICON_SMALL;
        ui::set_font(big_, face);
        lv_obj_set_style_text_align(big_, LV_TEXT_ALIGN_CENTER, 0);
        box(big_, (pw - lh(face)) / 2, (ph - lh(face)) / 2, lh(face), lh(face));
        ui::set_text(big_, ui::icon(AIRPLANE));
        ui::rotate(big_, plane_.track - 45);
        photo_ = ui::picture(c.parent, tessera::Placeholder::NONE);
        box(photo_, (c.width - pw) / 2, y, pw, ph);
        lv_obj_set_style_radius(photo_, r, 0);
        credit_ = ui::label(c.parent, Font::BODY, theme::SUBTLE);
        lv_obj_set_style_text_align(credit_, LV_TEXT_ALIGN_CENTER, 0);
        box(credit_, 0, y + ph + ui::px(2), c.width, lb);
        y += ph + lb + g;
      }
    }
    // Six numbers in two rows of three: a caption over each value.
    const int cols = 3, cw = (c.width - (cols - 1) * g) / cols;
    for (int i = 0; i < 6; ++i) {
      const int cx = (i % cols) * (cw + g), cy = y + (i / cols) * (lb + lt + g);
      cap_[i] = ui::label(c.parent, Font::BODY, theme::MUTED);
      box(cap_[i], cx, cy, cw, lb);
      val_[i] = ui::label(c.parent, Font::TITLE, theme::INK);
      box(val_[i], cx, cy + lb, cw, lt);
    }
    paint();
  }

  void on_tick(uint32_t) override { paint(); }
  void on_theme() override { paint(); }

 private:
  PlaneRadar *plugin_{};
  Sky sky_;
  Plane plane_;
  Units units_;
  int width_ = 0, head_w_ = 0;
  lv_obj_t *disc_{}, *icon_{}, *head_{}, *sub_{}, *stage_{}, *big_{}, *photo_{}, *credit_{}, *cap_[6]{}, *val_[6]{};

  void paint() {
    if (!head_) return;
    const Details *d = plugin_->details(plane_.hex, plane_.flight);
    lv_obj_set_style_bg_color(disc_, ui::color(theme::INSTRUMENT), 0);
    ui::set_color(icon_, theme::INSTRUMENT_GLOW);
    // Where it flies, as the head; its flight and airline under it. Without a route the flight is the head.
    std::string route = route_of(plugin_, d, Font::HEADLINE, head_w_);
    Font face = Font::HEADLINE;
    if (!route.empty() && ui::text_width(route, face) > head_w_) {
      face = Font::TITLE;
      route = route_of(plugin_, d, face, head_w_);
    }
    ui::set_font(head_, face);
    const std::string kind = kind_of(d, plane_);
    std::string under;
    if (!route.empty()) {
      ui::set_text(head_, route);
      under = name_of(plane_);
      if (!kind.empty()) under += "  ·  " + kind;
    } else {
      ui::set_text(head_, name_of(plane_));
      under = kind.empty() ? (d && *d->owner ? d->owner : "") : kind;
    }
    ui::set_text(sub_, under);
    ui::set_color(head_, theme::INK);
    ui::set_color(sub_, theme::MUTED);
    if (photo_) {
      lv_obj_set_style_bg_color(stage_, ui::color(theme::INSTRUMENT), 0);
      ui::set_color(big_, theme::INSTRUMENT_MUTED);
      ui::set_picture(photo_, d && *d->photo ? d->photo : "");
      ui::set_text(credit_, d && *d->by ? tessera::fill(plugin_->text("photo_by"), "name", d->by)
                                        : (d && d->answered[0] ? plugin_->text("no_photo") : ""));
      ui::set_color(credit_, theme::SUBTLE);
    }
    const char *keys[6] = {"altitude", "speed", "distance", "climb", "type", "registration"};
    const std::string type = d && *d->model ? std::string(d->model) : std::string(*plane_.type ? plane_.type : "–");
    const std::string values[6] = {
        worded(units_.altitude(plane_), units_.altitude_unit()),
        worded(units_.speed(plane_), units_.speed_unit()),
        worded(units_.distance(plane_.dist), units_.distance_unit()),
        std::fabs(plane_.rate) < 64 ? plugin_->text("level") : worded(units_.climb(plane_), units_.climb_unit()),
        ui::text_width(type, Font::TITLE) <= lv_obj_get_width(val_[4]) ? type : std::string(*plane_.type ? plane_.type : "–"),
        *plane_.reg ? plane_.reg : "–"};
    for (int i = 0; i < 6; ++i) {
      ui::set_text(cap_[i], plugin_->text(keys[i]));
      ui::set_color(cap_[i], theme::MUTED);
      ui::set_text(val_[i], values[i]);
      ui::set_color(val_[i], theme::INK);
    }
  }
};

static void explain(PlaneRadar *plugin, const Sky &sky, int index, const Units &units) {
  if (index < 0 || index >= sky.n) return;
  popup_sky = sky, popup_index = index, popup_units = units, popup_plugin = plugin;
  tessera::show_popup(plugin, "plane", []() -> tessera::Popup * { return new PlanePopup(); }, tessera::PopupWidth::WIDE);
}

// ---------------------------------------------------------------------------------------------------------------------
// The tile and the card
// ---------------------------------------------------------------------------------------------------------------------

class RadarTile : public tessera::Tile {
 public:
  explicit RadarTile(PlaneRadar *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    range_ = atoi(c.options["range"] | "13");
    if (range_ <= 0) range_ = 13;
    units_.aviation = !strcmp(c.options["units"] | "metric", "aviation");
    const int W = c.width, H = c.height, g = ui::px(ui::large() ? 12 : 8);
    // Lying down: the radar as high as the tile at the left, the words beside it, and a photo where the words leave
    // room for one. Standing: the radar on top, the words under it.
    if (W * 10 >= H * 13) {
      const int d = H;
      scope_.make(c.parent, 0, 0, d);
      const int x = d + g, w = W - x;
      // A photo beside the words on a tile low and wide enough, else over them where the column is high enough.
      const int text_w = ui::px(ui::large() ? 150 : 100), photo_w = (H - lh(Font::BODY)) * 3 / 2;
      if (ui::pictures() && w >= text_w + g + photo_w && H >= ui::px(90)) {
        about_.make(c.parent, x, 0, w - photo_w - g, H, false);
        side_photo_ = ui::picture(c.parent);
        box(side_photo_, W - photo_w, 0, photo_w, H - lh(Font::BODY) - ui::px(2));
        side_credit_ = ui::label(c.parent, Font::BODY, theme::SUBTLE);
        box(side_credit_, W - photo_w, H - lh(Font::BODY), photo_w, lh(Font::BODY));
      } else {
        about_.make(c.parent, x, 0, w, H, w >= ui::px(110));
      }
    } else {
      const int words = std::min(H * 55 / 100, lh(Font::HEADLINE) + 2 * lh(Font::BODY) + 3 * (lh(Font::TITLE) + ui::px(2)) + W * 2 / 3);
      const int d = std::min(W, H - words - g);
      scope_.make(c.parent, (W - d) / 2, 0, d);
      about_.make(c.parent, 0, d + g, W, H - d - g, true);
    }
    about_.paint_colours();
  }

  void on_state(JsonObjectConst data) override { sky_.read(data); }

  void on_tick(uint32_t epoch) override {
    sky_.now(epoch);
    const int chosen = sky_.nearest();
    const Details *d = chosen >= 0 ? plugin_->details(sky_.p[chosen].hex, sky_.p[chosen].flight) : nullptr;
    scope_.paint(sky_, range_, chosen, units_, toward(plugin_, d, scope_.size / 2));
    about_.paint(plugin_, sky_, chosen, units_, range_, d);
    if (side_photo_) {
      const bool has = chosen >= 0 && d && *d->photo;
      ui::set_picture(side_photo_, has ? d->photo : "");
      show(side_photo_, has);
      ui::set_text(side_credit_, has ? credit_of(plugin_, d, lv_obj_get_style_width(side_credit_, LV_PART_MAIN)) : "");
    }
  }

  void on_theme() override {
    scope_.paint_colours();
    about_.paint_colours();
    if (side_credit_) ui::set_color(side_credit_, theme::SUBTLE);
  }

  void on_tap() override {
    plugin_->card_range = range_;
    plugin_->card_aviation = units_.aviation;
    tessera::open_card(plugin_->plugin_id(), "sky", "", tile_);
  }

 private:
  PlaneRadar *plugin_;
  Sky sky_;
  Units units_;
  Scope scope_;
  About about_;
  lv_obj_t *side_photo_{}, *side_credit_{};
  int range_ = 13, tile_ = -1;
};

// The card: the radar as large as the room allows, every aircraft on it, and a list of them nearest first. A tap on
// an aircraft, on the radar or in the list, opens its popup.
class SkyCard : public tessera::Card {
 public:
  explicit SkyCard(PlaneRadar *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    range_ = plugin_->card_range;
    units_.aviation = plugin_->card_aviation;
    const int W = c.width, H = c.height, g = ui::px(ui::large() ? 14 : 8), lb = lh(Font::BODY), lt = lh(Font::TITLE);
    const bool lying = W * 10 >= H * 12;
    d_ = lying ? std::min(H, W * 55 / 100) : std::min(W, H * 55 / 100);
    sx_ = lying ? 0 : (W - d_) / 2, sy_ = lying ? (H - d_) / 2 : 0;
    scope_.make(c.parent, sx_, sy_, d_);
    lx_ = lying ? d_ + 2 * g : 0, ly_ = lying ? 0 : d_ + g;
    lw_ = W - lx_, lh_ = H - ly_;
    hint_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    box(hint_, lx_, ly_ + lh_ - lb, lw_, lb);
    row_h_ = std::max(lt + ui::px(ui::large() ? 18 : 10), ui::mm(8));
    rows_ = std::min(ROWS, std::max(0, (lh_ - lb - g) / (row_h_ + ui::px(4))));
    for (int i = 0; i < rows_; ++i) {
      Row &r = row_[i];
      const int y = ly_ + i * (row_h_ + ui::px(4));
      r.bg = ui::block(c.parent, theme::TRACK);
      box(r.bg, lx_, y, lw_, row_h_);
      const int pad = ui::px(ui::large() ? 12 : 7);
      // A narrow list keeps the name and the distance; a wide one the airplane along its course and the altitude too.
      narrow_ = lw_ < ui::px(230);
      int tx = pad;
      r.icon = ui::label(r.bg, Font::ICON_SMALL, theme::MUTED);
      lv_obj_set_style_text_align(r.icon, LV_TEXT_ALIGN_CENTER, 0);
      box(r.icon, pad, (row_h_ - lh(Font::ICON_SMALL)) / 2, lh(Font::ICON_SMALL), lh(Font::ICON_SMALL));
      ui::set_text(r.icon, ui::icon(AIRPLANE));
      show(r.icon, !narrow_);
      if (!narrow_) tx += lh(Font::ICON_SMALL) + pad;
      const Font nf = narrow_ ? Font::BODY_LARGE : Font::TITLE;
      const int split = narrow_ ? lw_ * 58 / 100 : lw_ * 45 / 100;
      r.name = ui::label(r.bg, nf, theme::INK);
      box(r.name, tx, (row_h_ - lh(nf)) / 2, split - tx, lh(nf));
      r.dist = ui::label(r.bg, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(r.dist, LV_TEXT_ALIGN_RIGHT, 0);
      box(r.dist, split, (row_h_ - lb) / 2, lw_ - split - pad, lb);
    }
    paint_colours();
  }

  void on_state(JsonObjectConst data) override { sky_.read(data); }

  void on_tick(uint32_t epoch) override {
    sky_.now(epoch);
    // The list nearest first: the data comes so, but aircraft move.
    int order[MOST];
    for (int i = 0; i < sky_.n; ++i) order[i] = i;
    std::sort(order, order + sky_.n, [this](int a, int b) { return sky_.p[a].dist < sky_.p[b].dist; });
    shown_ = std::min(rows_, sky_.n);
    for (int i = 0; i < shown_; ++i) index_[i] = order[i];
    const int chosen = sky_.nearest();
    const Details *near = chosen >= 0 ? plugin_->details(sky_.p[chosen].hex, sky_.p[chosen].flight, false) : nullptr;
    scope_.paint(sky_, range_, chosen, units_, toward(plugin_, near, d_ / 2));
    for (int i = 0; i < rows_; ++i) {
      Row &r = row_[i];
      show(r.bg, i < shown_);
      if (i >= shown_) continue;
      const Plane &q = sky_.p[index_[i]];
      // Where it flies by its airports, the way a departures board says it; its flight while that is not known.
      const Details *d = plugin_->details(q.hex, q.flight, false);
      // A width of 1 leaves the airports' codes: a list row has room for those.
      const std::string codes = route_of(plugin_, d, narrow_ ? Font::BODY_LARGE : Font::TITLE, 1);
      const std::string said = codes.empty() ? std::string(name_of(q)) : codes;
      ui::set_text(r.name, said);
      const std::string alt = q.ground ? plugin_->text("ground") : worded(units_.altitude(q), units_.altitude_unit());
      const std::string dist = worded(units_.distance(q.dist), units_.distance_unit());
      ui::set_text(r.dist, narrow_ ? dist : dist + "  ·  " + alt);
      ui::rotate(r.icon, q.track - 45);
      ui::set_color(r.icon, index_[i] == chosen ? theme::ACCENT : theme::MUTED);
    }
    ui::set_text(hint_, sky_.n ? plugin_->text("tap_plane")
                               : (sky_.waiting ? plugin_->text("waiting")
                                               : tessera::fill(plugin_->text("clear_hint"), "range", units_.range(range_))));
  }

  void on_theme() override { paint_colours(); }

  void on_tap() override {
    const tessera::Point at = tessera::tap_point();
    if (at.x < 0) return;
    int index = -1;
    if (at.x >= sx_ && at.x < sx_ + d_ && at.y >= sy_ && at.y < sy_ + d_) index = scope_.at(sky_, range_, at.x - sx_, at.y - sy_);
    else if (at.x >= lx_ && at.y >= ly_) {
      const int i = (at.y - ly_) / (row_h_ + ui::px(4));
      if (i >= 0 && i < shown_) index = index_[i];
    }
    explain(plugin_, sky_, index, units_);
  }

 private:
  static constexpr int ROWS = 8;
  struct Row {
    lv_obj_t *bg{}, *icon{}, *name{}, *dist{};
  };
  PlaneRadar *plugin_;
  Sky sky_;
  Units units_;
  Scope scope_;
  Row row_[ROWS];
  lv_obj_t *hint_{};
  int index_[ROWS] = {};
  int range_ = 13, d_ = 0, sx_ = 0, sy_ = 0, lx_ = 0, ly_ = 0, lw_ = 0, lh_ = 0, row_h_ = 0, rows_ = 0, shown_ = 0;
  bool narrow_ = false;

  void paint_colours() {
    scope_.paint_colours();
    ui::set_color(hint_, theme::MUTED);
    for (int i = 0; i < rows_; ++i) {
      lv_obj_set_style_bg_color(row_[i].bg, ui::color(theme::TRACK), 0);
      ui::set_color(row_[i].name, theme::INK);
      ui::set_color(row_[i].dist, theme::MUTED);
    }
  }
};

// ---------------------------------------------------------------------------------------------------------------------
// The plugin
// ---------------------------------------------------------------------------------------------------------------------

void PlaneRadar::setup() {
  add_tile("radar", [this]() { return new RadarTile(this); });
  add_card("sky", [this]() { return new SkyCard(this); }, true);
}

const Details *PlaneRadar::details(const char *hex, const char *callsign, bool full) {
  if (!hex || !*hex) return nullptr;
  if (kept_.size() < KEPT) kept_.resize(KEPT);
  Details *d = nullptr;
  for (auto &k : kept_)
    if (!strcmp(k.hex, hex)) d = &k;
  if (!d) {   // not known yet: in the place of the oldest
    d = &kept_[next_];
    next_ = (next_ + 1) % KEPT;
    *d = Details();
    snprintf(d->hex, sizeof(d->hex), "%s", hex);
  }
  // Each question once: the route for every aircraft a part names, its photo and type for the one it shows.
  auto ask = [this, d](int k, const char *fetch, const char *name, const char *value) {
    if (d->asked[k] || d->answered[k]) return;
    JsonDocument request;
    request["ask"] = fetch;
    request["data"][name] = value;
    d->asked[k] = tessera::send(this, request.as<JsonObjectConst>());
  };
  if (callsign && *callsign) ask(1, "fetch:route", "callsign", callsign);
  else d->answered[1] = true;
  if (full) {
    ask(0, "fetch:photo", "hex", hex);
    ask(2, "fetch:aircraft", "hex", hex);
  }
  return d;
}

void PlaneRadar::on_message(JsonObjectConst message) {
  const uint32_t re = message["re"] | 0u;
  if (!re) return;
  const bool ok = message["ok"] | false;
  JsonObjectConst r = message["result"];
  for (auto &d : kept_) {
    for (int k = 0; k < 3; ++k) {
      if (d.asked[k] != re) continue;
      d.answered[k] = true;
      if (!ok) return;
      auto put = [&r](char *out, size_t size, const char *key) { snprintf(out, size, "%s", r[key] | ""); };
      if (k == 0) put(d.photo, sizeof(d.photo), "photo"), put(d.by, sizeof(d.by), "by");
      if (k == 1)
        put(d.airline, sizeof(d.airline), "airline"), put(d.from, sizeof(d.from), "from"),
            put(d.from_city, sizeof(d.from_city), "from_city"), put(d.to, sizeof(d.to), "to"),
            put(d.to_city, sizeof(d.to_city), "to_city");
      if (k == 2) put(d.maker, sizeof(d.maker), "maker"), put(d.model, sizeof(d.model), "model"), put(d.owner, sizeof(d.owner), "owner");
      // The tiles paint it in their next tick; a card or popup in theirs.
      tessera::refresh();
      return;
    }
  }
}

}  // namespace esphome::plane_radar
