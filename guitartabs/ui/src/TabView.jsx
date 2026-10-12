import { useEffect, useRef } from "react";
import { midiName } from "./theory.js";
import { BEND_NAMES, MARK, annotation, fretLabel } from "./techniques.js";

const GUTTER = 78;
const RULER = 36;

function modifierMask(event) {
  let mask = 0;
  if (event.shiftKey) mask |= 1;
  if (event.metaKey) mask |= 2;
  if (event.altKey) mask |= 4;
  if (event.ctrlKey) mask |= 8;
  return mask;
}

function marqueeGesture(event, gestures) {
  const list = gestures?.length ? gestures : [{ button: 2, modifiers: 0, mode: "replace" }];
  const mask = modifierMask(event);
  return list.find((gesture) => gesture.button === event.button && gesture.modifiers === mask) || null;
}

export default function TabView({ doc, map, transportRef, follow, pxPerQn, selected, marquee, onCell, onSelect, onManualScroll }) {
  const canvasRef = useRef(null);
  const scrollRef = useRef(0);
  const hoverRef = useRef(null);
  const dragRef = useRef(null);
  const bandRef = useRef(null);
  const layoutRef = useRef({ top: 80, gap: 34 });

  useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas.getContext("2d");
    let frame = 0;

    const draw = () => {
      frame = requestAnimationFrame(draw);
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const width = Math.max(1, rect.width);
      const height = Math.max(1, rect.height);
      if (canvas.width !== Math.floor(width * dpr) || canvas.height !== Math.floor(height * dpr)) {
        canvas.width = Math.floor(width * dpr);
        canvas.height = Math.floor(height * dpr);
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      const strings = doc?.strings || 6;
      const tuning = doc?.tuning || [];
      const gap = strings > 6 ? 30 : 36;
      const staff = (strings - 1) * gap;
      const top = Math.max(RULER + 22, (height - staff) / 2);
      layoutRef.current = { top, gap, strings };
      const endQn = Math.max(map?.endQn || 16, ...(doc?.events || []).map((event) => event.qn + event.d), 4);
      const content = GUTTER + endQn * pxPerQn + 120;
      const transport = transportRef.current || {};
      const maxScroll = Math.max(0, content - width);
      if (follow && transport.playQn != null && !dragRef.current) {
        const target = transport.playQn * pxPerQn - (width - GUTTER) * 0.28;
        scrollRef.current += (Math.max(0, Math.min(maxScroll, target)) - scrollRef.current) * 0.18;
      } else scrollRef.current = Math.max(0, Math.min(maxScroll, scrollRef.current));
      const scroll = scrollRef.current;

      ctx.clearRect(0, 0, width, height);
      ctx.fillStyle = "#0c0e12";
      ctx.fillRect(0, 0, width, height);

      ctx.save();
      ctx.beginPath();
      ctx.rect(GUTTER, 0, width - GUTTER, height);
      ctx.clip();
      ctx.translate(GUTTER - scroll, 0);

      if (transport.hasSel) {
        const x0 = transport.selQn0 * pxPerQn;
        const x1 = transport.selQn1 * pxPerQn;
        ctx.fillStyle = "rgba(227, 106, 58, 0.13)";
        ctx.fillRect(x0, RULER, Math.max(2, x1 - x0), height - RULER - 12);
      }

      const division = doc?.division || 8;
      for (const measure of map?.measures || []) {
        const x = measure.qn0 * pxPerQn;
        ctx.strokeStyle = "rgba(243,239,230,0.28)";
        ctx.lineWidth = 1.4;
        ctx.beginPath();
        ctx.moveTo(x, RULER - 8);
        ctx.lineTo(x, top + staff + 16);
        ctx.stroke();
        ctx.fillStyle = "#8f958c";
        ctx.font = "12px Avenir Next, Segoe UI, sans-serif";
        ctx.textAlign = "left";
        ctx.fillText(String(measure.n), x + 6, 18);
        const beat = 4 / measure.den;
        for (let q = measure.qn0 + beat; q < measure.qn1 - 0.01; q += beat) {
          ctx.strokeStyle = "rgba(243,239,230,0.1)";
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.moveTo(q * pxPerQn, top - 10);
          ctx.lineTo(q * pxPerQn, top + staff + 10);
          ctx.stroke();
        }
        const sub = 4 / division;
        if (sub < beat - 0.01 && sub * pxPerQn > 18) {
          for (let q = measure.qn0; q < measure.qn1 - 0.01; q += sub) {
            if (Math.abs((q - measure.qn0) / beat - Math.round((q - measure.qn0) / beat)) < 0.05) continue;
            ctx.strokeStyle = "rgba(243,239,230,0.045)";
            ctx.beginPath();
            ctx.moveTo(q * pxPerQn, top - 4);
            ctx.lineTo(q * pxPerQn, top + staff + 4);
            ctx.stroke();
          }
        }
      }

      for (let string = 0; string < strings; string += 1) {
        const y = top + (strings - 1 - string) * gap;
        const weight = string / Math.max(1, strings - 1);
        ctx.strokeStyle = `rgba(243,239,230,${0.28 - weight * 0.1})`;
        ctx.lineWidth = 1 + weight * 1.1;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(endQn * pxPerQn + 80, y);
        ctx.stroke();
      }

      const previous = Array.from({ length: strings }, () => null);
      for (const event of doc?.events || []) {
        for (let string = 0; string < strings; string += 1) {
          const fret = event.f?.[string];
          const y = top + (strings - 1 - string) * gap;
          const x = event.qn * pxPerQn + (event.d * pxPerQn) / 2;
          if (fret == null || fret < 0) {
            previous[string] = null;
            continue;
          }
          const mask = event.k?.[string] || 0;
          const bend = event.bn?.[string] || 0;
          const attack = (event.a || 0) & (1 << string);
          const prior = previous[string];
          const sustained = prior && prior.fret === fret && Math.abs(prior.qn + prior.d - event.qn) < 1e-2 && !attack;
          const chosen = (selected || []).some((note) => note.string === string && Math.abs(note.qn - event.qn) < 1e-3);
          const ink = chosen ? "#e36a3a" : "#f6f1e7";
          if (prior && (mask & (MARK.slideUp | MARK.slideDown)) && prior.fret !== fret) {
            ctx.strokeStyle = "rgba(243,239,230,0.72)";
            ctx.lineWidth = 1.4;
            ctx.beginPath();
            ctx.moveTo(prior.x + 10, prior.y);
            ctx.lineTo(x - 12, y);
            ctx.stroke();
          }
          if (prior && (mask & (MARK.hammer | MARK.pull))) {
            ctx.fillStyle = "#c9d7cf";
            ctx.font = "600 11px SF Mono, ui-monospace, Menlo, monospace";
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.fillText(mask & MARK.hammer ? "h" : "p", (prior.x + x) / 2, y - 14);
          }
          if (mask & (MARK.fromBelow | MARK.fromAbove)) {
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1.4;
            ctx.beginPath();
            ctx.moveTo(x - 16, y + (mask & MARK.fromBelow ? 9 : -9));
            ctx.lineTo(x - 8, y);
            ctx.stroke();
          }
          if (sustained) {
            ctx.fillStyle = ink;
            ctx.fillRect(x - 7, y - 1.25, 14, chosen ? 3.5 : 2.5);
          } else {
            const label = fretLabel(fret, mask);
            ctx.font = "600 13px SF Mono, ui-monospace, Menlo, monospace";
            const width = Math.max(22, ctx.measureText(label).width + 10);
            ctx.fillStyle = "rgba(12,14,18,0.92)";
            ctx.beginPath();
            ctx.roundRect(x - width / 2, y - 11, width, 22, 11);
            ctx.fill();
            if (chosen) {
              ctx.strokeStyle = "#e36a3a";
              ctx.lineWidth = 2;
              ctx.beginPath();
              ctx.roundRect(x - width / 2 - 2, y - 13, width + 4, 26, 13);
              ctx.stroke();
            }
            ctx.fillStyle = ink;
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.fillText(label, x, y + 0.5);
          }
          if (bend) {
            const rise = 8 + bend * 1.2;
            ctx.strokeStyle = "#e36a3a";
            ctx.lineWidth = 1.3;
            ctx.beginPath();
            ctx.moveTo(x + 10, y - 2);
            ctx.quadraticCurveTo(x + 18, y - rise, x + 28, y - rise);
            if (mask & MARK.release) ctx.quadraticCurveTo(x + 36, y - rise, x + 42, y - 2);
            ctx.stroke();
            ctx.fillStyle = "#e36a3a";
            ctx.font = "10px SF Mono, ui-monospace, Menlo, monospace";
            ctx.textAlign = "left";
            ctx.textBaseline = "bottom";
            const bendText = `${mask & MARK.preBend ? "pre " : ""}${BEND_NAMES[bend] || ""}`;
            ctx.fillText(bendText, x + 16, y - rise - 1);
          }
          const palmContinues = prior && prior.mask & MARK.palm && mask & MARK.palm;
          if (palmContinues) {
            ctx.strokeStyle = "rgba(125,222,196,0.7)";
            ctx.setLineDash([2, 3]);
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(prior.x, y - 18);
            ctx.lineTo(x, y - 18);
            ctx.stroke();
            ctx.setLineDash([]);
          }
          const caption = annotation(mask & (palmContinues ? ~MARK.palm : ~0));
          if (caption) {
            ctx.fillStyle = "#c9d7cf";
            ctx.font = "10px Avenir Next, Segoe UI, sans-serif";
            ctx.textAlign = "center";
            ctx.textBaseline = "bottom";
            ctx.fillText(caption, x, y - 14);
          }
          previous[string] = { fret, qn: event.qn, d: event.d, x, y, mask };
        }
      }

      const hover = hoverRef.current;
      if (hover) {
        const step = 4 / division;
        const y = top + (strings - 1 - hover.string) * gap;
        ctx.strokeStyle = "rgba(227,106,58,0.85)";
        ctx.lineWidth = 1.2;
        ctx.strokeRect(hover.qn * pxPerQn + 3, y - gap / 2 + 3, step * pxPerQn - 6, gap - 6);
      }

      if (transport.playQn != null) {
        const x = transport.playQn * pxPerQn;
        ctx.strokeStyle = "#7ddec4";
        ctx.lineWidth = 1.6;
        ctx.beginPath();
        ctx.moveTo(x, 8);
        ctx.lineTo(x, height - 8);
        ctx.stroke();
        ctx.fillStyle = "#7ddec4";
        ctx.beginPath();
        ctx.moveTo(x - 6, 8);
        ctx.lineTo(x + 6, 8);
        ctx.lineTo(x, 18);
        ctx.fill();
      }

      if (!doc?.events?.length) {
        ctx.fillStyle = "#8f958c";
        ctx.font = "16px Iowan Old Style, Palatino, serif";
        ctx.textAlign = "left";
        ctx.fillText("Detect the track, or click a string to write a fret.", 24, top + staff / 2);
      }
      ctx.restore();

      ctx.fillStyle = "#0c0e12";
      ctx.fillRect(0, 0, GUTTER, height);
      ctx.strokeStyle = "rgba(243,239,230,0.08)";
      ctx.beginPath();
      ctx.moveTo(GUTTER - 0.5, 0);
      ctx.lineTo(GUTTER - 0.5, height);
      ctx.stroke();
      for (let string = 0; string < strings; string += 1) {
        const y = top + (strings - 1 - string) * gap;
        ctx.fillStyle = "#5e655e";
        ctx.font = "11px SF Mono, ui-monospace, Menlo, monospace";
        ctx.textAlign = "left";
        ctx.textBaseline = "middle";
        ctx.fillText(String(strings - string), 12, y);
        ctx.fillStyle = "#f3efe6";
        ctx.font = "13px Avenir Next, Segoe UI, sans-serif";
        ctx.textAlign = "right";
        ctx.fillText(midiName(tuning[string] ?? 40), GUTTER - 12, y);
      }

      const band = bandRef.current;
      if (band) {
        const x = Math.min(band.x0, band.x1);
        const y = Math.min(band.y0, band.y1);
        ctx.fillStyle = "rgba(227,106,58,0.14)";
        ctx.strokeStyle = "rgba(227,106,58,0.9)";
        ctx.lineWidth = 1;
        ctx.fillRect(x, y, Math.abs(band.x1 - band.x0), Math.abs(band.y1 - band.y0));
        ctx.strokeRect(x, y, Math.abs(band.x1 - band.x0), Math.abs(band.y1 - band.y0));
      }
    };

    frame = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(frame);
  }, [doc, map, follow, pxPerQn, transportRef, selected]);

  function locate(event) {
    const rect = canvasRef.current.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    const { top, gap, strings } = layoutRef.current;
    if (x < GUTTER || !strings) return null;
    const qn = (x + scrollRef.current - GUTTER) / pxPerQn;
    const fromTop = Math.round((y - top) / gap);
    if (fromTop < 0 || fromTop >= strings) return null;
    const step = 4 / (doc?.division || 8);
    const string = strings - 1 - fromTop;
    const cursorX = x + scrollRef.current - GUTTER;
    const reach = Math.min(22, Math.max(10, step * pxPerQn * 0.55));
    let nearest = null;
    let nearestDist = reach;
    for (const item of doc?.events || []) {
      const fret = item.f?.[string];
      if (fret == null || fret < 0) continue;
      const noteX = item.qn * pxPerQn + (item.d * pxPerQn) / 2;
      const dist = Math.abs(cursorX - noteX);
      if (dist <= nearestDist) {
        nearestDist = dist;
        nearest = item.qn;
      }
    }
    return {
      qn: nearest != null ? nearest : Math.floor(qn / step + 1e-6) * step,
      string,
    };
  }

  function notesInside(x0, y0, x1, y1) {
    const left = Math.min(x0, x1) - 8;
    const right = Math.max(x0, x1) + 8;
    const topEdge = Math.min(y0, y1) - 8;
    const bottom = Math.max(y0, y1) + 8;
    const { top, gap, strings } = layoutRef.current;
    const notes = [];
    for (const item of doc?.events || []) {
      for (let string = 0; string < strings; string += 1) {
        const fret = item.f?.[string];
        if (fret == null || fret < 0) continue;
        const x = GUTTER - scrollRef.current + item.qn * pxPerQn + (item.d * pxPerQn) / 2;
        const y = top + (strings - 1 - string) * gap;
        if (x >= left && x <= right && y >= topEdge && y <= bottom) notes.push({ qn: item.qn, string });
      }
    }
    return notes;
  }

  function canvasPoint(event) {
    const rect = canvasRef.current.getBoundingClientRect();
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  }

  return (
    <canvas
      ref={canvasRef}
      onWheel={(event) => {
        event.preventDefault();
        const delta = Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY;
        scrollRef.current += delta;
        onManualScroll();
      }}
      onContextMenu={(event) => event.preventDefault()}
      onPointerDown={(event) => {
        const gesture = marqueeGesture(event, marquee);
        if (!gesture && event.button !== 0) return;
        const point = canvasPoint(event);
        dragRef.current = {
          x: event.clientX,
          y: event.clientY,
          scroll: scrollRef.current,
          moved: false,
          gesture,
          originX: point.x,
          originY: point.y,
        };
        if (gesture) event.preventDefault();
        event.currentTarget.setPointerCapture(event.pointerId);
      }}
      onPointerMove={(event) => {
        const hit = locate(event);
        hoverRef.current = hit;
        const drag = dragRef.current;
        if (!drag) return;
        const dx = event.clientX - drag.x;
        const dy = event.clientY - drag.y;
        if (Math.abs(dx) > 4 || Math.abs(dy) > 4) drag.moved = true;
        if (drag.gesture) {
          if (drag.moved) {
            const point = canvasPoint(event);
            bandRef.current = { x0: drag.originX, y0: drag.originY, x1: point.x, y1: point.y };
          }
          return;
        }
        if (drag.moved) {
          scrollRef.current = drag.scroll - dx;
          onManualScroll();
        }
      }}
      onPointerUp={(event) => {
        const drag = dragRef.current;
        dragRef.current = null;
        bandRef.current = null;
        if (drag?.gesture) {
          if (drag.moved) {
            const point = canvasPoint(event);
            onSelect({ type: "marquee", mode: drag.gesture.mode, notes: notesInside(drag.originX, drag.originY, point.x, point.y) });
          }
          return;
        }
        if (drag?.moved) return;
        const hit = locate(event);
        if (hit) onCell(hit, event.clientX, event.clientY);
      }}
      onPointerLeave={() => {
        hoverRef.current = null;
      }}
    />
  );
}
