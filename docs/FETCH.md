# Data from a web service: fetch

A screen never fetches anything from the internet itself: its HTTP client would hold up drawing and taps, and a key
does not belong in firmware. Instead the plugin's manifest describes a fetch, the Tessera app in Home Assistant carries
it out with its own code, and the screen gets only the fields the manifest maps.

## What the app does

- It asks once per distinct question (the URL and headers with every value filled in), for every tile and every screen
  together. Two tiles with the same stop share one ask; so do the departures and the list of lines of the bus plugin,
  which read the same URL.
- It asks again when the answer is older than the fetch's `every`, and only while a tile on some screen uses it (or the
  editor asks for a list of choices).
- When the answer changed, it sends the tiles that show it their new data. The screen draws them the next time they
  are on the glass.
- When the service fails, the tiles keep the last good answer with `"stale": true`, and the app waits 1, 2, 5 and then
  15 minutes before it asks again.

## The rules

- **Hosts**: only those in `permissions.network`, by name. Never an address, never a name on the home network, and the
  address a name resolves to is checked again when the app connects, so a DNS answer cannot point it at a router or at
  Home Assistant.
- **https**, or plain http only for a fetch without a secret in it.
- **No redirects** are followed.
- **At most 64 KB** of answer, in **10 seconds**, and it must be **JSON**.
- **Every 30 seconds** at the most often.
- **Secrets** (`inputs` of kind `secret`) go into a header or the query, never the URL path, and never appear in a log,
  in the editor or on a screen.

## Placeholders

A URL and the headers can hold `{name}`:

