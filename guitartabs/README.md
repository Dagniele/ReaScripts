# Guitar Tabs

A REAPER extension that opens one tab editor per track. It is a native extension (`reaper_guitartabs.dylib`), not a ReaScript and not an audio plugin. The staff is a React page hosted in a macOS web view. Notes are stored in quarter-note time, so they stay on the same bar and beat as the project tempo map, and the playhead is the REAPER play position.

## What you can do

- Open a tab window for every selected track from the action **Dagniele: Open Guitar Tabs for selected track**.
- Choose 4, 5, 6, 7, or 8 strings. Six-string standard E is the default.
- Pick a tuning (Standard E, Drop D, Drop C, Drop A, and the other built-in sets) or tune each string yourself.
- Click **Detect audio** to transcribe the track. A time selection limits the analysis to that range; otherwise the whole project is used. If that range already has tabs, choose **Stop** or **Replace**.
- Click any cell and type a fret from 0 to 24. Changes are saved in the project and can be undone with ⌘Z.
- The staff follows the project tempo map. **Follow** keeps the REAPER playhead in view.

Pitch detection is a prototype. It reads the track pre-FX, estimates pitches on the note grid, and fits them onto the current tuning. Clean single notes and simple dyads work better than dense chords, distortion, or several guitars on one track.

## Build

The UI is bundled into one HTML file, then the extension is compiled.

```bash
cd guitartabs/ui
npm install
npm run build
cd ..
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`guitartabs_tests` checks tuning, fret placement, and the pitch estimator:

```bash
cmake --build build --target guitartabs_tests
./build/guitartabs_tests
```

## Install

Copy the extension and the interface folder into REAPER's UserPlugins directory, then restart REAPER:

```bash
cp build/reaper_guitartabs.dylib "$HOME/Library/Application Support/REAPER/UserPlugins/"
cp -R build/reaper_guitartabs_ui "$HOME/Library/Application Support/REAPER/UserPlugins/"
```

Run **Dagniele: Open Guitar Tabs for selected track** from the Actions list. Select several tracks to open one window each. Tab data is stored with the project and restored the next time you open that track.

This build targets Apple Silicon macOS. REAPER 6 or newer is required.
