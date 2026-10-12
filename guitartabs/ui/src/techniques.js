export const MARK = {
  hammer: 1 << 0,
  pull: 1 << 1,
  slideUp: 1 << 2,
  slideDown: 1 << 3,
  fromBelow: 1 << 4,
  fromAbove: 1 << 5,
  vibrato: 1 << 6,
  palm: 1 << 7,
  harmonic: 1 << 8,
  pinch: 1 << 9,
  dead: 1 << 10,
  tap: 1 << 11,
  ghost: 1 << 12,
  letRing: 1 << 13,
  staccato: 1 << 14,
  accent: 1 << 15,
  tremolo: 1 << 16,
  trill: 1 << 17,
  slap: 1 << 18,
  pop: 1 << 19,
  preBend: 1 << 20,
  release: 1 << 21,
  rake: 1 << 22,
};

const RIVALS = {
  [MARK.hammer]: MARK.pull,
  [MARK.pull]: MARK.hammer,
  [MARK.slideUp]: MARK.slideDown,
  [MARK.slideDown]: MARK.slideUp,
  [MARK.fromBelow]: MARK.fromAbove,
  [MARK.fromAbove]: MARK.fromBelow,
  [MARK.harmonic]: MARK.pinch,
  [MARK.pinch]: MARK.harmonic,
  [MARK.slap]: MARK.pop,
  [MARK.pop]: MARK.slap,
};

export const BEND_NAMES = { 1: "¼", 2: "½", 4: "full", 6: "1½", 8: "2" };

export const GROUPS = [
  {
    name: "Legato",
    items: [
      { bit: MARK.hammer, label: "h", title: "Hammer-on" },
      { bit: MARK.pull, label: "p", title: "Pull-off" },
      { bit: MARK.slideUp, label: "/", title: "Slide up" },
      { bit: MARK.slideDown, label: "\\", title: "Slide down" },
      { bit: MARK.fromBelow, label: "↗", title: "Slide in from below" },
      { bit: MARK.fromAbove, label: "↘", title: "Slide in from above" },
    ],
  },
  {
    name: "Bend",
    items: [
      { bend: 1, label: "¼", title: "Quarter-step bend" },
      { bend: 2, label: "½", title: "Half-step bend" },
      { bend: 4, label: "full", title: "Whole-step bend" },
      { bend: 6, label: "1½", title: "One-and-a-half bend" },
      { bend: 8, label: "2", title: "Two-step bend" },
      { bit: MARK.preBend, label: "pre", title: "Pre-bend" },
      { bit: MARK.release, label: "rel", title: "Release" },
      { bit: MARK.vibrato, label: "~", title: "Vibrato" },
    ],
  },
  {
    name: "Attack",
    items: [
      { bit: MARK.palm, label: "PM", title: "Palm mute" },
      { bit: MARK.dead, label: "x", title: "Dead note" },
      { bit: MARK.ghost, label: "( )", title: "Ghost note" },
      { bit: MARK.harmonic, label: "◇", title: "Natural harmonic" },
      { bit: MARK.pinch, label: "PH", title: "Pinch harmonic" },
      { bit: MARK.tap, label: "T", title: "Tap" },
      { bit: MARK.slap, label: "slap", title: "Slap" },
      { bit: MARK.pop, label: "pop", title: "Pop" },
      { bit: MARK.rake, label: "rake", title: "Rake" },
    ],
  },
  {
    name: "Phrase",
    items: [
      { bit: MARK.letRing, label: "let ring", title: "Let ring" },
      { bit: MARK.staccato, label: "·", title: "Staccato" },
      { bit: MARK.accent, label: ">", title: "Accent" },
      { bit: MARK.tremolo, label: "///", title: "Tremolo picking" },
      { bit: MARK.trill, label: "tr", title: "Trill" },
    ],
  },
];

export function nextMask(mask, bit, on) {
  let next = on ? mask | bit : mask & ~bit;
  if (on && RIVALS[bit]) next &= ~RIVALS[bit];
  return next >>> 0;
}

export function fretLabel(fret, mask) {
  if (mask & MARK.dead) return "x";
  let text = String(fret);
  if (mask & (MARK.harmonic | MARK.pinch)) text = `<${text}>`;
  if (mask & MARK.ghost) text = `(${text})`;
  if (mask & MARK.tap) text = `t${text}`;
  return text;
}

export function annotation(mask) {
  const parts = [];
  if (mask & MARK.palm) parts.push("PM");
  if (mask & MARK.vibrato) parts.push("~");
  if (mask & MARK.pinch) parts.push("PH");
  if (mask & MARK.letRing) parts.push("let ring");
  if (mask & MARK.staccato) parts.push("·");
  if (mask & MARK.accent) parts.push(">");
  if (mask & MARK.tremolo) parts.push("///");
  if (mask & MARK.trill) parts.push("tr");
  if (mask & MARK.slap) parts.push("slap");
  if (mask & MARK.pop) parts.push("pop");
  if (mask & MARK.rake) parts.push("rake");
  return parts.join(" ");
}
