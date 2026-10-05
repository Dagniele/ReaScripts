import { useEffect, useMemo, useRef, useState } from "react";
import { installBridge, post } from "./bridge.js";
import TabView from "./TabView.jsx";
import { barBeat, midiName } from "./theory.js";
import "./styles.css";

const PITCHES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

function currentFret(doc, qn, stringIndex) {
  const step = 4 / (doc?.division || 8);
  const event = doc?.events?.find((item) => Math.abs(item.qn - qn) < step * 0.25);
  const fret = event?.f?.[stringIndex];
  return fret >= 0 ? String(fret) : "";
}

export default function App() {
  const [doc, setDoc] = useState(null);
  const [map, setMap] = useState({ measures: [], endQn: 16 });
  const [transport, setTransport] = useState(null);
  const [status, setStatus] = useState({ phase: "idle", progress: 0, detail: "" });
  const [confirm, setConfirm] = useState(null);
  const [editor, setEditor] = useState(null);
  const [draft, setDraft] = useState("");
  const [tuningOpen, setTuningOpen] = useState(false);
  const [follow, setFollow] = useState(true);
  const [pxPerQn, setPxPerQn] = useState(72);
  const transportRef = useRef(null);
  const mapRef = useRef(map);
  const footerKey = useRef("");

  useEffect(() => {
    installBridge({
      onState: setDoc,
      onMap: (next) => {
        mapRef.current = next;
        setMap(next);
      },
      onTransport: (next) => {
        transportRef.current = next;
        const place = barBeat(next.playQn, mapRef.current.measures);
        const key = `${place.bar}.${place.beat}|${Math.round(next.bpm)}|${next.hasSel}|${next.playing}`;
        if (key !== footerKey.current) {
          footerKey.current = key;
          setTransport(next);
        }
      },
      onStatus: setStatus,
      onConfirm: setConfirm,
    });
  }, []);

  useEffect(() => {
    const onKey = (event) => {
      const meta = event.metaKey || event.ctrlKey;
      if (!meta || event.key.toLowerCase() !== "z") return;
      event.preventDefault();
      post({ type: event.shiftKey ? "redo" : "undo" });
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  const place = useMemo(() => barBeat(transport?.playQn || 0, map.measures), [transport, map.measures]);
  const busy = status.phase === "reading" || status.phase === "detecting";

  function detect() {
    const current = transportRef.current;
    const q0 = current?.hasSel ? current.selQn0 : 0;
    const q1 = current?.hasSel ? current.selQn1 : map.endQn || 0;
    const hits = (doc?.events || []).filter((event) => event.qn >= q0 - 1e-4 && event.qn < q1 - 1e-6);
    if (hits.length) setConfirm({ count: hits.length, qn0: q0, qn1: q1 });
    else post({ type: "detect", overwrite: true });
  }

  function commit(fret) {
    if (!editor) return;
    if (fret < 0) post({ type: "clearCell", qn: editor.qn, string: editor.string });
    else post({ type: "setCell", qn: editor.qn, string: editor.string, fret });
    setEditor(null);
  }

  function typeDigit(digit) {
    const next = `${draft}${digit}`.replace(/\D/g, "").slice(0, 2);
    const value = Number(next);
    if (Number.isNaN(value)) return;
    if (value > 24) {
      setDraft(String(digit));
      return;
    }
    setDraft(next);
  }

  const highToLow = doc ? [...doc.tuning].map((note, index) => ({ note, index })).reverse() : [];

  return (
    <div className="app">
      <header className="top">
        <div className="brand">
          <h1>Guitar Tabs</h1>
          <span>{doc?.trackName || "Waiting for a track"}</span>
        </div>
        <div className="controls">
          <div className="segment" aria-label="String count">
            {[4, 5, 6, 7, 8].map((count) => (
              <button key={count} className={doc?.strings === count ? "active" : ""} onClick={() => post({ type: "setStringCount", count })}>
                {count}
              </button>
            ))}
          </div>
          <select className="select" aria-label="Tuning" value={doc?.preset || "Custom"} onChange={(event) => post({ type: "setPreset", name: event.target.value })}>
            {(doc?.presetList || []).map((preset) => (
              <option key={preset.name}>{preset.name}</option>
            ))}
            {doc?.preset === "Custom" && <option>Custom</option>}
          </select>
          <button className={tuningOpen ? "tune active" : "tune"} onClick={() => setTuningOpen((open) => !open)}>
            Strings
          </button>
          <div className="segment" aria-label="Grid">
            {[4, 8, 16].map((division) => (
              <button key={division} className={doc?.division === division ? "active" : ""} onClick={() => post({ type: "setDivision", division })}>
                1/{division}
              </button>
            ))}
          </div>
          <button className={follow ? "ghost active" : "ghost"} onClick={() => setFollow((value) => !value)}>
            Follow
          </button>
          <label className="zoom">
            Zoom
            <input type="range" min="42" max="130" value={pxPerQn} onChange={(event) => setPxPerQn(Number(event.target.value))} />
          </label>
          <button className="ghost" onClick={() => post({ type: "clearRange" })}>
            Clear
          </button>
          <button className={busy ? "detect busy" : "detect"} disabled={!doc || busy || doc.alive === false} onClick={detect}>
            {busy ? "Detecting" : "Detect audio"}
          </button>
        </div>
      </header>
      {tuningOpen && doc && (
        <div className="tuning">
          {highToLow.map((string) => (
            <label className="string-tune" key={string.index}>
              <b>{doc.strings - string.index}</b>
              <select
                aria-label={`String ${doc.strings - string.index} note`}
                value={PITCHES[((string.note % 12) + 12) % 12]}
                onChange={(event) => {
                  const pitch = PITCHES.indexOf(event.target.value);
                  const octave = Math.floor(string.note / 12) - 1;
                  const notes = [...doc.tuning];
                  notes[string.index] = (octave + 1) * 12 + pitch;
                  post({ type: "setTuning", notes });
                }}
              >
                {PITCHES.map((name) => (
                  <option key={name}>{name}</option>
                ))}
              </select>
              <select
                aria-label={`String ${doc.strings - string.index} octave`}
                value={Math.floor(string.note / 12) - 1}
                onChange={(event) => {
                  const pitch = ((string.note % 12) + 12) % 12;
                  const notes = [...doc.tuning];
                  notes[string.index] = (Number(event.target.value) + 1) * 12 + pitch;
                  post({ type: "setTuning", notes });
                }}
              >
                {Array.from({ length: 8 }, (_, octave) => (
                  <option key={octave}>{octave}</option>
                ))}
              </select>
              <span>{midiName(string.note)}</span>
            </label>
          ))}
        </div>
      )}
      <div className="stage">
        {busy && (
          <div className="progress">
            <span style={{ width: `${Math.round((status.progress || 0) * 100)}%` }} />
          </div>
        )}
        <TabView
          doc={doc}
          map={map}
          transportRef={transportRef}
          follow={follow}
          pxPerQn={pxPerQn}
          onManualScroll={() => setFollow(false)}
          onCell={(hit, x, y) => {
            setEditor({ ...hit, x: Math.min(x, window.innerWidth - 220), y: Math.min(y, window.innerHeight - 180) });
            setDraft(currentFret(doc, hit.qn, hit.string));
          }}
        />
      </div>
      <footer className="foot">
        <div>
          <strong>
            {place.bar}.{place.beat}
          </strong>
          {"  "}
          {transport ? `${Math.round(transport.bpm)} bpm` : ""}
          {transport?.hasSel ? "  ·  time selection" : "  ·  whole song"}
          {"  ·  "}
          {doc ? `${doc.strings} strings · ${doc.preset}` : ""}
        </div>
        <div className={status.phase === "error" || status.phase === "empty" ? "status-pill warn" : "status-pill"}>
          {status.detail || "Click a cell and type a fret. ⌘Z undoes."}
        </div>
      </footer>
      {confirm && (
        <div className="modal">
          <div className="card">
            <h2>Replace these tabs?</h2>
            <p>
              {confirm.count} existing {confirm.count === 1 ? "cell is" : "cells are"} already in this range. Detecting again will overwrite them.
            </p>
            <div className="actions">
              <button className="stop" onClick={() => setConfirm(null)}>
                Stop
              </button>
              <button
                className="replace"
                onClick={() => {
                  setConfirm(null);
                  post({ type: "detect", overwrite: true });
                }}
              >
                Replace
              </button>
            </div>
          </div>
        </div>
      )}
      {editor && (
        <form
          className="popover"
          style={{ left: editor.x + 12, top: editor.y + 12 }}
          onSubmit={(event) => {
            event.preventDefault();
            if (draft === "") commit(-1);
            else commit(Math.max(0, Math.min(24, Number(draft))));
          }}
        >
          <input
            autoFocus
            inputMode="numeric"
            aria-label="Fret"
            value={draft}
            onChange={(event) => setDraft(event.target.value.replace(/\D/g, "").slice(0, 2))}
            onKeyDown={(event) => {
              if (event.key === "Escape") setEditor(null);
              if (event.key === "Backspace" && draft === "") {
                event.preventDefault();
                commit(-1);
              }
            }}
          />
          <div className="digits">
            {Array.from({ length: 10 }, (_, digit) => (
              <button key={digit} type="button" onClick={() => typeDigit(digit)}>
                {digit}
              </button>
            ))}
          </div>
          <div className="row">
            <button type="button" onClick={() => commit(-1)}>
              Clear
            </button>
            <button type="submit">Set</button>
          </div>
        </form>
      )}
    </div>
  );
}
