#!/usr/bin/env python3
"""Writes the German translation into i18n/blokkily_de.ts.

lupdate gathers the window's strings into the .ts file (`cmake --build build
--target update_translations`); this fills each one it knows. A string with
no entry here stays unfinished and reads in English. Labels on chips stay
about as long as the English, so they fit where the English fits.
"""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

DE = {
    # Arrangement and clips
    "ARRANGEMENT": "ARRANGEMENT",
    "click places on every stem  |  ctrl-click one stem  |  shift-click lengthens  |  alt-click moves  |  right-click removes":
        "Klick setzt auf jede Spur  |  Strg-Klick auf eine  |  Umschalt-Klick verlängert  |  Alt-Klick verschiebt  |  Rechtsklick entfernt",
    "+PAT": "+PAT", "DUP": "DUP", "+TRK": "+SPR", "+AUD": "+AUD",
    "TEMPO": "TEMPO", "AUTO": "AUTO",
    "Import audio": "Audio importieren",
    "MISSING ": "FEHLT ", "RENDERING…": "RENDERT…", "TAKE ": "TAKE ",
    "no lane: move a control in TOUCH, LATCH or WRITE while playing, or double-click":
        "keine Spur: einen Regler in TOUCH, LATCH oder WRITE beim Abspielen bewegen, oder doppelklicken",
    "double-click adds a tempo point": "Doppelklick setzt einen Tempopunkt",
    # Warp
    "WARP": "WARP", "FOLLOW TEMPO": "TEMPO FOLGEN", "READY": "BEREIT", "OFF": "AUS",
    "SOURCE BPM": "QUELL-BPM", "DETECT": "ERKENNEN", "STRETCH ×": "DEHNUNG ×",
    "PITCH": "TONHÖHE", "ST": "HT", "CT": "CT", "RESET": "ZURÜCKSETZEN",
    # Effects
    "INSERTS": "INSERTS", "+ FX": "+ FX", "RESAMPLE": "RESAMPLE", "STEM": "STEM",
    "Bounce to new track": "Auf neue Spur bouncen",
    "Bounce and mute source": "Bouncen und Quelle stummschalten",
    "BYP": "BYP", "SIDECHAIN": "SIDECHAIN", "NONE": "KEINE",
    "No key": "Kein Key", "Key from ": "Key von ",
    "No inserts. Choose + FX, then an effect.": "Keine Inserts. + FX wählen, dann einen Effekt.",
    # Inspector
    "STEP ": "SCHRITT ", " IS EMPTY": " IST LEER",
    "Click the step to place a note, then shape it here":
        "Schritt anklicken, um eine Note zu setzen, dann hier formen",
    "LEN": "LÄNGE", "VELOCITY": "ANSCHLAG", "MICRO": "MIKRO", "STRUM": "STRUM",
    " TK": " TK", "chords": "Akkorde", "PROBABILITY": "WAHRSCHEINLICHKEIT",
    "RATCHET": "RATCHET", "INVERSION": "UMKEHRUNG", "▼ INV": "▼ UMK", "INV ▲": "UMK ▲",
    "PARAMETER LOCK": "PARAMETER-LOCK", "MOD": "MOD", "CLEAR": "LEEREN", "track": "Spur",
    "ON": "AN", "VALUE": "WERT", "none": "keiner", "EVERY": "JEDES", "LOOP": "LOOP",
    "VOICE VELOCITY": "STIMMEN-ANSCHLAG", "DRAG ↕": "ZIEHEN ↕",
    # Devices
    "DEVICES": "GERÄTE", "NO TRACK": "KEINE SPUR", "MIDI IN": "MIDI EIN", "UPPER": "OBEN",
    "MPE": "MPE", "No MIDI input": "Kein MIDI-Eingang", "None": "Keiner",
    "MIDI OUT": "MIDI AUS", "OFFSET": "VERSATZ", "SMP": "SMP", "ARP": "ARP",
    "LATCH": "HALTEN", "PLUGINS": "PLUGINS", "INSTR": "INSTR", "FX": "FX",
    "Find effect": "Effekt suchen", "Find instrument": "Instrument suchen",
    "No effect matches “%1”": "Kein Effekt passt zu „%1“",
    "No instrument matches “%1”": "Kein Instrument passt zu „%1“",
    "INT": "INT",
    # Keyboard
    "TUNING": "STIMMUNG", " STEPS": " STUFEN", "SCALE": "SKALA", "ROOT": "GRUNDTON",
    "AUTO-SCALE": "AUTO-SKALA", "PLAYED ": "GESPIELT ", "SURFACE": "FLÄCHE",
    "RUNS ": "LÄUFT ", "WRITING STEP ": "SCHREIBT SCHRITT ",
    # Launcher
    "SCENE LAUNCHER": "SZENEN-LAUNCHER", "SCENE QUANT": "SZENEN-QUANT",
    "REC ARRANGEMENT": "ARRANGEMENT AUFN.", "+ SCENE": "+ SZENE", "STOP ALL": "ALLE STOPP",
    "No scenes. Add one, then click a cell to put the open pattern in it.":
        "Keine Szenen. Eine hinzufügen, dann eine Zelle anklicken, um das offene Pattern hineinzulegen.",
    "■ STOP": "■ STOPP", "LOOPS": "LOOPS", "THEN": "DANN", "QUANT": "QUANT", "SONG": "SONG",
    # Loop controls
    "IN": "EIN", "OUT": "AUS", "PUNCH": "PUNCH", "TAKES": "TAKES", "SPLIT": "TEILEN",
    # Window and dialogs
    " — Blokkily": " — Blokkily",
    "Discard unsaved changes?": "Ungesicherte Änderungen verwerfen?",
    "“%1” has changes that are not saved.": "„%1“ hat Änderungen, die nicht gesichert sind.",
    "RECOVER UNSAVED WORK": "UNGESICHERTE ARBEIT WIEDERHERSTELLEN",
    "“%1” was not saved when Blokkily last closed. It was autosaved %2.":
        "„%1“ war beim letzten Schließen von Blokkily nicht gesichert. Automatisch gesichert: %2.",
    "Untitled": "Unbenannt", "DISCARD": "VERWERFEN", "RECOVER": "WIEDERHERSTELLEN",
    "RENAME ": "UMBENENNEN: ", "Rename…": "Umbenennen…", "Duplicate": "Duplizieren",
    "Clear steps": "Schritte leeren", "Delete": "Löschen", "Mute": "Stumm", "Solo": "Solo",
    "MIDI learn gain": "MIDI-Learn Pegel", "MIDI learn pan": "MIDI-Learn Pan",
    "MIDI learn mute": "MIDI-Learn Stumm", "MIDI learn solo": "MIDI-Learn Solo",
    "Forget MIDI controls": "MIDI-Zuweisungen vergessen",
    "Open Blokkily project in a new window": "Blokkily-Projekt in neuem Fenster öffnen",
    "Open Blokkily project": "Blokkily-Projekt öffnen",
    "Save Blokkily project": "Blokkily-Projekt sichern",
    "Bounce the arrangement": "Arrangement bouncen",
    "Wave audio (*.wav)": "Wave-Audio (*.wav)",
    "Export every track as a file of its own": "Jede Spur als eigene Datei exportieren",
    "Render an oscilloscope video of the tracks and the master":
        "Ein Oszilloskop-Video der Spuren und des Masters rendern",
    "Import MIDI after the arrangement (keep song tempo)":
        "MIDI hinter dem Arrangement importieren (Songtempo behalten)",
    "Export arrangement notes and controllers": "Noten und Controller des Arrangements exportieren",
    "Open a song from another DAW": "Einen Song aus einer anderen DAW öffnen",
    "Export the song for another DAW": "Den Song für eine andere DAW exportieren",
    "SPACE play  |  HOME rewind  |  ARROWS move  |  SHIFT+UP/DOWN transpose  |  INSERT push  |  ALT nudge  |  CTRL+D duplicate  |  CTRL+M mute  |  CTRL+L solo":
        "LEER spielt  |  POS1 zurück  |  PFEILE bewegen  |  UMSCH+AUF/AB transponiert  |  EINFG schiebt  |  ALT verrückt  |  STRG+D dupliziert  |  STRG+M stumm  |  STRG+L solo",
    "HELP": "HILFE", "GUIDE": "GUIDE",
    "%1 has stopped and is silenced. The song and everything else play on.":
        "%1 ist ausgefallen und stummgeschaltet. Der Song und alles andere spielen weiter.",
    "%1 have stopped and are silenced. The song and everything else play on.":
        "%1 sind ausgefallen und stummgeschaltet. Der Song und alles andere spielen weiter.",
    "RELOAD": "NEU LADEN",
    # Hints on hover
    "Arm recording (Ctrl+R)": "Aufnahme scharf schalten (Strg+R)",
    "Automation: READ plays lanes, TOUCH / LATCH / WRITE record moves, OFF ignores them. Click: next, right-click: back":
        "Automation: READ spielt Spuren ab, TOUCH / LATCH / WRITE nehmen Bewegungen auf, OFF ignoriert sie. Klick: weiter, Rechtsklick: zurück",
    "Back to the start (Home)": "Zurück zum Anfang (Pos1)",
    "Bypass: pass the sound through this effect untouched": "Bypass: der Klang geht unverändert durch diesen Effekt",
    "Clear the whole song. UNDO brings it back": "Den ganzen Song leeren. RÜCK holt ihn zurück",
    "Count-in bars before recording. Click: more, right-click: fewer": "Vorzähltakte vor der Aufnahme. Klick: mehr, Rechtsklick: weniger",
    "Duplicate the open pattern as a variation": "Das offene Pattern als Variation duplizieren",
    "Freeze: render the instrument and inserts to audio to save CPU; again to unfreeze":
        "Einfrieren: Instrument und Inserts zu Audio rendern, um CPU zu sparen; erneut zum Auftauen",
    "Import an audio file onto a new track": "Eine Audiodatei auf eine neue Spur importieren",
    "Metronome click while playing and recording": "Metronom-Klick beim Abspielen und Aufnehmen",
    "Mute (Ctrl+M on the selected track)": "Stumm (Strg+M auf der gewählten Spur)",
    "New instrument track": "Neue Instrumentenspur",
    "New pattern, as long as the bar under the playhead": "Neues Pattern, so lang wie der Takt unter dem Abspielkopf",
    "Open the instrument's own window": "Das eigene Fenster des Instruments öffnen",
    "Play / pause (Space)": "Abspielen / Pause (Leertaste)",
    "Record this rack's output to a new audio track while the song plays":
        "Den Ausgang dieses Racks beim Abspielen auf eine neue Audiospur aufnehmen",
    "Redo (Ctrl+Shift+Z)": "Wiederholen (Strg+Umschalt+Z)",
    "Render this rack to audio on a new track": "Dieses Rack zu Audio auf einer neuen Spur rendern",
    "Show effects in the browser: a click there inserts it here": "Effekte im Browser zeigen: ein Klick dort fügt ihn hier ein",
    "Solo: hear only soloed tracks (Ctrl+L on the selected track)": "Solo: nur Solo-Spuren hören (Strg+L auf der gewählten Spur)",
    "The master went over 0 dB. Click to clear": "Der Master lag über 0 dB. Klicken zum Löschen",
    "Undo (Ctrl+Z)": "Rückgängig (Strg+Z)",
    "Zoom the arrangement in": "Arrangement vergrößern",
    "Zoom the arrangement out": "Arrangement verkleinern",
    "Dismiss": "Schließen",
    # The keys sheet
    "TRANSPORT": "TRANSPORT", "Close": "Schließen", "Keyboard shortcuts": "Tastenkürzel",
    "Keyboard shortcuts (Ctrl+/)": "Tastenkürzel (Strg+/)",
    "STEPS AND TRACKER": "SCHRITTE UND TRACKER", "FILE": "DATEI",
    "Play or stop": "Abspielen oder anhalten",
    "Return or Home": "Eingabe oder Pos1",
    "Go to the start of the song": "Zum Anfang des Songs",
    "Arm recording": "Aufnahme scharf schalten",
    "Mute the selected track": "Die gewählte Spur stummschalten",
    "Solo the selected track": "Die gewählte Spur solo schalten",
    "Arrow keys": "Pfeiltasten",
    "Move the step cursor": "Den Schrittcursor bewegen",
    "Transpose the step by a semitone": "Den Schritt um einen Halbton transponieren",
    "Transpose the step by an octave": "Den Schritt um eine Oktave transponieren",
    "Change the tracker entry octave": "Die Eingabeoktave des Trackers ändern",
    "Next / previous tracker channel": "Nächster / vorheriger Tracker-Kanal",
    "Backspace or Delete": "Rücktaste oder Entf",
    "Clear a tracker row": "Eine Trackerzeile leeren",
    "Push later rows down / pull them up": "Spätere Zeilen nach unten schieben / hochziehen",
    "Alt+arrows": "Alt+Pfeile",
    "Nudge micro-timing and note length": "Mikro-Timing und Notenlänge verschieben",
    "Copy / paste the selected step": "Den gewählten Schritt kopieren / einfügen",
    "Duplicate the step onto the next row": "Den Schritt in die nächste Zeile duplizieren",
    "Toggle a step": "Einen Schritt umschalten",
    "Save / save to a new file": "Sichern / in eine neue Datei sichern",
    "New session / open a project": "Neue Session / ein Projekt öffnen",
    "New window / open in a new window": "Neues Fenster / in neuem Fenster öffnen",
    "Bounce the arrangement to disk": "Das Arrangement auf die Platte bouncen",
    "Undo / redo": "Rückgängig / wiederholen",
    "Bring the left / right bar out, or send it back": "Die linke / rechte Leiste hervorholen oder zurückschicken",
    "Search the plugin browser": "Im Plugin-Browser suchen",
    "Ask the assistant": "Den Assistenten fragen",
    "Start or leave the guided tour": "Die Tour starten oder verlassen",
    "Show or hide these keys": "Diese Tasten zeigen oder verbergen",
    "AUTOSAVE": "AUTOSPEICH.", "10 S": "10 S", "30 S": "30 S", "2 MIN": "2 MIN",
    "unsaved work is not copied aside: a crash loses what was not saved":
        "Ungesichertes wird nicht beiseitegelegt: ein Absturz verliert, was nicht gesichert war",
    "unsaved work is copied aside this often, and offered back after a crash":
        "Ungesichertes wird so oft beiseitegelegt und nach einem Absturz wieder angeboten",
    "WHOLE SONG": "GANZER SONG", "LOOP ONLY": "NUR LOOP",
    "Export only the loop's bars, from IN to OUT": "Nur die Takte des Loops exportieren, von EIN bis AUS",
    "Double-click to reset": "Doppelklick setzt zurück",
    "Click level %1 dB": "Klick-Pegel %1 dB",
    # Mixer
    "MIXER": "MIXER", "CLICK": "KLICK", "NO CI": "VZ –", "CI ": "VZ ",
    " TRACK": " SPUR", " TRACKS": " SPUREN", "+ RETURN": "+ RETURN", "MASTER": "MASTER",
    "CLIP": "CLIP", "OK": "OK", "LEVEL": "PEGEL", "FROZEN · ": "EINGEFROREN · ",
    "FRZ": "FRZ", "+ OUT": "+ AUS", "AUX ": "AUX ", "GAIN": "PEGEL", "PAN": "PAN",
    "RETURN": "RETURN", " insert": " Insert", " inserts": " Inserts",
    "SEND ": "SEND ", "PRE": "PRE", "off": "aus",
    # Modulation
    "MODULATION": "MODULATION", "+ LFO": "+ LFO", "+ MACRO": "+ MAKRO", "+ FOLLOW": "+ FOLGER",
    "No modulators. Add an LFO, a macro or a follower, then aim it at a parameter.":
        "Keine Modulatoren. Einen LFO, ein Makro oder einen Folger hinzufügen, dann auf einen Parameter richten.",
    "LEARN": "LERNEN", "+ TARGET": "+ ZIEL", "SYNC": "SYNC",
    "PER CYCLE · FOLLOWS THE TEMPO": "PRO ZYKLUS · FOLGT DEM TEMPO", "RATE": "RATE",
    "Not aimed yet: + TARGET picks a parameter of this track.":
        "Noch kein Ziel: + ZIEL wählt einen Parameter dieser Spur.",
    # Notes
    "NOTES": "NOTIZEN",
    "saved with the song · Escape returns to the editors": "wird mit dem Song gesichert · Esc kehrt zu den Editoren zurück",
    "Lyrics, ideas, what to try next…": "Texte, Ideen, was als Nächstes zu probieren ist…",
    # Pattern header
    "QUANT ": "QUANT ",
    "Move every step onto the grid. Right-click: grid. Shift: halfway.":
        "Jeden Schritt aufs Raster setzen. Rechtsklick: Raster. Umschalt: zur Hälfte.",
    "HUMAN": "HUMAN",
    "Loosen every step's timing (±8 ticks) and velocity (±0.08).":
        "Timing (±8 Ticks) und Anschlag (±0,08) jedes Schritts lockern.",
    " ST": " SCH", "SWING": "SWING", "EDITOR": "EDITOR",
    # Project
    "PROJECT": "PROJEKT", "COLLECT": "PACKEN", "NEW": "NEU", "OPEN": "ÖFFNEN",
    "SAVE": "SICHERN", "SAVE AS": "SICHERN ALS", "+ WINDOW": "+ FENSTER", "OPEN NEW": "NEU ÖFFNEN",
    "EXPORT": "EXPORT", "DITHER": "DITHER", "+ CLICK": "+ KLICK", "BOUNCE": "BOUNCE",
    "STEMS": "STEMS", "SCOPE VIDEO": "SCOPE-VIDEO", "DAWPROJ IN": "DAWPROJ IN",
    "DAWPROJ OUT": "DAWPROJ OUT",
    # Assistant
    "ASSISTANT": "ASSISTENT",
    "Where the prompt is answered: Ollama on this machine, or a cloud model you have keyed":
        "Wo die Anfrage beantwortet wird: Ollama auf diesem Rechner oder ein Cloud-Modell mit Schlüssel",
    "Apply or discard the proposal below.": "Den Vorschlag unten übernehmen oder verwerfen.",
    "Ask for a pattern — “a minor pentatonic ascending”, “offbeat hats”, “make this jazzier”…":
        "Pattern erfragen — „Moll-Pentatonik aufwärts“, „Offbeat-Hats“…",
    "ASK": "LOS", "APPLY": "ÜBERNEHMEN",
    "Ctrl+K to focus · Return to send · Escape to leave": "Strg+K fokussiert · Eingabe sendet · Esc verlässt",
    # Sampler
    "Load a sample": "Ein Sample laden",
    "Import a multi-sampled instrument (key and layer from each file's name)":
        "Ein Multisample-Instrument importieren (Taste und Schicht aus jedem Dateinamen)",
    "KEYS": "TASTEN", "KIT": "KIT", "no sample": "kein Sample", "LOAD": "LADEN",
    "MULTI": "MULTI", "EDIT": "BEARB.", "File missing — load it again": "Datei fehlt — erneut laden",
    "LOW": "TIEF", "HIGH": "HOCH", "VEL": "ANS", "FWD": "VOR", "P-P": "P-P",
    "SLICES": "SLICES", "CHOP": "SCHNEIDEN",
    # Score
    "Import a MusicXML score as a new section": "Eine MusicXML-Partitur als neuen Teil importieren",
    "Export the song as a MusicXML score": "Den Song als MusicXML-Partitur exportieren",
    "SCORE": "NOTEN", "SCORE IN": "NOTEN EIN", "SCORE OUT": "NOTEN AUS", "FULL": "ALLE",
    "CHANT": "GESANG", "LYRICS": "TEXT", "A-ma-zing grace how sweet the sound": "Stil-le Nacht, hei-li-ge Nacht",
    "NOTE": "NOTE", "DOT": "PUNKT", "DEL": "ENTF",
    "click the staff to write · ↑↓ move · shift octave · del": "Notensystem anklicken zum Schreiben · ↑↓ bewegt · Umschalt Oktave · Entf",
    # Settings
    "SETTINGS": "EINSTELLUNGEN",
    "this machine's · remembered between launches, not saved with the song":
        "für diesen Rechner · bleibt zwischen Starts erhalten, wird nicht mit dem Song gesichert",
    "AUDIO": "AUDIO", "SYSTEM": "SYSTEM",
    "PipeWire: JACK (through pipewire-jack) or PULSE. Windows: WASAPI, DirectSound or ASIO. Mac: Core Audio. Only what this build can open is listed.":
        "PipeWire: JACK (über pipewire-jack) oder PULSE. Windows: WASAPI, DirectSound oder ASIO. Mac: Core Audio. Aufgeführt ist nur, was dieser Build öffnen kann.",
    "OUTPUT": "AUSGANG", "REFRESH": "AKTUALISIEREN", "INPUT": "EINGANG",
    "opened only while a track records audio input": "nur geöffnet, solange eine Spur Audio aufnimmt",
    "SAMPLE RATE": "SAMPLERATE", "set by the JACK server": "vom JACK-Server bestimmt",
    "asked for; the device may run at another": "erbeten; das Gerät kann mit einer anderen laufen",
    "BUFFER": "PUFFER",
    "smaller answers sooner, larger is safer against dropouts (":
        "kleiner reagiert schneller, größer schützt besser vor Aussetzern (",
    "RUNNING": "LÄUFT", "MIDI": "MIDI", "CLOCK OUT": "CLOCK AUS",
    "drum machines and sequencers on this port follow the song":
        "Drumcomputer und Sequenzer an diesem Port folgen dem Song",
    "EXT": "EXT", "ISOLATE": "ISOLIEREN",
    "each plugin runs in a process of its own: one that crashes or hangs goes silent and can be reloaded, and Blokkily keeps running. Costs one block of latency, which is compensated":
        "jedes Plugin läuft in einem eigenen Prozess: stürzt eines ab oder hängt, verstummt es und kann neu geladen werden, und Blokkily läuft weiter. Kostet einen Block Latenz, der ausgeglichen wird",
    "not available in this build: plugins run inside Blokkily": "in diesem Build nicht verfügbar: Plugins laufen in Blokkily",
    "WINDOW": "FENSTER", "AUTO-HIDE": "AUTO-AUSBL.", "LEFT BAR": "LINKS", "RIGHT BAR": "RECHTS",
    "a hidden bar comes back when the pointer rests on its edge, and stays while it is used":
        "eine ausgeblendete Leiste kommt zurück, wenn der Zeiger an ihrem Rand ruht, und bleibt, solange sie benutzt wird",
    "DOCK": "ANDOCKEN", "WHEN WIDE": "WENN BREIT",
    "on, a window with room for both bars beside the editors docks them; off, they auto-hide at any width":
        "an: ein Fenster mit Platz für beide Leisten neben den Editoren dockt sie an; aus: sie blenden sich bei jeder Breite aus",
    "THEME": "DESIGN", "DARK": "DUNKEL", "LIGHT": "HELL",
    "SYSTEM follows the desktop's light or dark setting": "SYSTEM folgt der hellen oder dunklen Einstellung des Desktops",
    "LANGUAGE": "SPRACHE",
    "SYSTEM follows the desktop's language, in English where there is no translation":
        "SYSTEM folgt der Sprache des Desktops, auf Englisch, wo es keine Übersetzung gibt",
    "LAYOUT": "LAYOUT",
    "drag a panel's grip in the left margin to move it, the gap under it to size it, and a bar's inner edge to widen it":
        "den Griff eines Bereichs am linken Rand ziehen, um ihn zu verschieben, die Lücke darunter, um seine Größe zu ändern, und die Innenkante einer Leiste, um sie zu verbreitern",
    # Step editors
    "STEP GRID": "SCHRITTRASTER", "FOLLOW": "FOLGEN",
    "click toggles  |  shift-click selects  |  CTRL-click seeks  |  CTRL+1..0 toggles":
        "Klick schaltet  |  Umschalt-Klick wählt  |  STRG-Klick springt  |  STRG+1..0 schaltet",
    "TRACKER": "TRACKER", "OCT-": "OKT-", "OCT+": "OKT+",
    "TYPE ZSXDCVGBHNJM  ·  TAB CHANNEL  ·  DRAG VEL/FX": "TIPPE ZSXDCVGBHNJM  ·  TAB KANAL  ·  ZIEHE VEL/FX",
    "ROW": "ZEILE", "PIANO ROLL": "PIANOROLLE",
    "drag draws  |  drag a note to move  |  drag its end to lengthen  |  right-click erases":
        "Ziehen zeichnet  |  Note ziehen verschiebt  |  ihr Ende ziehen verlängert  |  Rechtsklick löscht",
    " KEYS": " TASTEN", "CTRL": "CTRL", "CC ": "CC ",
    "select a note to draw its own expression": "eine Note wählen, um ihren Ausdruck zu zeichnen",
    "drag sets each note's velocity": "Ziehen setzt den Anschlag jeder Note",
    "drag draws  |  drag a point to move  |  right-click erases":
        "Ziehen zeichnet  |  Punkt ziehen verschiebt  |  Rechtsklick löscht",
    " PTS": " PKT", "ARMED": "SCHARF",
    # Transport
    "BAR": "TAKT", "BPM": "BPM", "UNDO": "RÜCK", "REDO": "VOR",
    "ALL": "ALLE", "STEP": "STEP", "PIANO": "PIANO", "LAUNCH": "LAUNCH", "VERSIONS": "VERSIONEN",
    "TRK": "TRK", "ROLL": "ROLLE", "LNCH": "LNCH", "VERS": "VERS",
    "STP": "SCH", "ROL": "ROL", "SCR": "NOT", "KEY": "TAS", "LNC": "LNC", "VER": "VER", "NTS": "NTZ",
    "SCANNING…": "SUCHT…", "SCAN": "SUCHE", "RESCAN": "SUCHEN", "RESCAN PLUGINS": "PLUGINS SUCHEN",
    # Tour
    "Half T-block, half broccoli, all groove. Let me show you around, or press ASK or F1 to skip straight to asking me. Esc or X sends me away.":
        "Halb T-Block, halb Brokkoli, ganz Groove. Ich zeige dir alles, oder du drückst FRAGEN oder F1 und fragst mich gleich. Esc oder X schickt mich weg.",
    "Play, record and rewind the song from here.": "Von hier spielst, nimmst auf und spulst den Song zurück.",
    "Drag it up or down, scroll it, or double-click to type a tempo.":
        "Nach oben oder unten ziehen, scrollen oder doppelklicken, um ein Tempo einzutippen.",
    "Step grid, tracker and piano roll are one pattern seen three ways. ALL shows them side by side.":
        "Schrittraster, Tracker und Pianorolle sind ein Pattern aus drei Blickwinkeln. ALLE zeigt sie nebeneinander.",
    "Patterns placed on bars make the song. Right-click a pattern or a track for its menu.":
        "Auf Takte gesetzte Patterns ergeben den Song. Rechtsklick auf ein Pattern oder eine Spur öffnet ihr Menü.",
    "LOOP wraps playback between IN and OUT. Shift-click a bar number for IN, Ctrl-click for OUT. Export still writes the whole song.":
        "LOOP wiederholt zwischen EIN und AUS. Umschalt-Klick auf eine Taktzahl für EIN, Strg-Klick für AUS. Der Export schreibt trotzdem den ganzen Song.",
    "Click a step to light it. Type notes like a tracker: Z to M is one octave, Q to I the next.":
        "Einen Schritt anklicken, um ihn zu setzen. Noten wie im Tracker tippen: Z bis M ist eine Oktave, Q bis I die nächste.",
    "What the selected track plays. Search the browser for a CLAP, VST3 or SoundFont.":
        "Was die gewählte Spur spielt. Im Browser nach einem CLAP, VST3 oder SoundFont suchen.",
    "Every track has a fader, pan, mute and solo. You hear moves straight away.":
        "Jede Spur hat Fader, Pan, Stumm und Solo. Änderungen hörst du sofort.",
    "Ask for a beat or a bassline in plain words and approve what comes back.":
        "In einfachen Worten nach einem Beat oder einer Basslinie fragen und das Ergebnis annehmen.",
    "Press HELP whenever you want me back. Next, ask me anything.":
        "Drück HILFE, wann immer du mich zurückwillst. Jetzt frag mich, was du willst.",
    "CANCEL": "ABBRECHEN", "BACK": "ZURÜCK", "DONE": "FERTIG", "NEXT": "WEITER",
    # Versions
    "differs: ": "anders: ", "none saved yet": "noch keine gesichert",
    " saved with this project": " mit diesem Projekt gesichert",
    "Name this version — “before the new chorus”, “mix 2”…": "Diese Version benennen — „vor dem neuen Refrain“, „Mix 2“…",
    "SAVE VERSION": "VERSION SICHERN",
    "Save a version before a big change. Restoring one later is a single step UNDO can take back, and the song it replaces is kept as a version too.":
        "Vor einer großen Änderung eine Version sichern. Eine spätere Wiederherstellung ist ein Schritt, den RÜCK rückgängig macht, und der ersetzte Song bleibt ebenfalls als Version erhalten.",
    "Double-click to rename": "Doppelklick zum Umbenennen",
    "RENAME": "UMBENENNEN", "RESTORE": "HOLEN", "SURE?": "SICHER?", "DELETE": "LÖSCHEN",
}