- an option of a tile that uses the fetch (`{stop}` is the tile's option `stop`), through `data` or `options_from`;
- an input of kind `secret` (`{api_key}`). A secret with `scope: all` is the same for every screen; one with
  `scope: screen` is the one filled in for the tile's screen;
- an attribute of the entity a person chose for an entity option (0.27): `{place.latitude}`, or `{place.latitude:2}`
  rounded to two decimals ("An attribute of an entity a person chooses", below);
- a name of `asked`, in a fetch the screen asks for (0.27, "A fetch the screen asks for", below).

The check accepts the id of any input here, but the app fills in only options and secrets: a value a fetch needs that
is not a secret is an option of the tile.

A value in the URL is percent-encoded. A placeholder with no value means no ask: the tile gets
`{"wait": "not_filled"}`. Every value goes in as one word (0.12): `true` or `false` for a toggle, a whole number without
`.0`, a pick as its value, whether the screen's own loop or the editor asks.

A list option (`multiple`, 0.12) goes into a URL in one of two ways:

- `{tickers|,}` joins its values by the separator after the bar (1 to 3 characters), each value percent-encoded, for a
  service that takes several in one ask: `https://api.example.org/quotes?symbols={tickers|,}` asks for
  `symbols=AAPL,ASML`. An empty list is no value.
- `each: tickers` asks once per value (below).

A list in a plain `{tickers}` is refused by the check, and so is a list in `where`, `skip` or a path of `items`.
`{search}` is what a person types for a choice with `search` (below).

```yaml
fetch:
  - id: prices
    url: https://api.example.org/v1/prices?area={area}
    headers: { Authorization: "Bearer {api_key}" }
    every: 15m
    map: ...
```

## Once per value: `each`

A service that answers for one thing at a time (the releases of one artist, the departures of one stop) and a tile that
shows several: `each` names a list option, and the app asks once per value, with `{<option>}` in the URL that value.

```yaml
fetch:
  - id: releases
    each: artists                      # an option with multiple, of every tile that uses this fetch
    url: https://api.deezer.com/artist/{artists}/albums?limit=25
    every: 6h
    map:
      items: "$.data[*]"
      fields:
        title: title
        date: { path: release_date, as: epoch }
        cover: md5_image
        artist: { each: label }        # the pick whose ask brought the item: its label
      sort: -date
      limit: 16
```

- Each filled-in URL is a question of its own, as always: two tiles that share an artist share its ask and its answer.
- The map runs on each answer, with `where` and `skip`; then the items of all answers are one list, in the order of the
  values, and `sort` and `limit` work on that list: the newest 16 releases of every artist together. A map of one object
  (fields without `items`) gives one item per value, so a fetch of the weather in one city makes a list of cities.
- `{ each: value }` and `{ each: label }` as a field are the value whose ask brought the item, and the words of its pick
  (for a list of texts, the text twice): many answers do not name what was asked for.
- A value whose ask failed is left out and the list comes with `"stale": true`; one still on its way is left out until
  its answer is there. The tile gets `"wait"` only when no value has an answer, and `not_filled` while the list is empty.
- At most as many asks as the list holds (16 at most), each at most every `every`, in turn.

## An attribute of an entity a person chooses (0.27)

A service that answers for a place, a device or a value Home Assistant knows (the weather where someone is, the air
near a zone): the URL takes an attribute of the entity a person chose for one of the tile's options of kind `entity`.

```yaml
tiles:
  - id: now
    data: forecast
    options:
      - { id: place, kind: entity, domains: [zone, person, device_tracker], attributes: [latitude, longitude],
          default: zone.home, label: place }
fetch:
  - id: forecast
    url: https://api.example.org/forecast?lat={place.latitude:2}&lon={place.longitude:2}
    every: 10m
    map: { fields: { temp: { path: current.temperature, as: number } } }
```

- `{<option>.<attribute>}` is the attribute of the entity chosen for `<option>`, as Home Assistant has it now;
  `{<option>.state}` is its state. `<option>` is an option of kind `entity` of every tile whose `data` is the fetch,
  and the attribute is `state` or one of that option's `attributes`. The check refuses anything else, and refuses such
  a placeholder in a fetch that fills a list of choices (the editor asks those, without a tile's entity).
- `{<option>.<attribute>:<decimals>}`, 0 to 6, rounds a number to that many decimals and writes exactly that many:
  `52.3676` with `:2` is `52.37`, `4.9` is `4.90`. Rounding keeps a question the same while a place moves a little,
  so it is shared and cached, and it sends the service no more than it needs: two decimals is about a kilometre.
  A value that is no number (a text such as `"north"`) makes no ask.
- No entity chosen, an entity Home Assistant does not have or calls `unavailable`, an attribute it lacks, or a state
  that is `unknown`: no ask, and the tile gets `{"wait": "not_filled"}`, as for an empty option.
- The value is percent-encoded in the URL like every other, and goes as it is into a header. It never stands in the
  host, a user or a port: nothing filled in may stand before the path of the URL. A secret keeps its rules.
- An entity option may have a `default` (0.27), an entity id of one of its `domains` such as `zone.home`, so the tile
  works before anyone opens its settings ([MANIFEST.md](MANIFEST.md), "Tiles").
- **When the attribute changes**, the URL changes, so it is another question. The app asks it within one turn of its
  loop (every 15 seconds), but never more often than the fetch's `every`: each tile has one line of questions for its
  fetch, and a new question of that line replaces the old one instead of being added to it. While `every` has not
  passed since the line last asked the service, the tile keeps the answer it has. A question another tile has already
  asked is taken from the cache at once, as always. A person who chooses another entity, or changes another option,
  starts a new line, which is asked at once. So someone who moves all day is asked about at most once per `every`.

**What it sends.** Such a fetch sends something of the person's Home Assistant to a web service: a place, a state.
The app works out from the manifest, for each fetch, its host and the attributes it sends, and shows them in the
plugin's details under **What it may do**: "Sends to api.example.org: latitude and longitude of the entity you choose
for Place" (the option's label). It is part of what a person agrees to: an update that sends an attribute it did not
send before, or sends to another host, asks for their yes again. Another name of the fetch, or other decimals, do not.
Round as much as the service allows, and say in the plugin's privacy statement what goes where.

## A fetch the screen asks for (0.27)

A plugin's C++ can ask the app for one of its own fetches, with values of its own: the details of a row a finger
tapped, the route of the flight it shows. The fetch names those values in `asked`, and its URL and headers use them as
placeholders, besides secrets and inputs.

```yaml
fetch:
  - id: details
    asked: [row, day]                  # 1 to 4 names, each used in the URL or a header
    url: https://api.example.org/rows/{row}?day={day}
    every: 5m
    map:
      fields:
        title: title
        at: { path: when, as: epoch }
```

```cpp
JsonDocument request;
request["ask"] = "fetch:details";
request["data"]["row"] = "A1";
request["data"]["day"] = 3;                                      // a number goes as its text, "3"
asked_ = tessera::send(this, request.as<JsonObjectConst>());
// on_message: {"re": asked_, "ok": true, "result": {"title": "...", "at": 1792389600}}
//          or {"re": asked_, "ok": false, "error": "refused" | "not_filled" | "failed" | "too_often"}
```

[FIRMWARE_API.md](FIRMWARE_API.md), "Questions to the app", has the rest of `tessera::send` and `on_message`.

- A fetch with `asked` is no tile's `data`, no screensaver's and no option's `options_from`, and uses no tile's
  options: there is no tile. A name of `asked` is not also the id of an input. Otherwise it keeps every rule of a fetch:
  a fixed host of `permissions.network`, `every`, a `map` with `fields` (and `items` for a list), 64 KB, 10 seconds, no
  redirects. It needs no entry in `permissions.ha_commands`: its host is already in `permissions.network`.
- The screen gives every name of `asked`, and no other. Each value is a text of 1 to 64 characters of `A-Z`, `a-z`,
  `0-9`, `_`, `.`, `:`, `+`, `-` and the space; a number is taken as its text (`3`, not `3.0`). Anything else is refused.
- The answer's `result` is what a tile of that fetch would get, through the same `map`, bounded like the answer of a
  command (long texts cut, lists shortened to fit a message), with `"stale": true` when the service failed since.
- It is a question like every other: the same filled-in URL and headers is one ask, shared with every screen; an
  answer younger than `every` comes from the cache without asking the service; a failure waits 1, 2, 5 and 15 minutes.
- At most 20 such asks per plugin per screen in a minute; the 21st gets `too_often`. Each one is logged with the
  screen, the plugin and the fetch, never its values or a secret.
- When it does not work, `ok` is `false` and `error` one of: `refused` (no such fetch, a fetch without `asked`, a name
  missing or unknown, a value that is not allowed), `not_filled` (a secret it needs is not filled in), `failed` (the
  service failed, or its answer did not fit the map) or `too_often`.

## The map

`map` says what of the answer reaches the screen. Everything else is dropped in the app.

### Paths

A small path language, on purpose without filters or expressions:

| Step | Means |
|---|---|
| `$` | The whole answer (a path in `items` starts with it). |
| `.name` | A field of an object. |
| `.{option}` | A field named by an option's value (OVapi keys its answer by the stop code). |
| `[*]` | Every item of a list, or every value of an object. |
| `[3]` | One item of a list. |
| `[:5]` | The first five items. |

A field path (inside `fields`, `value`, `label`) starts at an item, without `$`: `name`, `departure.time`, `stops[0]`.
It may also start with a list step, for an answer that files its list under a name you cannot know in
advance: `[*][*].price` reads every price of Nord Pool's `{"NL": [...]}`.

The same paths and kinds work in a tile's `fields` (the attributes of its entity) and in `answers` (the answer of a
Home Assistant command), see [MANIFEST.md](MANIFEST.md).

### A list: `items` with `fields`

```yaml
map:
  items: "$.{stop}.Passes[*]"          # where the list is
  fields:                               # each item becomes { line, to, at }
    line: LinePublicNumber
    to: DestinationName50
    at: { path: ExpectedDepartureTime, as: epoch, tz: Europe/Amsterdam }
  where: { line: "{line}" }             # keep only items whose field equals; an empty option keeps all
  skip: { state: [PASSED, CANCEL] }     # drop items whose field is one of these
  sort: at                              # soonest first (-at for the other way round)
  limit: 6                              # at most this many (48 at the most)
```

The screen gets `{"items": [{"line": "15", "to": "Station Sloterdijk", "at": 1791386619}, ...]}`.

### One object: `fields` alone

```yaml
map:
  fields:
    price: { path: current.price, as: number }
    unit: current.unit
```

The screen gets `{"price": 0.23, "unit": "EUR/kWh"}`.

### A field

A field is a path, or `{ path, as, tz }`:

| `as` | The screen gets |
|---|---|
| `text` (default) | Text, at most 48 bytes. |
| `number` | A number (a text such as `"0,23"` is read as one). |
| `epoch` | Seconds since 1970, from a number (seconds or milliseconds) or an ISO 8601 time. A time without a zone is read in `tz` (`Europe/Amsterdam`), or in Home Assistant's time zone without one. |
| `numbers` | Every value the path reaches, in order, as one list of numbers: `prices[*].price` of 96 objects is 96 numbers. A value that is not a number is `null`, so a slot keeps its place. |
| `url` (0.28) | A web address whole, up to 255 bytes: a picture's, for `ui::set_picture`. Only `http://` or `https://`; anything else, or longer, is `null`. A text field would cut it at 48 bytes into an address of nothing. |

At most 12 fields (8 before 0.27; a manifest with more names `api: "0.27"`). The bytes still decide what travels: more
fields leave room for fewer items. A field that is missing in an item is `null`.

### A list of choices: `value` and `label`

For an option `{ kind: choice, options_from: <fetch> }`: the editor's inspector shows a list filled by the app.

```yaml
map:
  items: "$.{stop}.Passes[*]"
  value: LinePublicNumber                          # what the option stores
  label: [LinePublicNumber, DestinationName50]     # what the list shows, joined with " · "
```

`value` and `label` may each be one path or a list of paths; `label` is `value` when left out. Each value appears
once, at most 48 of them. The editor asks for the list with the tile's other options filled in (the
stop of the bus tile), so a list can depend on them.

