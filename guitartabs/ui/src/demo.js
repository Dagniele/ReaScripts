import { nextMask } from "./techniques.js";

const PRESETS = {
  4: [
    ["Standard (DGBE)", [50, 55, 59, 64]],
    ["Bass Standard", [28, 33, 38, 43]],
    ["Bass Drop D", [26, 33, 38, 43]],
  ],
  5: [
    ["Guitar (ADGBE)", [45, 50, 55, 59, 64]],
    ["Bass Standard", [23, 28, 33, 38, 43]],
    ["Bass Tenor", [28, 33, 38, 43, 48]],
  ],
  6: [
    ["Standard E", [40, 45, 50, 55, 59, 64]],
    ["Drop D", [38, 45, 50, 55, 59, 64]],
    ["Drop C", [36, 43, 48, 53, 57, 62]],
    ["Drop B", [35, 42, 47, 52, 56, 61]],
    ["Drop A", [33, 40, 45, 50, 54, 59]],
    ["D Standard", [38, 43, 48, 53, 57, 62]],
    ["C Standard", [36, 41, 46, 51, 55, 60]],
    ["Half Step Down", [39, 44, 49, 54, 58, 63]],
    ["Open G", [38, 43, 50, 55, 59, 62]],
    ["Open D", [38, 45, 50, 54, 57, 62]],
    ["DADGAD", [38, 45, 50, 55, 57, 62]],
  ],
  7: [
    ["Standard", [35, 40, 45, 50, 55, 59, 64]],
    ["Drop A", [33, 40, 45, 50, 55, 59, 64]],
    ["Drop G", [31, 38, 43, 48, 53, 57, 62]],
  ],
  8: [
    ["Standard", [30, 35, 40, 45, 50, 55, 59, 64]],
    ["Drop E", [28, 35, 40, 45, 50, 55, 59, 64]],
  ],
};

function list(count) {
  return (PRESETS[count] || PRESETS[6]).map(([name, notes]) => ({ name, notes: [...notes] }));
}

function match(count, tuning) {
  const found = list(count).find((preset) => preset.notes.every((note, index) => note === tuning[index]));
  return found ? found.name : "Custom";
}

function clone(state) {
  return JSON.parse(JSON.stringify(state));
}

let handlers = null;
let state = null;
let undo = [];
let redo = [];
let playing = true;
let playQn = 0;

function push() {
  handlers.onState(clone(state));
}

function remember() {
  undo.push(clone(state));
  if (undo.length > 60) undo.shift();
  redo = [];
}

function measures() {
  const out = [];
  for (let i = 0; i < 16; i += 1) {
    out.push({ n: i + 1, t0: i * 2, t1: (i + 1) * 2, qn0: i * 4, qn1: (i + 1) * 4, num: 4, den: 4, bpm: 96 });
  }
  return out;
}

function seed() {
  const tuning = [40, 45, 50, 55, 59, 64];
  const shapes = [
    [-1, 0, 2, 2, 2, 0],
    [-1, -1, 0, 2, 3, 2],
    [-1, 3, 2, 0, 1, 0],
    [-1, -1, 0, 2, 3, 1],
  ];
  const events = shapes.map((frets, index) => ({
    qn: index * 2,
    d: 2,
    f: frets,
    k: frets.map(() => 0),
    bn: frets.map(() => 0),
    a: 0b111111,
  }));
  return {
    v: 1,
    strings: 6,
    preset: "Standard E",
    tuning,
    division: 8,
    events,
    trackName: "Rhythm guitar",
    trackGuid: "demo",
    presetList: list(6),
    alive: true,
    marquee: [{ button: 2, modifiers: 0, mode: "replace" }],
  };
}

