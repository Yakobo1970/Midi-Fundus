# Midi-Fundus – MIDI-Archiv mit Dublettenfinder (VST3 / Standalone)

Midi-Fundus durchsucht beliebig viele Ordner samt Unterordnern nach MIDI-Dateien und listet alles
Wissenswerte in einer großen, frei einstellbaren Tabelle: Titel, Interpret, Komponist, Genre, Jahr,
Copyright, Tonart, Skala, Tempo, Takt, Dauer, Instrumente, Liedtext und mehr. Schon beim Einlesen
findet es Dubletten – nicht am Dateinamen, sondern an den Noten, auch transponiert oder in anderem Tempo.
Läuft als VST3 in jeder DAW oder als eigenständiges Programm; Oberfläche auf Deutsch, Englisch und Französisch.

![Midi-Fundus](docs/main.png)

## Download
Windows-Installer (VST3 + Standalone): **[Releases](https://github.com/Yakobo1970/Midi-Fundus/releases/latest)** –
`Midi-Fundus-<Version>-Setup.exe` ausführen; das VST3 landet in `C:\Program Files\Common Files\VST3`.
Projektseite: https://www.undabtanzbar.de/software/midi-fundus/

| Dubletten-Ansicht | Helles Thema |
|---|---|
| ![Dubletten](docs/dups.png) | ![Hell](docs/light.png) |

## Funktionen
- **Einlesen:** beliebig viele Ordner (auch per Drag & Drop ins Fenster), rekursiv, `.mid` `.midi` `.kar` `.rmi`;
  parallel, unveränderte Dateien werden beim nächsten Mal übersprungen. Erprobt mit rund 90 000 Dateien.
- **Analyse:** Tonart, Skala, Tempo, Takt, Dauer, Spuren, Kanäle, Notenanzahl, GM-Instrumente, Liedtext (Karaoke),
  Sprache des Liedtexts und ein „Notengebirge“ als Mini-Profil jeder Datei.
- **Tabelle:** Spalten sortieren, verschieben, verbreitern, ein- und ausblenden; unter jeder Spalte ein eigenes
  Suchfeld (mehrere Wörter, `!wort` = enthält nicht, `=` = leer, `!` = nicht leer, Bereiche wie `1984-1989`),
  dazu eine Suche über alle Felder. Bewertung mit Sternen und Favoriten.
- **Direkt bearbeiten:** Titel, Interpret, Komponist, Genre, Jahr, Copyright, Sprache, Kommentar und Tonart werden
  sofort in die MIDI-Datei geschrieben (das Original wird vorher einmalig gesichert, die Noten bleiben unangetastet).
  Bedienung wie in einer Tabellenkalkulation: Pfeiltasten wandern von Zelle zu Zelle, Enter speichert und springt
  eine Zeile tiefer, mehrere markierte Zeilen lassen sich auf einmal ändern. **Rückgängig** per Rechtsklick oder Strg+Z.
- **Dubletten:** „identisch“ (Byte für Byte), „gleiche Noten“ (andere Texte/Einstellungen) und „ähnlich nn %“
  (gleiche Melodiefolgen, transpositions- und tempounabhängig). Die Dubletten-Ansicht zeigt beide Dateien als
  Notengebirge, das 12-Ton-Profil und übereinandergelegte Pianorollen: grün = in beiden Dateien, rot = nur in einer.
  „Kein Duplikat“ merkt sich Fehltreffer; gelöscht wird nur nach Rückfrage – in den Windows-Papierkorb.
- **Vorhören:** Leertaste oder Doppelklick – mit eingebautem SoundFont (GeneralUser GS), dem Windows-GM-Synth,
  eingebauten Synth-Klängen oder als MIDI an die DAW. Im Plugin auf Wunsch taktgenau mit dem DAW-Transport.
- **In die DAW:** Zeilen anfassen und auf eine Spur ziehen – auch mehrere Dateien auf einmal.
- **Recherche:** Rechtsklick → Internetsuche, automatische Suche fehlender Angaben (MusicBrainz, Wikidata)
  und ein „Deep-Modus“, der über die Melodie verwandte Dateien findet.
- **Dateien verwalten:** kopieren, verschieben (Bewertung und Daten bleiben erhalten), in den Papierkorb legen;
  Titel und Interpret aus dem Dateinamen übernehmen.
- **Oberfläche:** sechs Farbthemen, jeweils hell und dunkel; Sprache Deutsch/Englisch/Französisch (live umschaltbar);
  eingebaute Hilfe und Tooltipps über „?“.

Die Datenbank liegt in `%APPDATA%\MIDIARCHIV` und wird von Standalone und Plugin gemeinsam genutzt.

## Bauen
```
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target MIDIARCHIV_VST3 MIDIARCHIV_Standalone
```
JUCE wird unter `%USERPROFILE%\JUCE` erwartet (`-DJUCE_DIR=…` zum Überschreiben).
Installer: `ISCC installer\MIDIARCHIV.iss` (Inno Setup 7) → `dist_installer\Midi-Fundus-<Version>-Setup.exe`.

## Tests
Alle Testprogramme mit der Umgebungsvariable `MIDIARCHIV_DB` auf eine **Kopie** der Datenbank richten.
- `ArchivTest info|write <datei>`, `ArchivTest scan <db> <ordner>`, `ArchivTest groups <db>`, `ArchivTest lang`, `ArchivTest fileops <mid>`
- `UiShot shot <out.png> [breite höhe] [select n] [dupsonly] [light] [about] [keys] [clean]` – rendert die Oberfläche; `keys` prüft Tastatursteuerung und Rückgängig
- `UiShot synctest`, `UiShot voicetest`, `UiShot carrytest` – Wiedergabe, DAW-Sync, hängende Stimmen
- `HostTest <Midi-Fundus.vst3>` – lädt das VST3 wie eine DAW
- `python Tools/check_lang.py` – prüft, dass jeder Text auf Englisch und Französisch vorliegt

## Lizenz & Dank
Eigener Code: MIT-Lizenz (siehe `LICENSE`). Gebaut mit [JUCE](https://juce.com).
Enthalten: [TinySoundFont](https://github.com/schellingb/TinySoundFont) (MIT, `Source/ThirdParty/LICENSE_tsf.txt`)
und der SoundFont [GeneralUser GS](https://schristiancollins.com/generaluser.php) von S. Christian Collins
(frei, auch zur Weitergabe; `Resources/SoundFont/LICENSE_GeneralUser.txt`).
Online-Daten von [MusicBrainz](https://musicbrainz.org) und [Wikidata](https://www.wikidata.org) – nur auf Anforderung per Rechtsklick.
VST ist eine Marke der Steinberg Media Technologies GmbH.

Idee, Konzept und Regie: Kay „Yakobo“ Fiedler – umgesetzt mit Claude (Anthropic).
Kostenlos – wer mag, kann per [PayPal](https://paypal.me/yakobo) etwas spenden.
Kay „Yakobo“ Fiedler · https://www.undabtanzbar.de · https://github.com/Yakobo1970