**Found by searching** (0.12). An option with `search: true` asks its fetch with what the person types as `{search}`:
from two characters on, a moment after the last key. A short question with an empty `{search}` is no ask. The app keeps
those answers ten minutes, whatever the fetch's `every`.

```yaml
fetch:
  - id: artist_search
    url: https://api.deezer.com/search/artist?q={search}
    every: 1d
    map: { items: "$.data[*]", value: id, label: name }
```

## Size on the screen

A tile's data travels in one message of at most 4 KB, with the rest of the tile and its options. The app keeps the data
under about 2.6 KB, less beside options that take much of their 1280 bytes (about 2 KB at the most), by dropping the last
items of a list. Ask only for the fields the tile shows, and set `limit` to what fits on
the largest size of your tile: the bus plugin asks for six departures.

## When a service is not JSON, or needs a login

- An XML or binary service (GTFS-realtime): use the Home Assistant integration for it, and a normal tile or a plugin
  tile that belongs to its entity (`domains` in the manifest).
- OAuth: `fetch` knows fixed keys only. A Home Assistant integration that offers the data as an entity is the way.
- Data that only exists inside Home Assistant: a normal tile shows any entity; no plugin needed.

## Trying a map

The app's code is in the Tessera repository (`screen_manager/app/plugin_fetch.py`). Without a screen:

```python
import json, urllib.request, yaml, sys
sys.path.insert(0, 'path/to/homeassistant_espscreen/screen_manager/app')
import plugin_manifest, plugin_fetch
manifest = plugin_manifest.check(yaml.safe_load(open('tessera-plugin.yaml')))
fetch = manifest['fetch'][0]
data = json.load(urllib.request.urlopen('http://v0.ovapi.nl/tpc/30003025'))
print(plugin_fetch.apply_map(fetch['map'], data, {'stop': '30003025', 'line': ''}))
```
