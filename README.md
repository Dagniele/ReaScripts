# Hommelsound — ReaScripts

A collection of REAPER scripts for MIDI analysis and visualization.

## Generate Chord Track

Analyzes a selected MIDI item and automatically generates a **Chords** track with the corresponding chord symbols. Useful as a visual harmony guide above (or alongside) your project's MIDI tracks.

### What it does

1. Reads all notes from the selected MIDI item (muted notes are ignored).
2. Splits the timeline into time segments based on note onsets and releases.
3. For each segment, identifies the active chord by analyzing **pitch classes** (modulo 12), so it works correctly even when notes span different octaves.
4. Creates or reuses a track named **Chords** and inserts text items with the chord name (e.g. `C`, `Am`, `G7`, `C/E`, `Dm7/G`).

### Recognized chords

The script recognizes a wide dictionary of voicings, including:

| Type | Examples |
|------|----------|
| Dyads (2 notes) | major, minor, and their inversions |
| Triads | major, minor, diminished, augmented, sus2, sus4 |
| Power chords | `C5`, `C5/G` |
| Sevenths | maj7, m7, 7, dim7, m7b5 and their inversions |
| Extensions | add9, m(add9) |

**Inversions** are written as slash chords (e.g. `C/E` = C major with E in the bass). If a voicing is not in the dictionary, it is shown as `?`.

### Requirements

- [REAPER](https://www.reaper.fm/) 6.x or later
- A MIDI item selected in the timeline

### Installation

#### Via ReaPack (recommended)

1. Open **Extensions → ReaPack → Import repositories**.
2. Add the repository URL:
   ```
   https://github.com/Dagniele/ReaScripts/raw/main/index.xml
   ```
3. Go to **Extensions → ReaPack → Synchronize packages**.
4. The script will appear in **Actions** under the **MIDI** category.

#### Manual

1. Copy `midi/Generate_Chord_Track.lua` into your REAPER scripts folder.
2. In REAPER: **Actions → Show action list → ReaScript: Load** and select the file.

### How to use

1. Select a **MIDI item** in the timeline (piano roll, guitar, synth, etc.).
2. Run the **Generate Chord Track** script (from Actions or a assigned shortcut).
3. The script creates the **Chords** track at the top of the project (if it doesn't already exist) and fills the selected item's time range with chord symbols.

#### Overwrite behavior

If chord items already exist in the same time range on the Chords track, a dialog appears:

| Button | Behavior |
|--------|----------|
| **Yes** | Overwrites existing chords in the analyzed area |
| **No** | Keeps existing chords and only fills empty gaps |
| **Cancel** | Aborts the operation |

The entire operation can be undone with **Ctrl+Z** / **Cmd+Z** (undo entry: *Generate Chord Track*).

### Practical example

You have a piano track with a MIDI item playing `C – E – G` followed by `A – C – E`:

```
Piano:   [C E G────────] [A C E──────]
Chords:  [C─────────────] [Am─────────]
```

If the bass plays the third (`E – G – C`), the script writes `C/E` instead of a generic root-position chord.

### Limitations

- Only voicings in the internal dictionary are recognized; chords with unmapped alterations (e.g. `C7#9`, complex clusters) are shown as `?`.
- Analyzes one item at a time: select the MIDI item before running the script.
- Muted notes in the MIDI item are excluded from analysis.

### Repository structure

```
Hommelsound/
├── index.xml                      # ReaPack manifest
├── midi/
│   └── Generate_Chord_Track.lua   # Main script
└── README.md
```

### License

Open source — see the script for author details.
