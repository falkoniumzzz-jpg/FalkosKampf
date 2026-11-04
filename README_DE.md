# FalkosKampf – Test 1

**Status:** implementiert. Gebaut erst, wenn der CI-Lauf grün ist. **Im Spiel noch nicht bestätigt.**

## Was dieser Test macht

- Du blockst einen Nahkampftreffer (normaler Block, kein Parierfenster). Der Angreifer taumelt
  dann 3 Sekunden lang (Vanilla-Taumel-Animation, wird wiederholt ausgelöst).
- Greifst du in dieser Zeit einen taumelnden Gegner an (rechte Angriffstaste, Waffe gezogen,
  Gegner höchstens etwa 4 m entfernt und grob vor dir), spielt statt deines normalen
  Angriffs die Finisher-Animation `fk_finisher.hkx` (eine Kopie von `2hm_attackleft.hkx`).
  Das gilt mit jeder Waffe.
- Beim Treffer-Moment der Animation (`HitFrame`) stirbt der Gegner sofort. Kommt kein
  `HitFrame`, stirbt er nach 1 Sekunde. Essentielle NPCs fallen nur auf 0 Lebenspunkte.
- Power-Attacken bleiben unverändert. Nur der Spieler kann das, NPCs nicht.

## Voraussetzungen

SKSE (AE), Address Library for SKSE Plugins, Pandora Behaviour Engine+.

## Installation (MO2)

1. `FalkosKampf-MO2.zip` über „Mod aus Archiv installieren“ installieren und aktivieren.
2. **Pandora ausführen** (über MO2 starten) und die Ausgabe wie gewohnt aktivieren. Ohne
   diesen Schritt kennt das Spiel die Finisher-Animation nicht.

## Log

`Documents/My Games/Skyrim Special Edition/SKSE/FalkosKampf.log`
