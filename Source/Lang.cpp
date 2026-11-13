#include "Lang.h"
#include "MidiData.h"
#include <atomic>
#include <cstring>
#include <unordered_map>

namespace ma
{
static std::atomic<int> gLang { (int) Lang::de };

Lang language() { return (Lang) gLang.load(); }
void setLanguage (Lang l) { gLang = (int) l; }

juce::String langCode (Lang l) { return l == Lang::en ? "en" : l == Lang::fr ? "fr" : "de"; }

bool langFromCode (const juce::String& code, Lang& l)
{
    const auto c = code.trim().toLowerCase();
    if (c.startsWith ("de") || c == "german") { l = Lang::de; return true; }
    if (c.startsWith ("en")) { l = Lang::en; return true; }
    if (c.startsWith ("fr")) { l = Lang::fr; return true; }
    return false;
}

void initLanguage (juce::ValueTree& settings)
{
    Lang l = Lang::de;
    // 1. test/override
    if (langFromCode (juce::SystemStats::getEnvironmentVariable ("MIDIARCHIV_LANG", {}), l)) { setLanguage (l); return; }
    // 2. a (new) choice in the setup wins once over the stored one
    juce::String reg;
   #if JUCE_WINDOWS
    reg = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\Software\\Yakobo\\MIDIARCHIV\\Language");
    if (reg.isEmpty()) reg = juce::WindowsRegistry::getValue ("HKEY_CURRENT_USER\\Software\\Yakobo\\MIDIARCHIV\\Language");
   #endif
    if (reg.isNotEmpty() && reg != settings.getProperty ("langSetup").toString() && langFromCode (reg, l))
    {
        settings.setProperty ("lang", langCode (l), nullptr);
        settings.setProperty ("langSetup", reg, nullptr);
    }
    // 3. stored choice, else the Windows language
    if (! langFromCode (settings.getProperty ("lang").toString(), l))
        if (! langFromCode (juce::SystemStats::getUserLanguage(), l)) l = Lang::en;
    setLanguage (l);
}

void chooseLanguage (juce::ValueTree& settings, Lang l)
{
    settings.setProperty ("lang", langCode (l), nullptr);
    setLanguage (l);
}

// ============================================================================================ texts
namespace
{
struct Row { const char* de; const char* en; const char* fr; };

const char* const kAboutDe =
    "WAS MACHT DIESES PROGRAMM?\n"
    "Midi-Fundus durchsucht beliebige Ordner samt Unterordnern nach MIDI-Dateien (.mid, .midi, .kar, .rmi) und listet alles Wissenswerte in einer "
    "großen Tabelle: Titel, Interpret, Komponist, Genre, Jahr, Copyright, Sprache, Tonart und Skala, Tempo, Takt, Dauer, Spuren, Kanäle, "
    "Notenanzahl, Instrumente, Liedtext, Bewertung und Favoriten. Schon beim Einlesen sucht es Dubletten – nicht am Dateinamen, sondern an den Noten.\n\n"
    "KURZANLEITUNG\n"
    "1. „Ordner“ → „Ordner hinzufügen …“ oder Ordner einfach ins Fenster ziehen. Danach „Einlesen“. Bereits bekannte, unveränderte Dateien werden übersprungen; "
    "die Datenbank liegt in %APPDATA%\\MIDIARCHIV und wird von Standalone und Plugin gemeinsam benutzt.\n"
    "2. Spaltenkopf anklicken = sortieren, nochmal = umgekehrt. Spaltenkopf festhalten und ziehen = Spalte verschieben, Rand ziehen = Breite. "
    "Rechtsklick auf den Spaltenkopf blendet Spalten ein und aus. Anordnung und Breiten bleiben gespeichert.\n"
    "3. Unter der Tabelle steht die Suchmaske: ein Feld je Spalte, immer genau unter ihr. Alle Felder wirken zusammen.\n"
    "4. Klick auf Titel, Interpret, Komponist, Genre, Jahr, Copyright, Sprache, Kommentar oder Tonart (Datei) öffnet die Zelle direkt zum Bearbeiten "
    "(Inhalt markiert, Cursor am Ende). Enter speichert – und zwar sofort in der MIDI-Datei selbst – und springt eine Zeile tiefer, Esc bricht ab. "
    "Mit den Pfeiltasten wandert ein Rahmen von Zelle zu Zelle; Enter oder einfach Lostippen öffnet das Feld, Tab springt zum nächsten Feld. Im offenen Feld bewegen die Pfeiltasten nur den Cursor im Text; verlassen wird es mit Enter (bzw. Tab), Esc verwirft. Rechtsklick → „Rückgängig“ (Strg+Z) nimmt die letzte Änderung zurück. "
    "Mehrfachbearbeitung: Zeilen mit Strg- bzw. Umschalt-Klick markieren – die zuletzt angeklickte Zelle wird bearbeitet, die Markierung bleibt, "
    "und Enter schreibt den Wert nach einer Rückfrage in alle markierten Dateien. Vor der ersten Änderung wird das Original einmalig nach "
    "%APPDATA%\\MIDIARCHIV\\Sicherung kopiert; nach dem Schreiben prüft Midi-Fundus, dass die Noten unverändert sind.\n"
    "5. Zeile anfassen und in die DAW ziehen = die MIDI-Datei landet auf der Spur. Mehrere markierte Zeilen gehen auch.\n"
    "6. Rechtsklick in eine Zelle: Internetrecherche, automatische Suche nach fehlenden Angaben, Deep-Modus über die Melodie, Dateien kopieren, "
    "verschieben und löschen und mehr.\n\n"
    "DUBLETTEN\n"
    "„identisch“ = Datei Byte für Byte gleich, „gleiche Noten“ = gleiche Musik mit anderen Texten/Einstellungen, „ähnlich nn %“ = gleiche Melodiefolgen, "
    "auch transponiert, in anderem Tempo oder anders arrangiert. Die Dubletten-Ansicht zeigt beide Dateien als Notengebirge (Notendichte über die Zeit, "
    "je Spur eine Farbe gestapelt), daneben die 12 Töne der Tonleiter und darunter beide Pianorollen übereinandergelegt und abgeglichen: "
    "grün = auch in der anderen Datei, rot = nur hier. Midi-Fundus löscht nichts von allein; „A in Papierkorb“ und „B in Papierkorb“ legen nur "
    "nach einer Rückfrage in den Windows-Papierkorb.\n\n"
    "WAS IN DIE DATEI GESCHRIEBEN WIRD\n"
    "Titel (zusätzlich als Sequenzname), Copyright (Copyright-Eintrag), Tonart (Key Signature) sowie Interpret, Komponist, Genre, Jahr, Sprache und "
    "Kommentar als Text-Einträge „Artist: …“, „Composer: …“ usw. am Anfang der ersten Spur – lesbar für jedes Programm, das MIDI-Texte anzeigt. "
    "Bewertung und Favorit stehen nur in der Datenbank.\n\n"
    "SPRACHE\n"
    "Deutsch, English, Français – Auswahl im Setup oder jederzeit unter Einstellungen → „Sprache · Language · Langue“.\n\n"
    "LINKS\n"
    "Website: www.undabtanzbar.de\n\n"
    "ENTWICKLUNG\n"
    "Idee, Konzept und Regie: Kay 'Yakobo' Fiedler.\n"
    "Umgesetzt mit Claude (Anthropic) als KI-Programmierassistent für C++/JUCE-Code, MIDI-Analyse, Dublettenerkennung, Übersetzung und Tests – "
    "abgestimmt am eigenen MIDI-Archiv.\n\n"
    "TECHNIK\n"
    "JUCE (VST3 und Standalone). Eigener SMF-Leser und -Schreiber (Format 0/1/2, RIFF-RMID, Karaoke), Tonarterkennung nach Krumhansl-Kessler, "
    "Melodie-Fingerabdruck aus transpositions- und tempounabhängigen Intervall-N-Grammen (Bottom-k-Sketch), Online-Abfragen bei MusicBrainz und Wikidata. "
    "Vorhören mit TinySoundFont (Bernhard Schelling, MIT-Lizenz) und dem General-MIDI-SoundFont GeneralUser GS von S. Christian Collins "
    "(frei nutzbar, Lizenz liegt bei); wahlweise Windows-GM oder der Kreisel-Klangerzeuger.\n\n"
    "UNTERSTÜTZEN\n"
    "Midi-Fundus ist kostenlos. Wer die Entwicklung unterstützen möchte, kann über den Knopf unten eine kleine Spende per PayPal schicken – vielen Dank!";

const char* const kAboutEn =
    "WHAT DOES THIS PROGRAM DO?\n"
    "Midi-Fundus searches any folders including their subfolders for MIDI files (.mid, .midi, .kar, .rmi) and lists everything worth knowing in one "
    "large table: title, artist, composer, genre, year, copyright, language, key and scale, tempo, time signature, duration, tracks, channels, "
    "number of notes, instruments, lyrics, rating and favourites. It finds duplicates while scanning – not by file name but by the notes.\n\n"
    "QUICK START\n"
    "1. “Folders” → “Add folder …” or simply drag folders into the window. Then “Scan”. Known, unchanged files are skipped; "
    "the database lives in %APPDATA%\\MIDIARCHIV and is shared by the standalone program and the plug-in.\n"
    "2. Click a column header = sort, click again = reverse. Hold and drag a header = move the column, drag its edge = width. "
    "Right-click the header to show or hide columns. Order and widths are remembered.\n"
    "3. Below the table is the search mask: one field per column, always right underneath it. All fields work together.\n"
    "4. Clicking title, artist, composer, genre, year, copyright, language, comment or key (file) opens the cell for editing right away "
    "(content selected, cursor at the end). Enter saves – directly into the MIDI file itself – and moves one row down, Esc cancels. "
    "The arrow keys move a frame from cell to cell; Enter or simply typing opens the field, Tab jumps to the next field. Inside an open field the arrow keys only move the text cursor; Enter (or Tab) leaves it, Esc discards. Right-click → “Undo” (Ctrl+Z) reverts the last change. "
    "Multi-edit: select rows with Ctrl- or Shift-click – the cell clicked last is edited, the selection stays, "
    "and Enter writes the value into all selected files after asking. Before the first change the original is copied once to "
    "%APPDATA%\\MIDIARCHIV\\Sicherung; after writing, Midi-Fundus checks that the notes are unchanged.\n"
    "5. Grab a row and drag it into your DAW = the MIDI file lands on the track. Several selected rows work too.\n"
    "6. Right-click a cell: web research, automatic search for missing details, deep mode via the melody, copy, move and delete files and more.\n\n"
    "DUPLICATES\n"
    "“identical” = file equal byte for byte, “same notes” = the same music with other texts/settings, “similar nn %” = the same melodic phrases, "
    "also transposed, at another tempo or arranged differently. The duplicate view shows both files as note mountains (note density over time, "
    "one stacked colour per track), next to them the 12 tones of the scale and below both piano rolls laid on top of each other and aligned: "
    "green = also in the other file, red = only here. Midi-Fundus never deletes anything by itself; “A to bin” and “B to bin” only move files "
    "to the Windows recycle bin after asking.\n\n"
    "WHAT IS WRITTEN INTO THE FILE\n"
    "Title (also as sequence name), copyright (copyright event), key (key signature) as well as artist, composer, genre, year, language and "
    "comment as text events “Artist: …”, “Composer: …” etc. at the start of the first track – readable by every program that shows MIDI texts. "
    "Rating and favourite are stored in the database only.\n\n"
    "LANGUAGE\n"
    "Deutsch, English, Français – chosen in the setup or any time under Settings → “Sprache · Language · Langue”.\n\n"
    "LINKS\n"
    "Website: www.undabtanzbar.de\n\n"
    "DEVELOPMENT\n"
    "Idea, concept and direction: Kay 'Yakobo' Fiedler.\n"
    "Built with Claude (Anthropic) as AI programming assistant for C++/JUCE code, MIDI analysis, duplicate detection, translation and tests – "
    "tuned on a real MIDI archive.\n\n"
    "TECHNOLOGY\n"
    "JUCE (VST3 and standalone). Own SMF reader and writer (format 0/1/2, RIFF RMID, karaoke), key detection after Krumhansl-Kessler, "
    "melody fingerprint from transposition- and tempo-independent interval n-grams (bottom-k sketch), online queries to MusicBrainz and Wikidata. "
    "Preview with TinySoundFont (Bernhard Schelling, MIT licence) and the General MIDI SoundFont GeneralUser GS by S. Christian Collins "
    "(free to use, licence included); alternatively Windows GM or the Kreisel synth.\n\n"
    "SUPPORT\n"
    "Midi-Fundus is free. If you would like to support its development, the button below sends a small donation via PayPal – thank you!";

const char* const kAboutFr =
    "QUE FAIT CE PROGRAMME ?\n"
    "Midi-Fundus parcourt les dossiers de votre choix, sous-dossiers compris, à la recherche de fichiers MIDI (.mid, .midi, .kar, .rmi) et affiche "
    "tout ce qu'il faut savoir dans un grand tableau : titre, interprète, compositeur, genre, année, copyright, langue, tonalité et gamme, tempo, "
    "mesure, durée, pistes, canaux, nombre de notes, instruments, paroles, note et favoris. Dès l'analyse, il cherche les doublons – non pas d'après "
    "le nom du fichier, mais d'après les notes.\n\n"
    "PRISE EN MAIN\n"
    "1. « Dossiers » → « Ajouter un dossier … » ou glisser simplement des dossiers dans la fenêtre. Puis « Analyser ». Les fichiers déjà connus et "
    "inchangés sont ignorés ; la base de données se trouve dans %APPDATA%\\MIDIARCHIV et est partagée par le programme autonome et le plug-in.\n"
    "2. Clic sur un en-tête de colonne = trier, nouveau clic = ordre inverse. Maintenir et glisser l'en-tête = déplacer la colonne, glisser le bord = "
    "largeur. Clic droit sur l'en-tête pour afficher ou masquer des colonnes. Ordre et largeurs sont mémorisés.\n"
    "3. Sous le tableau se trouve le masque de recherche : un champ par colonne, toujours juste en dessous. Tous les champs agissent ensemble.\n"
    "4. Un clic sur titre, interprète, compositeur, genre, année, copyright, langue, commentaire ou tonalité (fichier) ouvre la cellule pour "
    "l'éditer (contenu sélectionné, curseur à la fin). Entrée enregistre – directement dans le fichier MIDI lui-même – et descend d'une ligne, Échap annule. "
    "Les flèches déplacent un cadre de cellule en cellule ; Entrée ou simplement la frappe ouvre le champ, Tab passe au champ suivant. Dans un champ ouvert, les flèches ne déplacent que le curseur du texte ; on le quitte avec Entrée (ou Tab), Échap annule. Clic droit → « Annuler » (Ctrl+Z) revient sur la dernière modification. "
    "Édition multiple : sélectionner des lignes avec Ctrl ou Maj + clic – la dernière cellule cliquée est éditée, la sélection reste, et Entrée "
    "écrit la valeur dans tous les fichiers sélectionnés après confirmation. Avant la première modification, l'original est copié une seule fois "
    "dans %APPDATA%\\MIDIARCHIV\\Sicherung ; après l'écriture, Midi-Fundus vérifie que les notes sont inchangées.\n"
    "5. Saisir une ligne et la glisser dans le DAW = le fichier MIDI arrive sur la piste. Plusieurs lignes sélectionnées fonctionnent aussi.\n"
    "6. Clic droit dans une cellule : recherche sur Internet, recherche automatique des informations manquantes, mode approfondi via la mélodie, "
    "copier, déplacer et supprimer des fichiers, et plus encore.\n\n"
    "DOUBLONS\n"
    "« identique » = fichier égal octet pour octet, « mêmes notes » = la même musique avec d'autres textes/réglages, « similaire nn % » = les mêmes "
    "phrases mélodiques, même transposées, à un autre tempo ou arrangées autrement. La vue des doublons montre les deux fichiers sous forme de "
    "montagnes de notes (densité des notes dans le temps, une couleur empilée par piste), à côté les 12 notes de la gamme et en dessous les deux "
    "pianos-rolls superposés et alignés : vert = aussi dans l'autre fichier, rouge = seulement ici. Midi-Fundus ne supprime rien de lui-même ; "
    "« A à la corbeille » et « B à la corbeille » déplacent seulement vers la corbeille de Windows, après confirmation.\n\n"
    "CE QUI EST ÉCRIT DANS LE FICHIER\n"
    "Titre (aussi comme nom de séquence), copyright (événement copyright), tonalité (key signature) ainsi qu'interprète, compositeur, genre, année, "
    "langue et commentaire sous forme d'événements texte « Artist: … », « Composer: … » etc. au début de la première piste – lisibles par tout "
    "programme qui affiche les textes MIDI. La note et les favoris restent uniquement dans la base de données.\n\n"
    "LANGUE\n"
    "Deutsch, English, Français – choix dans l'installation ou à tout moment sous Paramètres → « Sprache · Language · Langue ».\n\n"
    "LIENS\n"
    "Site web : www.undabtanzbar.de\n\n"
    "DÉVELOPPEMENT\n"
    "Idée, concept et direction : Kay 'Yakobo' Fiedler.\n"
    "Réalisé avec Claude (Anthropic) comme assistant de programmation IA pour le code C++/JUCE, l'analyse MIDI, la détection de doublons, "
    "la traduction et les tests – mis au point sur une vraie archive MIDI.\n\n"
    "TECHNIQUE\n"
    "JUCE (VST3 et autonome). Lecteur et enregistreur SMF maison (format 0/1/2, RIFF RMID, karaoké), détection de tonalité selon Krumhansl-Kessler, "
    "empreinte mélodique à partir de n-grammes d'intervalles indépendants de la transposition et du tempo (bottom-k sketch), requêtes en ligne "
    "auprès de MusicBrainz et Wikidata. Écoute avec TinySoundFont (Bernhard Schelling, licence MIT) et la SoundFont General MIDI GeneralUser GS "
    "de S. Christian Collins (libre d'utilisation, licence jointe) ; au choix aussi le GM de Windows ou le synthé Kreisel.\n\n"
    "SOUTENIR\n"
    "Midi-Fundus est gratuit. Si vous souhaitez soutenir son développement, le bouton ci-dessous permet d'envoyer un petit don via PayPal – merci !";

const char* const kTipsDe =
    "• Leertaste oder Enter spielt die markierte Datei, Leertaste nochmal stoppt. Doppelklick auf eine nicht bearbeitbare Zelle spielt ebenfalls.\n"
    "• Vorhören klingt standardmäßig über den mitgelieferten General-MIDI-SoundFont GeneralUser GS: alle 128 GM-Instrumente, GS-Varianten "
    "und das GM-Schlagzeug, so wie die Datei es vorsieht – im Plugin direkt durch die DAW. Unter Einstellungen → „Klang beim Vorhören“ "
    "lässt sich ein eigener SoundFont (.sf2) laden oder auf Windows-GM (Microsoft GS Wavetable Synth) bzw. den Kreisel-Synth umschalten; "
    "zusätzlich kann die Vorschau als MIDI an die DAW gehen.\n"
    "• Rechtsklick → „Datei“ / „Dateien (n)“: kopieren oder verschieben in einen anderen Ordner, löschen in den Papierkorb – für alle markierten Zeilen. "
    "Gleichnamige Dateien am Ziel werden nicht überschrieben, sondern bekommen eine Nummer. Verschobene Dateien behalten Bewertung und Favorit.\n"
    "• Rechtsklick → „Ermittelte Tonart eintragen“ schreibt die aus den Noten bestimmte Tonart als Key Signature in die Datei (auch für alle markierten). "
    "„Tonart (Datei)“ lässt sich auch direkt bearbeiten: a-Moll, Es-Dur, F#m, fis … (kleiner Anfangsbuchstabe = Moll).\n"
    "• Suchmaske: Wörter müssen alle vorkommen; !wort = enthält nicht; = allein = leer; ! allein = nicht leer; =text = genau dieser Text. "
    "Zahlenspalten: >120, <=3:00, 100-130, =1998. Das Suchfeld oben durchsucht Dateiname, Titel, Interpret, Komponist, Genre, Copyright, Kommentar und Ordner.\n"
    "• Kursive, graue Titel und Interpreten stammen aus dem Dateinamen („Interpret - Titel.mid“). Rechtsklick → „Aus Dateinamen in die Datei schreiben“ macht sie echt.\n"
    "• Rechtsklick → „Wert auf alle markierten übertragen“ setzt z. B. das Genre für eine ganze Auswahl auf einmal.\n"
    "• Rechtsklick → „Fehlende Angaben online suchen“ fragt MusicBrainz (Interpret, frühestes Jahr, Genre) und Wikidata (Komponist, Entstehungsjahr – wichtig bei Klassik) ab. "
    "Nichts wird ohne Haken und „Übernehmen“ geschrieben.\n"
    "• Deep-Modus: „Melodie im eigenen Archiv suchen“ zieht die Hauptmelodie heraus und sucht sie in allen anderen Dateien – findet sie sich in einer bereits "
    "beschrifteten Datei, lassen sich deren Titel, Interpret, Komponist und Jahr übernehmen. „Melodie kopieren“ legt Notennamen und Parsons-Code "
    "(*UDRU…) in die Zwischenablage, z. B. für Melodiesuchmaschinen wie Musipedia.\n"
    "• Ähnlichkeitsschwelle für „ähnlich“ unter Einstellungen (Standard 45 %). Niedriger findet mehr Bearbeitungen, aber auch mehr Zufallstreffer.\n"
    "• „Kein Duplikat“ trennt B dauerhaft von A – auch wenn beide nur über andere Dateien der Gruppe verbunden sind; dabei werden nur die schwächsten "
    "nötigen Verbindungen gelöst. Die Liste bleibt an ihrer Stelle, die nächste Datei wird B.\n"
    "• „A in Papierkorb“ / „B in Papierkorb“ in der Dubletten-Ansicht: nach A geht es mit B als neuem A weiter, nach B mit der nächsten Datei. "
    "Fehlende Dateien (durchgestrichen) entfernt Einstellungen → „Fehlende Dateien aus der Datenbank entfernen“.\n"
    "• Spalte „Profil“: das Notengebirge im Kleinen – gleiche Songs haben gleiche Gebirge, auch wenn Tonart oder Tempo anders sind.\n"
    "• Sprache: Einstellungen → „Sprache · Language · Langue“ wechselt sofort. Tonarten lassen sich in der gewählten Sprache eingeben "
    "(fis-Moll, F# minor, fa# mineur); in der Datei steht immer die standardisierte Key Signature.";

const char* const kTipsEn =
    "• Space or Enter plays the selected file, Space again stops. Double-clicking a cell that cannot be edited plays as well.\n"
    "• By default the preview sounds through the included General MIDI SoundFont GeneralUser GS: all 128 GM instruments, GS variations "
    "and the GM drum kit, just as the file intends – in the plug-in directly through the DAW. Under Settings → “Preview sound” "
    "you can load your own SoundFont (.sf2) or switch to Windows GM (Microsoft GS Wavetable Synth) or the Kreisel synth; "
    "in addition the preview can be sent as MIDI to the DAW.\n"
    "• Right-click → “File” / “Files (n)”: copy or move to another folder, delete to the recycle bin – for all selected rows. "
    "Files of the same name at the destination are never overwritten but get a number. Moved files keep their rating and favourite.\n"
    "• Right-click → “Write detected key” writes the key determined from the notes as key signature into the file (also for all selected). "
    "“Key (file)” can also be edited directly: A minor, Eb major, F#m, Bb …\n"
    "• Search mask: all words must occur; !word = does not contain; = alone = empty; ! alone = not empty; =text = exactly this text. "
    "Number columns: >120, <=3:00, 100-130, =1998. The search field at the top searches file name, title, artist, composer, genre, copyright, comment and folder.\n"
    "• Grey italic titles and artists come from the file name (“Artist - Title.mid”). Right-click → “Write title/artist from the file name into the file” makes them real.\n"
    "• Right-click → “Copy value to all selected” sets e.g. the genre for a whole selection at once.\n"
    "• Right-click → “Search missing details online” asks MusicBrainz (artist, earliest year, genre) and Wikidata (composer, year of composition – important "
    "for classical music). Nothing is written without a tick and “Apply”.\n"
    "• Deep mode: “Search melody in own archive” extracts the main melody and looks for it in all other files – if it appears in a file that is already "
    "labelled, its title, artist, composer and year can be taken over. “Copy melody” puts note names and Parsons code "
    "(*UDRU…) on the clipboard, e.g. for melody search engines such as Musipedia.\n"
    "• Similarity threshold for “similar” under Settings (default 45 %). Lower finds more arrangements, but also more chance hits.\n"
    "• “Not a duplicate” separates B from A for good – even if both are only connected through other files of the group; only the weakest "
    "necessary links are removed. The list stays where it is, the next file becomes B.\n"
    "• “A to bin” / “B to bin” in the duplicate view: after A, B becomes the new A; after B, the next file follows. "
    "Missing files (struck through) are removed with Settings → “Remove missing files from the database”.\n"
    "• Column “Profile”: the note mountain in miniature – the same songs have the same mountains, even in another key or tempo.\n"
    "• Language: Settings → “Sprache · Language · Langue” switches immediately. Keys can be typed in the chosen language "
    "(F# minor, fis-Moll, fa# mineur); the file always contains the standard key signature.";

const char* const kTipsFr =
    "• Espace ou Entrée joue le fichier sélectionné, Espace à nouveau l'arrête. Un double-clic sur une cellule non modifiable le joue aussi.\n"
    "• Par défaut, l'écoute passe par la SoundFont General MIDI fournie, GeneralUser GS : les 128 instruments GM, les variantes GS "
    "et la batterie GM, comme le fichier le prévoit – dans le plug-in directement à travers le DAW. Sous Paramètres → « Son de l'écoute » "
    "on peut charger sa propre SoundFont (.sf2) ou passer au GM de Windows (Microsoft GS Wavetable Synth) ou au synthé Kreisel ; "
    "l'écoute peut en plus être envoyée en MIDI au DAW.\n"
    "• Clic droit → « Fichier » / « Fichiers (n) » : copier ou déplacer vers un autre dossier, supprimer vers la corbeille – pour toutes les lignes "
    "sélectionnées. Les fichiers de même nom à la destination ne sont jamais écrasés mais reçoivent un numéro. Les fichiers déplacés gardent leur note et leur statut de favori.\n"
    "• Clic droit → « Inscrire la tonalité détectée » écrit la tonalité déterminée à partir des notes comme key signature dans le fichier (aussi pour "
    "toutes les lignes sélectionnées). « Tonalité (fichier) » se modifie aussi directement : la mineur, mib majeur, fa# mineur …\n"
    "• Masque de recherche : tous les mots doivent apparaître ; !mot = ne contient pas ; = seul = vide ; ! seul = non vide ; =texte = exactement ce texte. "
    "Colonnes numériques : >120, <=3:00, 100-130, =1998. Le champ de recherche en haut parcourt nom de fichier, titre, interprète, compositeur, genre, "
    "copyright, commentaire et dossier.\n"
    "• Les titres et interprètes gris en italique viennent du nom de fichier (« Interprète - Titre.mid »). Clic droit → « Écrire titre/interprète du nom "
    "de fichier dans le fichier » les rend définitifs.\n"
    "• Clic droit → « Reporter la valeur sur toutes les lignes sélectionnées » règle p. ex. le genre de toute une sélection d'un coup.\n"
    "• Clic droit → « Chercher les informations manquantes en ligne » interroge MusicBrainz (interprète, année la plus ancienne, genre) et Wikidata "
    "(compositeur, année de composition – important pour la musique classique). Rien n'est écrit sans coche et « Appliquer ».\n"
    "• Mode approfondi : « Chercher la mélodie dans l'archive » extrait la mélodie principale et la cherche dans tous les autres fichiers – si elle "
    "figure dans un fichier déjà renseigné, on peut reprendre son titre, interprète, compositeur et année. « Copier la mélodie » met les noms de notes "
    "et le code Parsons (*UDRU…) dans le presse-papiers, p. ex. pour des moteurs de recherche de mélodies comme Musipedia.\n"
    "• Seuil de similarité pour « similaire » sous Paramètres (45 % par défaut). Plus bas trouve plus d'arrangements, mais aussi plus de faux positifs.\n"
    "• « Pas un doublon » sépare B de A définitivement – même s'ils ne sont reliés que par d'autres fichiers du groupe ; seules les liaisons nécessaires "
    "les plus faibles sont supprimées. La liste reste à sa place, le fichier suivant devient B.\n"
    "• « A à la corbeille » / « B à la corbeille » dans la vue des doublons : après A, B devient le nouveau A ; après B, le fichier suivant prend la place. "
    "Les fichiers manquants (barrés) se retirent via Paramètres → « Retirer les fichiers manquants de la base de données ».\n"
    "• Colonne « Profil » : la montagne de notes en miniature – les mêmes morceaux ont les mêmes montagnes, même dans une autre tonalité ou à un autre tempo.\n"
    "• Langue : Paramètres → « Sprache · Language · Langue » change immédiatement. Les tonalités peuvent être saisies dans la langue choisie "
    "(fa# mineur, F# minor, fis-Moll) ; le fichier contient toujours la key signature standard.";

// German text -> English, French
const Row kRows[] = {
    // ---- table begin
    { "@about", kAboutEn, kAboutFr },
    { "@tips", kTipsEn, kTipsFr },
    // archive / scan
    { "Suche MIDI-Dateien … %1 gefunden", "Looking for MIDI files … %1 found", "Recherche de fichiers MIDI … %1 trouvés" },
    { "Lese ein: %1 / %2  (%3)", "Scanning: %1 / %2  (%3)", "Analyse : %1 / %2  (%3)" },
    { "Datei nicht lesbar", "File not readable", "Fichier illisible" },
    { "MIDI-Kopf beschädigt", "MIDI header damaged", "En-tête MIDI endommagé" },
    { "Spur 1 ist beschädigt – Schreiben abgebrochen", "Track 1 is damaged – writing cancelled", "La piste 1 est endommagée – écriture annulée" },
    { "Tonart nicht verstanden: ", "Key not understood: ", "Tonalité non comprise : " },
    { "Prüfung nach dem Schreiben fehlgeschlagen – Datei unverändert", "Check after writing failed – file unchanged", "Échec de la vérification après écriture – fichier inchangé" },
    { "Datei konnte nicht geschrieben werden (schreibgeschützt?)", "File could not be written (read-only?)", "Impossible d'écrire le fichier (lecture seule ?)" },
    // table / search mask
    { "Suche …", "Search …", "Rechercher …" },
    { "Suche in „%1“: Wörter (alle müssen vorkommen), !wort = enthält nicht, = leer, ! nicht leer, =text genau",
      "Search in “%1”: words (all must occur), !word = does not contain, = empty, ! not empty, =text exact",
      "Recherche dans « %1 » : mots (tous doivent apparaître), !mot = ne contient pas, = vide, ! non vide, =texte exact" },
    { ", Zahlen: >120  <=3:00  100-130", ", numbers: >120  <=3:00  100-130", ", nombres : >120  <=3:00  100-130" },
    { "ja (%1)", "yes (%1)", "oui (%1)" },
    { "ja", "yes", "oui" },
    { "Aus dem Dateinamen abgeleitet – Klick zum Bearbeiten, Enter schreibt ihn in die Datei",
      "Derived from the file name – click to edit, Enter writes it into the file",
      "Déduit du nom de fichier – cliquer pour modifier, Entrée l'écrit dans le fichier" },
    { "Tonart aus den Noten (Sicherheit %1 %)", "Key from the notes (confidence %1 %)", "Tonalité d'après les notes (fiabilité %1 %)" },
    { "; in der Datei eingetragen: %1", "; written in the file: %1", " ; inscrite dans le fichier : %1" },
    { "Ähnlichste Datei: %1", "Most similar file: %1", "Fichier le plus proche : %1" },
    { "Datei fehlt – nicht bearbeitbar", "File missing – cannot be edited", "Fichier manquant – non modifiable" },
    { "Mehrere Dateien ändern", "Change several files", "Modifier plusieurs fichiers" },
    { "%1 = „%2“\n\nin %3 markierte Dateien schreiben?", "%1 = “%2”\n\nwrite into %3 selected files?", "%1 = « %2 »\n\nécrire dans %3 fichiers sélectionnés ?" },
    { "Alle %1 ändern", "Change all %1", "Modifier les %1" },
    { "Abbrechen", "Cancel", "Annuler" },
    { "Nicht geschrieben:", "Not written:", "Non écrit :" },
    { "%1 in %2 Dateien geschrieben (Originale einmalig gesichert)", "%1 written into %2 files (originals backed up once)", "%1 écrit dans %2 fichiers (originaux sauvegardés une fois)" },
    { "Konnte nicht in die Datei schreiben:", "Could not write into the file:", "Impossible d'écrire dans le fichier :" },
    { "Gespeichert in %1 (Original einmalig gesichert in %APPDATA%\\MIDIARCHIV\\Sicherung)",
      "Saved in %1 (original backed up once in %APPDATA%\\MIDIARCHIV\\Sicherung)",
      "Enregistré dans %1 (original sauvegardé une fois dans %APPDATA%\\MIDIARCHIV\\Sicherung)" },
    { "identisch", "identical", "identique" },
    { "gleiche Noten", "same notes", "mêmes notes" },
    { "ähnlich %1 %", "similar %1 %", "similaire %1 %" },
    { "FEHLER: %1", "ERROR: %1", "ERREUR : %1" },
    // duplicate view
    { "B in Tabelle", "B in table", "B dans le tableau" },
    { "Kein Duplikat", "Not a duplicate", "Pas un doublon" },
    { "A in Papierkorb", "A to bin", "A à la corbeille" },
    { "B in Papierkorb", "B to bin", "B à la corbeille" },
    { "Datei A (in der Tabelle gewählt) anhören", "Listen to file A (selected in the table)", "Écouter le fichier A (choisi dans le tableau)" },
    { "Vergleichsdatei B anhören", "Listen to comparison file B", "Écouter le fichier de comparaison B" },
    { "Datei B in der Tabelle auswählen", "Select file B in the table", "Sélectionner le fichier B dans le tableau" },
    { "A und B sind keine Dubletten – Verbindung dauerhaft lösen", "A and B are not duplicates – separate them for good", "A et B ne sont pas des doublons – les séparer définitivement" },
    { "Datei A (in der Tabelle gewählt) in den Windows-Papierkorb legen (wiederherstellbar) – danach geht es mit B weiter",
      "Move file A (selected in the table) to the Windows recycle bin (restorable) – then continue with B",
      "Déplacer le fichier A (choisi dans le tableau) vers la corbeille de Windows (récupérable) – puis continuer avec B" },
    { "Datei B in den Windows-Papierkorb legen (wiederherstellbar)", "Move file B to the Windows recycle bin (restorable)", "Déplacer le fichier B vers la corbeille de Windows (récupérable)" },
    { "Getrennt: %1", "Separated: %1", "Séparé : %1" },
    { "über die Gruppe verbunden", "connected through the group", "relié via le groupe" },
    { "Datei in der Tabelle wählen – hier erscheinen ihre Dubletten.", "Choose a file in the table – its duplicates appear here.", "Choisissez un fichier dans le tableau – ses doublons apparaissent ici." },
    { "Zu „%1“ wurden keine Dubletten gefunden.", "No duplicates were found for “%1”.", "Aucun doublon trouvé pour « %1 »." },
    { "A: %1 % der Noten auch in B  ·  B: %2 % auch in A  ·  B transponiert %3 HT, versetzt %4 Viertel",
      "A: %1 % of the notes also in B  ·  B: %2 % also in A  ·  B transposed %3 semitones, shifted %4 quarters",
      "A : %1 % des notes aussi dans B  ·  B : %2 % aussi dans A  ·  B transposé de %3 demi-tons, décalé de %4 noires" },
    { "Dateien konnten nicht verglichen werden", "Files could not be compared", "Impossible de comparer les fichiers" },
    { "12 Töne (B transponiert)", "12 tones (B transposed)", "12 notes (B transposé)" },
    { "Gefüllt = A (grün: auch in B, rot: nur in A)  ·  Umriss = B  ·  Mausrad zoomt, Ziehen verschiebt",
      "Filled = A (green: also in B, red: only in A)  ·  Outline = B  ·  Mouse wheel zooms, drag scrolls",
      "Plein = A (vert : aussi dans B, rouge : seulement dans A)  ·  Contour = B  ·  Molette = zoom, glisser = défiler" },
    // fields
    { "Titel", "Title", "Titre" },
    { "Interpret", "Artist", "Interprète" },
    { "Komponist", "Composer", "Compositeur" },
    { "Jahr", "Year", "Année" },
    { "Kommentar", "Comment", "Commentaire" },
    { "Sprache", "Language", "Langue" },
    { "Tonart", "Key", "Tonalité" },
    // about / suggestions
    { "Über Midi-Fundus", "About Midi-Fundus", "À propos de Midi-Fundus" },
    { "Tooltipps", "Tips", "Astuces" },
    { "Schließen", "Close", "Fermer" },
    { "Spenden (PayPal)", "Donate (PayPal)", "Faire un don (PayPal)" },
    { "Version %1  ·  MIDI-Archiv mit Dublettenfinder  ·  von Un(d)AbtanzBAR", "Version %1  ·  MIDI archive with duplicate finder  ·  by Un(d)AbtanzBAR",
      "Version %1  ·  Archive MIDI avec détecteur de doublons  ·  par Un(d)AbtanzBAR" },
    { "Im Browser suchen: %1", "Search in the browser: %1", "Rechercher dans le navigateur : %1" },
    { "Übernehmen", "Apply", "Appliquer" },
    { "%1 Angabe(n) in „%2“ geschrieben", "%1 detail(s) written into “%2”", "%1 information(s) écrite(s) dans « %2 »" },
    { "Nichts Passendes gefunden. Die Knöpfe unten öffnen die Suche im Browser.", "Nothing suitable found. The buttons below open the search in the browser.",
      "Rien de correspondant trouvé. Les boutons ci-dessous ouvrent la recherche dans le navigateur." },
    { "Haken setzen und „Übernehmen“ – die Werte werden in die Datei geschrieben.", "Tick and “Apply” – the values are written into the file.",
      "Cocher puis « Appliquer » – les valeurs sont écrites dans le fichier." },
    { "   (bisher: %1)", "   (currently: %1)", "   (actuel : %1)" },
    { "Vorhören: Leertaste / Doppelklick", "Preview: Space / double-click", "Écoute : Espace / double-clic" },
    // editor
    { "Ordner", "Folders", "Dossiers" },
    { "Einstellungen", "Settings", "Paramètres" },
    { "Filter löschen", "Clear filters", "Effacer les filtres" },
    { "Stopp", "Stop", "Arrêter" },
    { "Einlesen", "Scan", "Analyser" },
    { "Noch leer. Über „Ordner“ einen oder mehrere Ordner mit MIDI-Dateien hinzufügen (oder ins Fenster ziehen) und „Einlesen“ klicken.",
      "Still empty. Add one or more folders with MIDI files via “Folders” (or drag them into the window) and click “Scan”.",
      "Encore vide. Ajoutez un ou plusieurs dossiers de fichiers MIDI via « Dossiers » (ou glissez-les dans la fenêtre) puis cliquez sur « Analyser »." },
    { "Ordner hinzufügen oder entfernen (Ordner lassen sich auch ins Fenster ziehen)", "Add or remove folders (folders can also be dragged into the window)",
      "Ajouter ou retirer des dossiers (on peut aussi les glisser dans la fenêtre)" },
    { "Alle Ordner einlesen – neue und geänderte Dateien werden analysiert, Dubletten sofort gesucht",
      "Scan all folders – new and changed files are analysed, duplicates are searched right away",
      "Analyser tous les dossiers – les fichiers nouveaux et modifiés sont analysés, les doublons cherchés aussitôt" },
    { "Markierte Datei abspielen (Leertaste)", "Play the selected file (Space)", "Jouer le fichier sélectionné (Espace)" },
    { "Farbthema, Sprache, Klang, Dubletten-Schwelle, Datenbank", "Colour theme, language, sound, duplicate threshold, database",
      "Thème de couleurs, langue, son, seuil des doublons, base de données" },
    { "Über Midi-Fundus und Tooltipps", "About Midi-Fundus and tips", "À propos de Midi-Fundus et astuces" },
    { "Suche in allen Feldern …", "Search all fields …", "Rechercher dans tous les champs …" },
    { "Nur Dateien zeigen, die zu einer Dubletten-Gruppe gehören", "Only show files that belong to a duplicate group", "N'afficher que les fichiers appartenant à un groupe de doublons" },
    { "Nur Favoriten zeigen", "Only show favourites", "N'afficher que les favoris" },
    { "Dubletten-Ansicht unter der Tabelle ein- oder ausblenden", "Show or hide the duplicate view below the table", "Afficher ou masquer la vue des doublons sous le tableau" },
    { "Alle Suchfelder leeren", "Clear all search fields", "Vider tous les champs de recherche" },
    { "%1 Dateien  ·  %2 angezeigt  ·  %3 Dubletten-Gruppen", "%1 files  ·  %2 shown  ·  %3 duplicate groups", "%1 fichiers  ·  %2 affichés  ·  %3 groupes de doublons" },
    { "Dubl.", "Dupl.", "Doubl." },
    { "Nur Dubletten", "Duplicates only", "Doublons seuls" },
    { "Fav.", "Fav.", "Fav." },
    { "Nur Favoriten", "Favourites only", "Favoris seuls" },
    { "Ansicht", "View", "Vue" },
    { "Dubletten-Ansicht", "Duplicate view", "Vue doublons" },
    { "Willkommen! Zuerst Ordner hinzufügen.", "Welcome! First add folders.", "Bienvenue ! Ajoutez d'abord des dossiers." },
    { "Kann nicht abgespielt werden: %1", "Cannot be played: %1", "Lecture impossible : %1" },
    { "Ordner mit MIDI-Dateien wählen", "Choose a folder with MIDI files", "Choisir un dossier de fichiers MIDI" },
    { "Ordner hinzufügen …", "Add folder …", "Ajouter un dossier …" },
    { "(noch keine Ordner)", "(no folders yet)", "(aucun dossier pour l'instant)" },
    { "Im Explorer öffnen", "Open in Explorer", "Ouvrir dans l'Explorateur" },
    { "Aus der Liste nehmen (Einträge behalten)", "Remove from the list (keep entries)", "Retirer de la liste (garder les entrées)" },
    { "Aus der Liste nehmen und Einträge löschen", "Remove from the list and delete entries", "Retirer de la liste et supprimer les entrées" },
    { "Hell", "Light", "Clair" },
    { "Farbthema", "Colour theme", "Thème de couleurs" },
    { "Ähnlichkeitsschwelle (Dubletten)", "Similarity threshold (duplicates)", "Seuil de similarité (doublons)" },
    { "General-MIDI-SoundFont: %1", "General MIDI SoundFont: %1", "SoundFont General MIDI : %1" },
    { "(nicht geladen)", "(not loaded)", "(non chargée)" },
    { "General MIDI von Windows (Microsoft GS Wavetable Synth – läuft an der DAW vorbei)", "General MIDI from Windows (Microsoft GS Wavetable Synth – bypasses the DAW)",
      "General MIDI de Windows (Microsoft GS Wavetable Synth – ne passe pas par le DAW)" },
    { "Eingebauter Synth-Klang (Kreisel, kein GM)", "Built-in synth sound (Kreisel, not GM)", "Son de synthé intégré (Kreisel, pas GM)" },
    { "Kein eigener Klang", "No sound of its own", "Pas de son propre" },
    { "Anderen SoundFont wählen (.sf2) …", "Choose another SoundFont (.sf2) …", "Choisir une autre SoundFont (.sf2) …" },
    { "Standard-SoundFont GeneralUser GS verwenden", "Use the standard SoundFont GeneralUser GS", "Utiliser la SoundFont standard GeneralUser GS" },
    { "Klang beim Vorhören", "Preview sound", "Son de l'écoute" },
    { "Play/Stopp der DAW folgen (Play spielt die markierte Datei)", "Follow the DAW's play/stop (play starts the selected file)", "Suivre lecture/arrêt du DAW (lecture joue le fichier sélectionné)" },
    { "DAW: Play – spielt die markierte Datei ab DAW-Position", "DAW: play – playing the selected file from the DAW position", "DAW : lecture – joue le fichier sélectionné depuis la position du DAW" },
    { "Tempo der DAW übernehmen (sonst eigenes Tempo der Datei)", "Use the DAW tempo (otherwise the file's own tempo)", "Utiliser le tempo du DAW (sinon le tempo propre du fichier)" },
    { "DAW: Stopp", "DAW: stop", "DAW : arrêt" },
    { "Vorhören zusätzlich als MIDI an die DAW / den MIDI-Ausgang", "Also send the preview as MIDI to the DAW / MIDI output", "Envoyer aussi l'écoute en MIDI au DAW / à la sortie MIDI" },
    { "Alle Dateien neu einlesen (auch unveränderte)", "Rescan all files (including unchanged ones)", "Réanalyser tous les fichiers (même inchangés)" },
    { "Fehlende Dateien aus der Datenbank entfernen", "Remove missing files from the database", "Retirer les fichiers manquants de la base de données" },
    { "Spaltenanordnung zurücksetzen", "Reset column layout", "Réinitialiser la disposition des colonnes" },
    { "Datenbank-Ordner öffnen", "Open database folder", "Ouvrir le dossier de la base de données" },
    { "Sicherungsordner (Originale vor Änderungen) öffnen", "Open backup folder (originals before changes)", "Ouvrir le dossier de sauvegarde (originaux avant modification)" },
    { "SoundFont wählen", "Choose SoundFont", "Choisir une SoundFont" },
    { "SoundFont wird geladen: %1", "Loading SoundFont: %1", "Chargement de la SoundFont : %1" },
    { "%1 fehlende Einträge entfernt", "%1 missing entries removed", "%1 entrées manquantes retirées" },
    // cell menu
    { "Abspielen", "Play", "Jouer" },
    { "Im Explorer zeigen", "Show in Explorer", "Afficher dans l'Explorateur" },
    { "%1 markierte Dateien kopieren nach …", "Copy %1 selected files to …", "Copier %1 fichiers sélectionnés vers …" },
    { "Datei kopieren nach …", "Copy file to …", "Copier le fichier vers …" },
    { "%1 markierte Dateien verschieben nach …", "Move %1 selected files to …", "Déplacer %1 fichiers sélectionnés vers …" },
    { "Datei verschieben nach …", "Move file to …", "Déplacer le fichier vers …" },
    { "%1 markierte Dateien löschen (Papierkorb) …", "Delete %1 selected files (recycle bin) …", "Supprimer %1 fichiers sélectionnés (corbeille) …" },
    { "Datei löschen (Papierkorb) …", "Delete file (recycle bin) …", "Supprimer le fichier (corbeille) …" },
    { "Dateien (%1)", "Files (%1)", "Fichiers (%1)" },
    { "Datei", "File", "Fichier" },
    { "Google: Komponist „%1“", "Google: composer “%1”", "Google : compositeur « %1 »" },
    { "Google: Titel + „Erscheinungsjahr“", "Google: title + “release year”", "Google : titre + « année de sortie »" },
    { "Google: Titel + „Komponist“", "Google: title + “composer”", "Google : titre + « compositeur »" },
    { "Erscheinungsjahr", "release year", "année de sortie" },
    { "Internetrecherche", "Web research", "Recherche sur Internet" },
    { "Fehlende Angaben online suchen (MusicBrainz + Wikidata) …", "Search missing details online (MusicBrainz + Wikidata) …", "Chercher les informations manquantes en ligne (MusicBrainz + Wikidata) …" },
    { "Melodie im eigenen Archiv suchen …", "Search melody in own archive …", "Chercher la mélodie dans l'archive …" },
    { "Melodie kopieren (Notennamen + Parsons-Code)", "Copy melody (note names + Parsons code)", "Copier la mélodie (noms de notes + code Parsons)" },
    { "Musipedia öffnen (Parsons-Code in der Zwischenablage)", "Open Musipedia (Parsons code on the clipboard)", "Ouvrir Musipedia (code Parsons dans le presse-papiers)" },
    { "Deep-Modus (Melodie)", "Deep mode (melody)", "Mode approfondi (mélodie)" },
    { "%1 bearbeiten", "Edit %1", "Modifier %1" },
    { "„%1“ auf alle %2 markierten übertragen", "Copy “%1” to all %2 selected", "Reporter « %1 » sur les %2 lignes sélectionnées" },
    { "Titel/Interpret aus dem Dateinamen in die Datei schreiben", "Write title/artist from the file name into the file", "Écrire titre/interprète du nom de fichier dans le fichier" },
    { "Als Suchfilter für „%1“", "As search filter for “%1”", "Comme filtre de recherche pour « %1 »" },
    { "Alle Dubletten dieser Gruppe zeigen", "Show all duplicates of this group", "Afficher tous les doublons de ce groupe" },
    { "Datei neu einlesen", "Rescan file", "Réanalyser le fichier" },
    { "Liedtext kopieren", "Copy lyrics", "Copier les paroles" },
    { "Ermittelte Tonart in alle %1 markierten Dateien eintragen", "Write the detected key into all %1 selected files", "Inscrire la tonalité détectée dans les %1 fichiers sélectionnés" },
    { "Ermittelte Tonart „%1“ in die Datei eintragen", "Write the detected key “%1” into the file", "Inscrire la tonalité détectée « %1 » dans le fichier" },
    { "%1 Dateien geändert", "%1 files changed", "%1 fichiers modifiés" },
    { "In die Datei geschrieben", "Written into the file", "Écrit dans le fichier" },
    { "Neu eingelesen: %1", "Rescanned: %1", "Réanalysé : %1" },
    { "Tonart in %1 Dateien eingetragen", "Key written into %1 files", "Tonalité inscrite dans %1 fichiers" },
    { ", %1 stimmten schon", ", %1 were already correct", ", %1 étaient déjà corrects" },
    // melody / lookup
    { "Keine Melodiestimme gefunden", "No melody voice found", "Aucune voix mélodique trouvée" },
    { "In der Zwischenablage: %1", "On the clipboard: %1", "Dans le presse-papiers : %1" },
    { "Online-Recherche: %1", "Online research: %1", "Recherche en ligne : %1" },
    { "Frage MusicBrainz und Wikidata …", "Asking MusicBrainz and Wikidata …", "Interrogation de MusicBrainz et Wikidata …" },
    { "Deep-Modus: Melodie von „%1“ im Archiv", "Deep mode: melody of “%1” in the archive", "Mode approfondi : mélodie de « %1 » dans l'archive" },
    { "Ziehe die Melodie heraus und vergleiche …", "Extracting the melody and comparing …", "Extraction de la mélodie et comparaison …" },
    { "Melodie: %1 Noten, %2 Motive; %3 Kandidaten geprüft", "Melody: %1 notes, %2 motifs; %3 candidates checked", "Mélodie : %1 notes, %2 motifs ; %3 candidats vérifiés" },
    { "%1 % der Melodie in „%2“", "%1 % of the melody in “%2”", "%1 % de la mélodie dans « %2 »" },
    { " (Dateiname)", " (file name)", " (nom de fichier)" },
    { "Keine andere Datei enthält diese Melodie.", "No other file contains this melody.", "Aucun autre fichier ne contient cette mélodie." },
    { "Keine Verbindung: %1", "No connection: %1", "Pas de connexion : %1" },
    { ": zu viele Anfragen in kurzer Zeit – bitte in ein paar Minuten erneut versuchen", ": too many requests in a short time – please try again in a few minutes",
      " : trop de requêtes en peu de temps – veuillez réessayer dans quelques minutes" },
    { "Antwort nicht lesbar: %1", "Response not readable: %1", "Réponse illisible : %1" },
    { "Kein Titel zum Suchen", "No title to search for", "Aucun titre à rechercher" },
    { "MusicBrainz: %1 passende Aufnahmen", "MusicBrainz: %1 matching recordings", "MusicBrainz : %1 enregistrements correspondants" },
    { "MusicBrainz (früheste Veröffentlichung)", "MusicBrainz (earliest release)", "MusicBrainz (première parution)" },
    { "Wikidata: %1 passende Werke", "Wikidata: %1 matching works", "Wikidata : %1 œuvres correspondantes" },
    { "Wikidata (Entstehung)", "Wikidata (composition)", "Wikidata (composition)" },
    { "Wikidata (Veröffentlichung)", "Wikidata (publication)", "Wikidata (publication)" },
    { "IMSLP (Klassik-Noten)", "IMSLP (classical scores)", "IMSLP (partitions classiques)" },
    // file operations
    { "… und %1 weitere", "… and %1 more", "… et %1 autres" },
    { "In den Papierkorb legen?", "Move to the recycle bin?", "Déplacer vers la corbeille ?" },
    { "1 Datei in den Windows-Papierkorb legen (wiederherstellbar):", "Move 1 file to the Windows recycle bin (restorable):", "Déplacer 1 fichier vers la corbeille de Windows (récupérable) :" },
    { "%1 Dateien in den Windows-Papierkorb legen (wiederherstellbar):", "Move %1 files to the Windows recycle bin (restorable):", "Déplacer %1 fichiers vers la corbeille de Windows (récupérable) :" },
    { "Löschen", "Delete", "Supprimer" },
    { "%1 Datei(en) in den Papierkorb gelegt", "%1 file(s) moved to the recycle bin", "%1 fichier(s) déplacé(s) vers la corbeille" },
    { "Nicht gelöscht:", "Not deleted:", "Non supprimés :" },
    { "Verschieben nach …", "Move to …", "Déplacer vers …" },
    { "Kopieren nach …", "Copy to …", "Copier vers …" },
    { "%1 (liegt schon dort)", "%1 (already there)", "%1 (déjà sur place)" },
    { "%1 Datei(en) verschoben nach %2", "%1 file(s) moved to %2", "%1 fichier(s) déplacé(s) vers %2" },
    { "%1 Datei(en) kopiert nach %2", "%1 file(s) copied to %2", "%1 fichier(s) copié(s) vers %2" },
    { "  (%1 umbenannt, Name war schon vergeben)", "  (%1 renamed, name was already taken)", "  (%1 renommé(s), le nom existait déjà)" },
    { "  (%1 neu im Archiv)", "  (%1 new in the archive)", "  (%1 nouveau(x) dans l'archive)" },
    { "Nicht verschoben:", "Not moved:", "Non déplacés :" },
    { "Nicht kopiert:", "Not copied:", "Non copiés :" },
    { "GeneralUser-GS.sf2 nicht gefunden", "GeneralUser-GS.sf2 not found", "GeneralUser-GS.sf2 introuvable" },
    { "SoundFont nicht lesbar: ", "SoundFont not readable: ", "SoundFont illisible : " },
    { "Keine gültige SoundFont-Datei: ", "Not a valid SoundFont file: ", "Fichier SoundFont non valide : " },
    // columns
    { "Nr", "No.", "N°" },
    { "Laufende Nummer in der Datenbank", "Sequential number in the database", "Numéro d'ordre dans la base de données" },
    { "Dateiname", "File name", "Nom de fichier" },
    { "Name der MIDI-Datei", "Name of the MIDI file", "Nom du fichier MIDI" },
    { "Songtitel – Klick zum Bearbeiten, wird in die Datei geschrieben. Kursiv = aus dem Dateinamen abgeleitet",
      "Song title – click to edit, written into the file. Italic = derived from the file name",
      "Titre du morceau – cliquer pour modifier, écrit dans le fichier. Italique = déduit du nom de fichier" },
    { "Interpret – Klick zum Bearbeiten. Kursiv = aus dem Dateinamen abgeleitet", "Artist – click to edit. Italic = derived from the file name",
      "Interprète – cliquer pour modifier. Italique = déduit du nom de fichier" },
    { "Komponist – Klick zum Bearbeiten", "Composer – click to edit", "Compositeur – cliquer pour modifier" },
    { "Genre – Klick zum Bearbeiten", "Genre – click to edit", "Genre – cliquer pour modifier" },
    { "Erscheinungsjahr (bei Klassik: Entstehung) – Klick zum Bearbeiten", "Release year (classical: year of composition) – click to edit",
      "Année de sortie (classique : année de composition) – cliquer pour modifier" },
    { "Copyright-Eintrag der Datei – Klick zum Bearbeiten", "Copyright event of the file – click to edit", "Mention de copyright du fichier – cliquer pour modifier" },
    { "Sprache der Liedtexte (aus Karaoke-Kopf oder erkannt)", "Language of the lyrics (from the karaoke header or detected)", "Langue des paroles (en-tête karaoké ou détectée)" },
    { "Aus den Noten ermittelte Tonart (? = unsicher)", "Key determined from the notes (? = uncertain)", "Tonalité déterminée d'après les notes (? = incertaine)" },
    { "Tongeschlecht", "Mode", "Mode" },
    { "Dur oder Moll – aus den Noten ermittelt (? = unsicher)", "Major or minor – determined from the notes (? = uncertain)", "Majeur ou mineur – déterminé d'après les notes (? = incertain)" },
    { "Skala", "Scale", "Gamme" },
    { "Tonvorrat: Diatonisch, Pentatonik, Harmonisch Moll, Blues, Chromatisch … und Anzahl genutzter Töne von 12",
      "Tone set: diatonic, pentatonic, harmonic minor, blues, chromatic … and number of the 12 tones used",
      "Réservoir de notes : diatonique, pentatonique, mineur harmonique, blues, chromatique … et nombre de notes utilisées sur 12" },
    { "Anfangstempo in BPM (* = Tempowechsel)", "Initial tempo in BPM (* = tempo changes)", "Tempo initial en BPM (* = changements de tempo)" },
    { "Takt", "Time", "Mesure" },
    { "Taktart", "Time signature", "Signature rythmique" },
    { "Dauer", "Length", "Durée" },
    { "Spieldauer", "Playing time", "Durée de lecture" },
    { "Spuren", "Tracks", "Pistes" },
    { "Anzahl der Spuren", "Number of tracks", "Nombre de pistes" },
    { "Kanäle", "Channels", "Canaux" },
    { "Anzahl benutzter MIDI-Kanäle", "Number of MIDI channels used", "Nombre de canaux MIDI utilisés" },
    { "Noten", "Notes", "Notes" },
    { "Anzahl der Noten", "Number of notes", "Nombre de notes" },
    { "Instrumente", "Instruments", "Instruments" },
    { "General-MIDI-Instrumente (Program Change)", "General MIDI instruments (program change)", "Instruments General MIDI (program change)" },
    { "Kar.Text", "Lyrics", "Paroles" },
    { "Liedtext vorhanden (Anzahl Zeichen)", "Lyrics present (number of characters)", "Paroles présentes (nombre de caractères)" },
    { "Bewertung", "Rating", "Note" },
    { "0–5 Sterne – Klick auf einen Stern (nur in der Datenbank)", "0–5 stars – click a star (database only)", "0–5 étoiles – cliquer sur une étoile (base de données uniquement)" },
    { "Favorit", "Favourite", "Favori" },
    { "Favorit – Klick schaltet um (nur in der Datenbank)", "Favourite – click toggles (database only)", "Favori – un clic bascule (base de données uniquement)" },
    { "Profil", "Profile", "Profil" },
    { "Notengebirge: Notendichte über die Zeit, je Spur eine Farbe gestapelt", "Note mountain: note density over time, one stacked colour per track",
      "Montagne de notes : densité des notes dans le temps, une couleur empilée par piste" },
    { "Dublette", "Duplicate", "Doublon" },
    { "Dubletten-Gruppe: identisch, gleiche Noten oder ähnlich (%)", "Duplicate group: identical, same notes or similar (%)", "Groupe de doublons : identique, mêmes notes ou similaire (%)" },
    { "Kommentar bzw. Texte der Datei – Klick zum Bearbeiten", "Comment or texts of the file – click to edit", "Commentaire ou textes du fichier – cliquer pour modifier" },
    { "Größe", "Size", "Taille" },
    { "Dateigröße in Byte", "File size in bytes", "Taille du fichier en octets" },
    { "SMF-Format 0/1/2 (RMI = RIFF-MIDI)", "SMF format 0/1/2 (RMI = RIFF MIDI)", "Format SMF 0/1/2 (RMI = RIFF MIDI)" },
    { "Tonart (Datei)", "Key (file)", "Tonalité (fichier)" },
    { "In der Datei eingetragene Tonart (Key Signature) – Klick zum Bearbeiten, z. B. a-Moll, Es-Dur, F#m. Rechtsklick: ermittelte Tonart eintragen",
      "Key written in the file (key signature) – click to edit, e.g. A minor, Eb major, F#m. Right-click: write detected key",
      "Tonalité inscrite dans le fichier (key signature) – cliquer pour modifier, p. ex. la mineur, mib majeur, fa# mineur. Clic droit : inscrire la tonalité détectée" },
    { "Prüfsumme der Datei", "Checksum of the file", "Somme de contrôle du fichier" },
    { "Ordner der Datei", "Folder of the file", "Dossier du fichier" },
    { "Geändert", "Modified", "Modifié" },
    { "Letzte Änderung der Datei", "Last change of the file", "Dernière modification du fichier" },
    { "Favorit: %1", "Favourite: %1", "Favori : %1" },
    { "Bewertung: %1", "Rating: %1", "Note : %1" },
    { "%1 in %2 Dateien", "%1 in %2 files", "%1 dans %2 fichiers" },
    { "Online-Angaben: %1", "Online data: %1", "Données en ligne : %1" },
    { "Rückgängig: %1", "Undo: %1", "Annuler : %1" },
    { "Rückgängig", "Undo", "Annuler" },
    { "Strg+Z", "Ctrl+Z", "Ctrl+Z" },
    { "Titel/Interpret: %1", "Title/artist: %1", "Titre/interprète : %1" },
    { "%1 Dateien", "%1 files", "%1 fichiers" },
    { "Nichts rückgängig zu machen", "Nothing to undo", "Rien à annuler" },
    { "Konnte nicht zurückschreiben:", "Could not write back:", "Impossible de réécrire :" },
    { "Rückgängig gemacht: %1", "Undone: %1", "Annulé : %1" },
    // ---- table end
};

const Row* findRow (const std::string& de)
{
    static const auto map = []
    {
        std::unordered_map<std::string, const Row*> m;
        for (auto& r : kRows) m[r.de] = &r;
        return m;
    }();
    auto it = map.find (de);
    return it != map.end() ? it->second : nullptr;
}

juce::String lookup (const char* de)
{
    const auto l = language();
    if (l == Lang::de)
    {
        if (std::strcmp (de, "@about") == 0) return juce::String::fromUTF8 (kAboutDe);
        if (std::strcmp (de, "@tips") == 0) return juce::String::fromUTF8 (kTipsDe);
        return juce::String::fromUTF8 (de);
    }
    if (auto* r = findRow (de)) return juce::String::fromUTF8 (l == Lang::en ? r->en : r->fr);
    return juce::String::fromUTF8 (de);
}
} // namespace

juce::String T (const char* germanUtf8) { return lookup (germanUtf8); }
juce::String TS (const juce::String& german) { return lookup (german.toRawUTF8()); }

// ============================================================================================ analysis values
namespace
{
const char* const kEnMajor[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
const char* const kEnMinor[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
const char* const kFrMajor[12] = { "do", "réb", "ré", "mib", "mi", "fa", "fa#", "sol", "lab", "la", "sib", "si" };
const char* const kFrMinor[12] = { "do", "do#", "ré", "mib", "mi", "fa", "fa#", "sol", "sol#", "la", "sib", "si" };

struct Word { const char* de; const char* en; const char* fr; };
const Word kScales[] = {
    { "Diatonisch", "Diatonic", "Diatonique" }, { "Pentatonik", "Pentatonic", "Pentatonique" },
    { "Harmonisch Moll", "Harmonic minor", "Mineur harmonique" }, { "Blues", "Blues", "Blues" },
    { "Chromatisch", "Chromatic", "Chromatique" }, { "Erweitert", "Extended", "Étendue" } };
const Word kLanguages[] = {
    { "Englisch", "English", "Anglais" }, { "Deutsch", "German", "Allemand" }, { "Italienisch", "Italian", "Italien" },
    { "Französisch", "French", "Français" }, { "Spanisch", "Spanish", "Espagnol" }, { "Niederländisch", "Dutch", "Néerlandais" },
    { "Portugiesisch", "Portuguese", "Portugais" } };

juce::String pick (const Word& w) { return juce::String::fromUTF8 (language() == Lang::en ? w.en : w.fr); }

// English: "F# minor", "Bb major", "Am", "c sharp minor" (B = B natural)
bool parseEnglishKey (const juce::String& input, int& tonic, bool& minor)
{
    auto s = input.trim().toLowerCase().replace ("-", " ").replace (juce::String::fromUTF8 ("♯"), "#").replace (juce::String::fromUTF8 ("♭"), "b");
    if (s.isEmpty()) return false;
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    const auto L = s[0];
    if (L < 'a' || L > 'g') return false;
    int pc = base[L - 'a'];
    auto rest = s.substring (1).trimStart();
    if (rest.startsWith ("sharp")) { ++pc; rest = rest.substring (5).trimStart(); }
    else if (rest.startsWith ("flat")) { --pc; rest = rest.substring (4).trimStart(); }
    else if (rest.startsWith ("#")) { ++pc; rest = rest.substring (1).trimStart(); }
    else if (rest.startsWith ("b")) { --pc; rest = rest.substring (1).trimStart(); }
    if (rest.isEmpty() || rest == "maj" || rest == "major") minor = false;
    else if (rest == "m" || rest == "min" || rest == "minor") minor = true;
    else return false;
    tonic = (pc % 12 + 12) % 12;
    return true;
}

// French: "fa# mineur", "si bémol majeur", "réb", "la m"
bool parseFrenchKey (const juce::String& input, int& tonic, bool& minor)
{
    auto s = input.trim().toLowerCase().replace ("-", " ").replace (juce::String::fromUTF8 ("é"), "e")
                  .replace (juce::String::fromUTF8 ("♯"), "#").replace (juce::String::fromUTF8 ("♭"), "b");
    struct N { const char* name; int pc; };
    static const N names[] = { { "sol", 7 }, { "do", 0 }, { "ut", 0 }, { "re", 2 }, { "mi", 4 }, { "fa", 5 }, { "la", 9 }, { "si", 11 } };
    int pc = -1;
    juce::String rest;
    for (auto& n : names)
        if (s.startsWith (n.name)) { pc = n.pc; rest = s.substring ((int) std::strlen (n.name)).trimStart(); break; }
    if (pc < 0) return false;
    if (rest.startsWith ("diese")) { ++pc; rest = rest.substring (5).trimStart(); }
    else if (rest.startsWith ("bemol")) { --pc; rest = rest.substring (5).trimStart(); }
    else if (rest.startsWith ("#")) { ++pc; rest = rest.substring (1).trimStart(); }
    else if (rest.startsWith ("b")) { --pc; rest = rest.substring (1).trimStart(); }
    if (rest.isEmpty() || rest == "majeur" || rest == "maj") minor = false;
    else if (rest == "mineur" || rest == "min" || rest == "m") minor = true;
    else return false;
    tonic = (pc % 12 + 12) % 12;
    return true;
}
} // namespace

juce::String localKey (const juce::String& germanKey)
{
    if (germanKey.isEmpty() || language() == Lang::de) return germanKey;
    const bool unsure = germanKey.endsWithChar ('?');
    int tonic = 0; bool minor = false;
    if (! parseKeyName (germanKey.trimCharactersAtEnd ("?"), tonic, minor)) return germanKey;
    juce::String out;
    if (language() == Lang::en) out << (minor ? kEnMinor : kEnMajor)[tonic] << (minor ? " minor" : " major");
    else out << juce::String::fromUTF8 ((minor ? kFrMinor : kFrMajor)[tonic]) << (minor ? " mineur" : " majeur");
    return unsure ? out + "?" : out;
}

juce::String localMode (const juce::String& germanKey)
{
    const juce::String q = germanKey.endsWithChar ('?') ? "?" : "";
    const bool minor = germanKey.contains ("Moll"), major = germanKey.contains ("Dur");
    if (! minor && ! major) return {};
    switch (language())
    {
        case Lang::en: return (minor ? "Minor" : "Major") + q;
        case Lang::fr: return (minor ? "Mineur" : "Majeur") + q;
        default: return (minor ? "Moll" : "Dur") + q;
    }
}

juce::String localScale (const juce::String& germanScale)
{
    if (language() == Lang::de || germanScale.isEmpty()) return germanScale;
    const auto sep = juce::String::fromUTF8 (" · ");
    const auto name = germanScale.upToFirstOccurrenceOf (sep, false, false);
    const auto rest = germanScale.fromFirstOccurrenceOf (sep, true, false);
    for (auto& w : kScales) if (name == w.de) return pick (w) + rest;
    return germanScale;
}

juce::String localLanguageName (const juce::String& germanName)
{
    if (language() == Lang::de || germanName.isEmpty()) return germanName;
    for (auto& w : kLanguages) if (germanName == juce::String::fromUTF8 (w.de)) return pick (w);
    return germanName;
}

juce::String localDate (juce::int64 millis)
{
    const juce::Time t (millis);
    switch (language())
    {
        case Lang::en: return t.formatted ("%Y-%m-%d %H:%M");
        case Lang::fr: return t.formatted ("%d/%m/%Y %H:%M");
        default: return t.formatted ("%d.%m.%Y %H:%M");
    }
}

juce::String keyInputToGerman (const juce::String& input)
{
    int tonic = 0; bool minor = false;
    const auto in = input.trim().trimCharactersAtEnd ("?");
    if (in.isEmpty()) return input;
    if (language() == Lang::en && parseEnglishKey (in, tonic, minor)) return keyNameOf (tonic, minor);
    if (language() == Lang::fr && (parseFrenchKey (in, tonic, minor) || parseEnglishKey (in, tonic, minor))) return keyNameOf (tonic, minor);
    return input;   // German spelling ("fis-Moll", "F#m", "Es-Dur") is understood by the writer in every language
}
} // namespace ma