# The view tabs' abbreviations, for a transport bar short of room: German
# keeps them as short as the English so the bar fits the same window.
SHORT = {
    "ALL": "ALLE", "STEP": "STEP", "TRK": "TRK", "ROLL": "ROLL", "SCORE": "NOTEN",
    "KEYS": "TAST", "LNCH": "LNCH", "VERS": "VERS", "NOTE": "NTZ",
    "STP": "STP", "ROL": "ROL", "SCR": "NOT", "KEY": "TAS", "LNC": "LNC", "VER": "VER", "NTS": "NTZ",
}
BY_COMMENT = {"short view tab": SHORT, "empty the whole song": {"CLEAR": "LEER"},
              "skip the tour to the guide's question box": {"ASK": "FRAGEN"}}


def main(path: Path) -> int:
    tree = ET.parse(path)
    missing = []
    for message in tree.getroot().iter("message"):
        source = message.find("source").text or ""
        comment = message.find("comment")
        table = BY_COMMENT.get(comment.text, {}) if comment is not None else DE
        translation = message.find("translation")
        if source in table:
            translation.text = table[source]
            translation.attrib.pop("type", None)
        else:
            missing.append(source)
    body = ET.tostring(tree.getroot(), encoding="unicode")
    path.write_text('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + body + "\n", encoding="utf-8")
    for source in sorted(set(missing)):
        print(f"untranslated: {source!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("blokkily_de.ts")))
