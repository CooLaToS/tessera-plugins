# Plane radar

The aircraft in the sky around your home right now, on a radar, with the nearest one's photo and route.

- **A radar that stays dark** in the light look and the dark one, with a ring at a third and two thirds of the range,
  north at the top and your home in the middle. Every aircraft is an airplane turned along its course; the nearest
  glows, with its name beside it.
- **They move.** Positions come every 30 seconds, and in between the screen moves each aircraft along its course at its
  own speed, every second.
- **The larger the tile, the more it says.** One cell shows the radar and the nearest aircraft. Larger tiles add its
  route, its distance, altitude and speed, and its photo where there is room.
- **A tap opens the sky.** The card shows the radar large and a list of the aircraft, nearest first. A tap on one, on
  the radar or in the list, opens it: its photo, its airline and type, where it flies from and to, and its numbers.

## Set up

1. Add the plugin to a screen in the Tessera app.
2. Put the Plane radar tile on a page.
3. It stands at your home (Home Assistant's Home zone) unless you choose another **Place** in the tile's settings: a
   zone, or a person or tracker that moves, so the radar goes with them.
4. Choose the **Range** (10, 25 or 50 km) and the **Units**: metric (km, m, km/h) or aviation (nm, ft, kt).

## Where the data comes from

| What | From | Terms |
|---|---|---|
| Positions | [adsb.lol](https://adsb.lol), live ADS-B from volunteers' receivers | Open data (ODbL) |
| Photos | [planespotters.net](https://www.planespotters.net) | Each photo shows its photographer and planespotters.net |
| Routes and types | [adsbdb](https://www.adsbdb.com) | Free |

Aircraft on the ground, and an airport's own cars, are left out: the radar shows what flies. An aircraft whose
transponder sends no position, as some military ones, does not show. A route is what the airline's flight number
usually flies; on a special day it can be another.

## What leaves your home

- To adsb.lol, every 30 seconds while a tile shows: your place's position rounded to two decimals, about a kilometre,
  and the range. Nothing else.
- To planespotters.net and adsbdb, for the aircraft a tile, the card or a popup shows: its transponder code and its
  flight number. Your place does not go there.

The Tessera app asks these services; the screen itself reaches none of them.
