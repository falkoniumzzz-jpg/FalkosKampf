# FalkosKampf

Vanilla-Animationen in neuen Kampfsituationen. Nur ein SKSE-Plugin: kein Pandora, Nemesis, FNIS,
OAR oder ESP.

**Status 0.3.1:** implementiert. Gebaut erst, wenn der CI-Lauf grün ist. **Im Spiel noch nicht bestätigt.**
(0.2.0 wurde im Spiel bestätigt: Block, Taumeln, Riposte im Stand, Killmove-Finisher.)

## Was diese Version macht

1. **Block → verwundbar:** Blockst du einen Nahkampftreffer, taumelt der Angreifer 3 Sekunden.
2. **Riposte:** Dein erster Treffer auf den verwundbaren Gegner macht doppelten Schaden, mit
   deinem normalen Angriff (auch in Bewegung).
3. **Killmoves 100 %:** Jeder deiner Nahkampftreffer, der einen menschenähnlichen,
   nicht-essentiellen Gegner töten würde, startet einen Vanilla-Killmove passend zu deiner Waffe,
   auch wenn noch andere Gegner da sind. Am Ende stirbt der Gegner normal (Ragdoll, Kill für dich).
   Ausnahmen: zu Pferd, oder während schon ein Killmove läuft.
4. **Freie Kamera:** Während des Killmoves verlässt das Plugin die Killmove-Kamera und gibt das
   Umsehen wieder frei.
5. **Nazeem:** Triffst du Nazeem im Nahkampf mit einem Einhandschwert, startet sofort der
   Vanilla-Killmove J (`paired_1hmkillmovej`), egal wie viel Leben er hat.

## Voraussetzungen

SKSE (AE), Address Library for SKSE Plugins.

## Installation (MO2)

Alte FalkosKampf-Version löschen, `FalkosKampf-MO2.zip` installieren und aktivieren. VioLens und
Valhalla Combat für Tests deaktivieren.

## Log

`Documents/My Games/Skyrim Special Edition/SKSE/FalkosKampf.log`