function setCount(count) {
  const previous = state.tuning;
  const old = state.strings;
  const fallback = list(count)[0].notes;
  const next = Array(count).fill(0);
  if (count > old) {
    const add = count - old;
    for (let i = 0; i < add; i += 1) next[i] = fallback[i];
    for (let i = 0; i < old; i += 1) next[add + i] = previous[i];
  } else {
    const drop = old - count;
    for (let i = 0; i < count; i += 1) next[i] = previous[drop + i];
  }
  state.strings = count;
  state.tuning = next;
  state.preset = match(count, next);
  state.presetList = list(count);
  state.events = state.events
    .map((event) => {
      const frets = Array(count).fill(-1);
      const marks = Array(count).fill(0);
      const bends = Array(count).fill(0);
      const place = (dest, src) => {
        frets[dest] = event.f[src];
        marks[dest] = event.k?.[src] || 0;
        bends[dest] = event.bn?.[src] || 0;
      };
      if (count > old) {
        const add = count - old;
        for (let i = 0; i < old; i += 1) place(add + i, i);
      } else {
        const drop = old - count;
        for (let i = 0; i < count; i += 1) place(i, drop + i);
      }
      return { ...event, f: frets, k: marks, bn: bends, a: 0 };
    })
    .filter((event) => event.f.some((fret) => fret >= 0));
}

