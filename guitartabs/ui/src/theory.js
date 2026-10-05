const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

export function midiName(midi) {
  const note = ((midi % 12) + 12) % 12;
  return `${NAMES[note]}${Math.floor(midi / 12) - 1}`;
}

export function barBeat(qn, measures) {
  for (const measure of measures || []) {
    if (qn >= measure.qn0 - 1e-4 && qn < measure.qn1 - 1e-4) {
      const beatQn = 4 / Math.max(1, measure.den);
      const beat = Math.floor((qn - measure.qn0) / beatQn) + 1;
      return { bar: measure.n, beat };
    }
  }
  return { bar: 1, beat: 1 };
}
