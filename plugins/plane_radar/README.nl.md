# Vliegtuigradar

De vliegtuigen die nu rond je huis in de lucht zijn, op een radar, met foto en route van het dichtstbijzijnde.

- **Een radar die donker blijft**, in de lichte en de donkere stand, met een ring op een derde en twee derde van het
  bereik, het noorden boven en je huis in het midden. Elk toestel is een vliegtuigje in zijn vliegrichting; het
  dichtstbijzijnde licht op, met zijn naam ernaast.
- **Ze bewegen.** Elke 30 seconden komen de posities binnen, en daartussen schuift het scherm elk toestel elke seconde
  langs zijn koers op, met zijn eigen snelheid.
- **Hoe groter de tegel, hoe meer hij zegt.** Eén vak toont de radar en het dichtstbijzijnde toestel. Grotere tegels
  geven zijn route, afstand, hoogte en snelheid, en zijn foto als er plek is.
- **Tik voor de hele lucht.** De kaart toont de radar groot en een lijst van de toestellen, dichtstbij eerst. Tik op
  een toestel, op de radar of in de lijst, en je ziet zijn foto, maatschappij en type, waar het vandaan komt en
  naartoe gaat, en zijn getallen.

## Instellen

1. Voeg de plugin toe aan een scherm in de Tessera-app.
2. Zet de tegel Vliegtuigradar op een pagina.
3. Hij staat bij je huis (de zone Thuis van Home Assistant), tenzij je in de instellingen van de tegel een andere
   **Plek** kiest: een zone, of een persoon of tracker die beweegt, zodat de radar meegaat.
4. Kies het **Bereik** (10, 25 of 50 km) en de **Eenheden**: metrisch (km, m, km/h) of luchtvaart (nm, ft, kt).

## Waar de gegevens vandaan komen

| Wat | Van | Voorwaarden |
|---|---|---|
| Posities | [adsb.lol](https://adsb.lol), live ADS-B van ontvangers van vrijwilligers | Open data (ODbL) |
| Foto's | [planespotters.net](https://www.planespotters.net) | Elke foto toont zijn fotograaf en planespotters.net |
| Routes en types | [adsbdb](https://www.adsbdb.com) | Gratis |

Toestellen aan de grond, en de auto's van een vliegveld, laat de radar weg: hij toont wat vliegt. Een toestel waarvan
de transponder geen positie stuurt, zoals sommige militaire, verschijnt niet. Een route is wat het vluchtnummer meestal
vliegt; op een bijzondere dag kan het een andere zijn.

## Wat je huis verlaat

- Naar adsb.lol, elke 30 seconden zolang een tegel in beeld is: de positie van je plek afgerond op twee decimalen,
  ongeveer een kilometer, en het bereik. Verder niets.
- Naar planespotters.net en adsbdb, voor het toestel dat een tegel, de kaart of een pop-up toont: zijn
  transpondercode en vluchtnummer. Je plek gaat daar niet heen.

De Tessera-app vraagt deze diensten; het scherm zelf bereikt geen van alle.