export function startDemo(nextHandlers) {
  handlers = nextHandlers;
  if (state) {
    push();
    return;
  }
  state = seed();
  push();
  handlers.onMap({ measures: measures(), endQn: 64 });
  handlers.onStatus({ phase: "idle", progress: 0, detail: "Preview" });
  let lastStamp = 0;
  const tick = (now) => {
    if (lastStamp && playing) playQn = (playQn + ((now - lastStamp) / 1000) * (96 / 60)) % 64;
    lastStamp = now;
    handlers.onTransport({
      playTime: playQn * 0.5,
      playQn,
      playing,
      paused: false,
      bpm: 96,
      hasSel: false,
      selTime0: 0,
      selTime1: 0,
      selQn0: 0,
      selQn1: 0,
    });
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

function applyCopied(notes) {
  const step = 4 / state.division;
  let touched = false;
  for (const note of notes) {
    if (note.string < 0 || note.string >= state.strings || note.fret < 0 || note.fret > 24) continue;
    const snapped = Math.max(0, Math.floor(note.qn / step + 1e-6) * step);
    let event = state.events.find((item) => Math.abs(item.qn - snapped) < step * 0.25);
    if (!event) {
      event = {
        qn: snapped,
        d: step,
        f: Array(state.strings).fill(-1),
        k: Array(state.strings).fill(0),
        bn: Array(state.strings).fill(0),
        a: 0,
      };
      state.events.push(event);
    }
    if (!event.k) event.k = Array(state.strings).fill(0);
    if (!event.bn) event.bn = Array(state.strings).fill(0);
    const attack = note.a !== false;
    const same =
      event.f[note.string] === note.fret &&
      (event.k[note.string] || 0) === (note.k || 0) &&
      (event.bn[note.string] || 0) === (note.bn || 0) &&
      ((event.a & (1 << note.string)) !== 0) === attack;
    if (same) continue;
    event.f[note.string] = note.fret;
    event.k[note.string] = note.k || 0;
    event.bn[note.string] = note.bn || 0;
    if (attack) event.a |= 1 << note.string;
    else event.a &= ~(1 << note.string);
    touched = true;
  }
  state.events.sort((a, b) => a.qn - b.qn);
  return touched;
}

export function demoDispatch(message) {
  if (!state) return;
  if (message.type === "play") {
    playing = !playing;
    return;
  }
  if (message.type === "undo" && undo.length) {
    redo.push(clone(state));
    state = undo.pop();
    push();
    return;
  }
  if (message.type === "redo" && redo.length) {
    undo.push(clone(state));
    state = redo.pop();
    push();
    return;
  }
  if (message.type === "detect") {
    remember();
    const step = 4 / state.division;
    const riff = [0, 2, 3, 5, 3, 2, 0, -1];
    state.events = [];
    for (let qn = 0; qn < 32; qn += step) {
      const fret = riff[Math.floor(qn / step) % riff.length];
      if (fret < 0) continue;
      const frets = Array(state.strings).fill(-1);
      frets[0] = fret;
      state.events.push({ qn, d: step, f: frets, k: Array(state.strings).fill(0), bn: Array(state.strings).fill(0), a: 1 });
    }
    push();
    handlers.onStatus({ phase: "idle", progress: 1, detail: "Preview transcription" });
    return;
  }

  remember();
  if (message.type === "setStringCount") setCount(message.count);
  else if (message.type === "setPreset") {
    const preset = state.presetList.find((item) => item.name === message.name);
    if (preset) {
      state.tuning = [...preset.notes];
      state.preset = preset.name;
    }
  } else if (message.type === "setTuning") {
    state.tuning = message.notes.map((note) => Math.max(16, Math.min(96, note)));
    state.preset = match(state.strings, state.tuning);
  } else if (message.type === "setDivision") state.division = message.division;
  else if (message.type === "setCell" || message.type === "clearCell") {
    const fret = message.type === "clearCell" ? -1 : message.fret;
    const step = 4 / state.division;
    const qn = Math.floor(message.qn / step + 1e-6) * step;
    let event = state.events.find((item) => Math.abs(item.qn - qn) < step * 0.25);
    if (!event && fret >= 0) {
      event = { qn, d: step, f: Array(state.strings).fill(-1), k: Array(state.strings).fill(0), bn: Array(state.strings).fill(0), a: 0 };
      state.events.push(event);
    }
    if (event) {
      if (!event.k) event.k = Array(state.strings).fill(0);
      if (!event.bn) event.bn = Array(state.strings).fill(0);
      event.f[message.string] = fret;
      if (fret >= 0) event.a |= 1 << message.string;
      else {
        event.a &= ~(1 << message.string);
        event.k[message.string] = 0;
        event.bn[message.string] = 0;
      }
      if (event.f.every((value) => value < 0)) state.events = state.events.filter((item) => item !== event);
      state.events.sort((a, b) => a.qn - b.qn);
    }
  } else if (message.type === "clearCells") {
    const step = 4 / state.division;
    for (const cell of message.cells || []) {
      const qn = Math.floor(cell.qn / step + 1e-6) * step;
      const event = state.events.find((item) => Math.abs(item.qn - qn) < step * 0.25);
      if (!event) continue;
      event.f[cell.string] = -1;
      event.a &= ~(1 << cell.string);
      if (event.k) event.k[cell.string] = 0;
      if (event.bn) event.bn[cell.string] = 0;
      if (event.f.every((value) => value < 0)) state.events = state.events.filter((item) => item !== event);
    }
  } else if (message.type === "setMark") {
    const step = 4 / state.division;
    let touched = false;
    for (const cell of message.cells || []) {
      const qn = Math.floor(cell.qn / step + 1e-6) * step;
      const event = state.events.find((item) => Math.abs(item.qn - qn) < step * 0.25);
      if (!event || event.f[cell.string] < 0) continue;
      if (!event.k) event.k = Array(state.strings).fill(0);
      if (!event.bn) event.bn = Array(state.strings).fill(0);
      if (message.bend != null) {
        if (event.bn[cell.string] === message.bend) continue;
        event.bn[cell.string] = message.bend;
      } else {
        const next = nextMask(event.k[cell.string] || 0, message.bit, message.on);
        if (next === (event.k[cell.string] || 0)) continue;
        event.k[cell.string] = next;
      }
      touched = true;
    }
    if (!touched) {
      undo.pop();
      return;
    }
  } else if (message.type === "duplicate") {
    const step = 4 / state.division;
    const found = [];
    for (const cell of message.cells || []) {
      const qn = Math.floor(cell.qn / step + 1e-6) * step;
      const event = state.events.find((item) => Math.abs(item.qn - qn) < step * 0.25);
      if (!event || event.f[cell.string] == null || event.f[cell.string] < 0) continue;
      found.push({
        qn: event.qn,
        string: cell.string,
        fret: event.f[cell.string],
        k: event.k?.[cell.string] || 0,
        bn: event.bn?.[cell.string] || 0,
        a: ((event.a || 0) & (1 << cell.string)) !== 0,
      });
    }
    if (!found.length) {
      undo.pop();
      return;
    }
    const min = Math.min(...found.map((note) => note.qn));
    const max = Math.max(...found.map((note) => note.qn));
    const offset = max - min + step;
    if (!applyCopied(found.map((note) => ({ ...note, qn: note.qn + offset })))) {
      undo.pop();
      return;
    }
  } else if (message.type === "pasteNotes") {
    if (!applyCopied(message.notes || [])) {
      undo.pop();
      return;
    }
  } else if (message.type === "clearRange") state.events = [];
  else {
    undo.pop();
    return;
  }
  push();
}
