# FalkosKampf

Vanilla-Animationen in neuen Kampfsituationen. Kein Pandora, kein Nemesis, kein FNIS, kein ESP.

**Status 0.2.0:** implementiert. Gebaut erst, wenn der CI-Lauf grün ist. **Im Spiel noch nicht bestätigt.**

## Was diese Version macht

1. **Block → verwundbar:** Blockst du einen Nahkampftreffer, taumelt der Angreifer 3 Sekunden
   (Vanilla-Taumeln, wird wiederholt ausgelöst).
2. **Riposte-Animation (OAR):** Solange ein verwundbarer Gegner vor dir steht (höchstens etwa
   4 m, grob in Blickrichtung) und du ein Einhandschwert, eine Einhandaxt oder einen
   Streitkolben gezogen hast, ersetzt OAR deinen normalen Einhand-Angriff im Stand durch den
   Vanilla-Zweihand-Schlag (`2hm_attackright` / `2hm_attackleft`).
3. **Riposte-Schaden:** Dein erster Treffer auf den verwundbaren Gegner macht doppelten Schaden
   und beendet die Verwundbarkeit.
4. **Finisher:** Würde die Riposte einen menschenähnlichen Gegner töten, startet stattdessen ein
   Vanilla-Killmove passend zu deiner Waffe. Am Ende stirbt der Gegner normal (mit Ragdoll und
   Kill für dich). Klappt der Killmove nicht, stirbt er sofort normal.

Power-Attacken und alles andere bleiben Vanilla. NPCs können (noch) nicht parieren.

## Voraussetzungen

SKSE (AE), Address Library for SKSE Plugins, Open Animation Replacer.

## Installation (MO2)

`FalkosKampf-MO2.zip` über „Mod aus Archiv installieren“ installieren und aktivieren. Kein
Pandora-Lauf nötig. Eine alte FalkosKampf-Version vorher entfernen.

## Log

`Documents/My Games/Skyrim Special Edition/SKSE/FalkosKampf.log`
