#include "transcribe.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <limits>
#include <numeric>

namespace guitartabs {
namespace {

constexpr float kPi = 3.14159265358979323846f;

void fft(std::vector<std::complex<float>>& data) {
  const int n = static_cast<int>(data.size());
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(data[static_cast<size_t>(i)], data[static_cast<size_t>(j)]);
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float angle = -2.f * kPi / static_cast<float>(len);
    const std::complex<float> wlen(std::cos(angle), std::sin(angle));
    for (int i = 0; i < n; i += len) {
      std::complex<float> w(1.f, 0.f);
      for (int j = 0; j < len / 2; ++j) {
        const auto u = data[static_cast<size_t>(i + j)];
        const auto v = data[static_cast<size_t>(i + j + len / 2)] * w;
        data[static_cast<size_t>(i + j)] = u + v;
        data[static_cast<size_t>(i + j + len / 2)] = u - v;
        w *= wlen;
      }
    }
  }
}

double midiFrequency(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

std::vector<float> spectrumSpan(const float* samples, int sampleCount, int start, int end, int fftSize) {
  start = std::clamp(start, 0, sampleCount);
  end = std::clamp(end, start, sampleCount);
  int readStart = start;
  int readCount = end - start;
  if (readCount > fftSize) {
    readStart += (readCount - fftSize) / 2;
    readCount = fftSize;
  }
  std::vector<std::complex<float>> data(static_cast<size_t>(fftSize));
  const int denom = std::max(1, readCount - 1);
  for (int i = 0; i < readCount; ++i) {
    const float window = 0.5f - 0.5f * std::cos(2.f * kPi * static_cast<float>(i) / static_cast<float>(denom));
    data[static_cast<size_t>(i)] = samples[readStart + i] * window;
  }
  fft(data);
  std::vector<float> mag(static_cast<size_t>(fftSize / 2));
  const float scale = 2.f / static_cast<float>(std::max(1, readCount));
  for (int i = 0; i < fftSize / 2; ++i) mag[static_cast<size_t>(i)] = std::abs(data[static_cast<size_t>(i)]) * scale;
  return mag;
}

std::vector<float> spectrum(const float* samples, int sampleCount, int center, int fftSize) {
  const int start = center - fftSize / 2;
  return spectrumSpan(samples, sampleCount, start, start + fftSize, fftSize);
}

bool nearHarmonic(int upper, int lower) {
  if (upper <= lower) return false;
  const double ratio = midiFrequency(upper) / midiFrequency(lower);
  const int partial = static_cast<int>(std::lround(ratio));
  if (partial < 2 || partial > 8) return false;
  return std::abs(ratio - partial) / static_cast<double>(partial) < 0.035;
}

std::vector<int> pickNotes(const std::vector<float>& mag, int fftSize, int sampleRate, int low, int high, int maxNotes) {
  if (high < low || mag.empty()) return {};
  auto at = [&](double frequency) -> float {
    const double bin = frequency * fftSize / static_cast<double>(sampleRate);
    if (bin < 1 || bin >= static_cast<int>(mag.size()) - 1) return 0;
    const int index = static_cast<int>(bin);
    const float frac = static_cast<float>(bin - index);
    return mag[static_cast<size_t>(index)] * (1.f - frac) + mag[static_cast<size_t>(index + 1)] * frac;
  };

  const int width = high - low + 1;
  std::vector<float> score(static_cast<size_t>(width), 0.f);
  for (int midi = low; midi <= high; ++midi) {
    const double fundamental = midiFrequency(midi);
    float value = at(fundamental);
    value += 0.62f * at(fundamental * 2.0);
    value += 0.38f * at(fundamental * 3.0);
    value += 0.22f * at(fundamental * 4.0);
    value += 0.12f * at(fundamental * 5.0);
    if (at(fundamental) < value * 0.07f) value *= 0.3f;
    score[static_cast<size_t>(midi - low)] = value;
  }

  const float maxScore = *std::max_element(score.begin(), score.end());
  if (maxScore < 1e-8f) return {};
  const float threshold = maxScore * 0.34f;

  struct Candidate {
    int midi;
    float score;
  };
  std::vector<Candidate> candidates;
  for (int midi = low; midi <= high; ++midi) {
    const float value = score[static_cast<size_t>(midi - low)];
    const float left = midi == low ? 0 : score[static_cast<size_t>(midi - low - 1)];
    const float right = midi == high ? 0 : score[static_cast<size_t>(midi - low + 1)];
    if (value >= threshold && value >= left && value >= right) candidates.push_back({midi, value});
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

  std::vector<Candidate> kept;
  for (const Candidate& candidate : candidates) {
    const bool adjacent = std::any_of(kept.begin(), kept.end(), [&](const Candidate& note) {
      return std::abs(note.midi - candidate.midi) <= 1;
    });
    if (adjacent) continue;
    const bool harmonic = std::any_of(kept.begin(), kept.end(), [&](const Candidate& note) {
      return nearHarmonic(candidate.midi, note.midi);
    });
    if (harmonic) continue;

    std::vector<Candidate> next;
    next.reserve(kept.size() + 1);
    for (const Candidate& note : kept) {
      if (nearHarmonic(note.midi, candidate.midi) && candidate.score > note.score * 0.35f) continue;
      next.push_back(note);
    }
    next.push_back(candidate);
    if (static_cast<int>(next.size()) > maxNotes) {
      std::sort(next.begin(), next.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
      next.resize(static_cast<size_t>(maxNotes));
    }
    kept = std::move(next);
  }

  std::vector<int> notes;
  notes.reserve(kept.size());
  for (const Candidate& note : kept) notes.push_back(note.midi);
  std::sort(notes.begin(), notes.end());
  return notes;
}

}  // namespace

std::vector<int> assignFrets(const std::vector<int>& pitches, const std::vector<int>& tuning, int maxFret) {
  const int strings = static_cast<int>(tuning.size());
  const int count = static_cast<int>(pitches.size());
  std::vector<int> best(static_cast<size_t>(strings), -1);
  if (strings == 0 || count == 0) return best;

  int bestCost = std::numeric_limits<int>::max();
  int bestPlayed = -1;
  std::vector<int> current(static_cast<size_t>(strings), -1);
  std::vector<char> used(static_cast<size_t>(strings), 0);

  std::function<void(int, int, int)> search = [&](int index, int played, int cost) {
    const int remain = count - index;
    if (played + remain < bestPlayed) return;
    if (played + remain == bestPlayed && cost >= bestCost) return;
    if (index == count) {
      int low = 99;
      int high = -1;
      for (int fret : current) {
        if (fret < 0) continue;
        low = std::min(low, fret);
        high = std::max(high, fret);
      }
      const int total = cost + (high >= 0 ? (high - low) * 4 : 0);
      if (played > bestPlayed || (played == bestPlayed && total < bestCost)) {
        bestPlayed = played;
        bestCost = total;
        best = current;
      }
      return;
    }

    const int pitch = pitches[static_cast<size_t>(index)];
    for (int stringIndex = 0; stringIndex < strings; ++stringIndex) {
      if (used[static_cast<size_t>(stringIndex)]) continue;
      const int fret = pitch - tuning[static_cast<size_t>(stringIndex)];
      if (fret < 0 || fret > maxFret) continue;
      int extra = fret * 2;
      if (fret > 12) extra += (fret - 12) * 3;
      used[static_cast<size_t>(stringIndex)] = 1;
      current[static_cast<size_t>(stringIndex)] = fret;
      search(index + 1, played + 1, cost + extra);
      current[static_cast<size_t>(stringIndex)] = -1;
      used[static_cast<size_t>(stringIndex)] = 0;
    }
    search(index + 1, played, cost + 36);
  };

  search(0, 0, 0);
  return best;
}

std::vector<int> detectMidis(const float* samples, int count, int sampleRate, int lowMidi, int highMidi, int maxNotes) {
  if (!samples || count < 64 || sampleRate < 1000) return {};
  int fftSize = 4096;
  while (fftSize > count && fftSize > 1024) fftSize >>= 1;
  const auto mag = spectrum(samples, count, count / 2, fftSize);
  return pickNotes(mag, fftSize, sampleRate, lowMidi, highMidi, maxNotes);
}

namespace {

struct Shape {
  std::vector<int> frets;
  std::vector<float> amps;
  double local = 0;
};

struct Column {
  double time = 0;
  std::vector<int> midi;
  std::vector<float> amp;
};

int handAnchor(const std::vector<int>& frets) {
  int low = 100;
  bool fretted = false;
  for (int fret : frets) {
    if (fret <= 0) continue;
    fretted = true;
    low = std::min(low, fret);
  }
  return fretted ? low : 0;
}

double localCost(const std::vector<int>& frets) {
  int low = 99;
  int high = -1;
  int minString = 99;
  int maxString = -1;
  int voices = 0;
  double cost = 0;
  for (int stringIndex = 0; stringIndex < static_cast<int>(frets.size()); ++stringIndex) {
    const int fret = frets[static_cast<size_t>(stringIndex)];
    if (fret < 0) continue;
    ++voices;
    minString = std::min(minString, stringIndex);
    maxString = std::max(maxString, stringIndex);
    cost += fret * 0.25;
    if (fret > 12) cost += (fret - 12) * 0.9;
    if (fret > 0) {
      low = std::min(low, fret);
      high = std::max(high, fret);
    }
  }
  if (high >= 0) cost += (high - low) * 3.5;
  if (voices >= 2) cost += std::max(0, (maxString - minString) - (voices - 1)) * 2.5;
  return cost;
}

double transitionCost(const std::vector<int>& previous, const std::vector<int>& current, const std::vector<int>& tuning, int maxFret) {
  double cost = std::abs(handAnchor(current) - handAnchor(previous)) * 0.55;
  const int strings = static_cast<int>(tuning.size());
  for (int stringIndex = 0; stringIndex < strings; ++stringIndex) {
    if (current[static_cast<size_t>(stringIndex)] < 0) continue;
    const int midi = tuning[static_cast<size_t>(stringIndex)] + current[static_cast<size_t>(stringIndex)];
    int bestString = -1;
    int bestInterval = 100;
    for (int previousString = 0; previousString < strings; ++previousString) {
      if (previous[static_cast<size_t>(previousString)] < 0) continue;
      const int interval = std::abs(midi - (tuning[static_cast<size_t>(previousString)] + previous[static_cast<size_t>(previousString)]));
      if (interval < bestInterval) {
        bestInterval = interval;
        bestString = previousString;
      }
    }
    if (bestString < 0) continue;
    const int stay = midi - tuning[static_cast<size_t>(bestString)];
    const bool canStay = stay >= 0 && stay <= maxFret;
    if (stringIndex == bestString) {
      if (bestInterval <= 7) cost -= 7;
      else if (bestInterval <= 12) cost -= 2;
    } else if (canStay && bestInterval <= 7 && std::abs(stay - previous[static_cast<size_t>(bestString)]) <= 12) {
      cost += 8;
    }
  }
  return cost;
}

std::vector<Shape> shapesFor(const Column& column, const std::vector<int>& tuning, int maxFret) {
  const int strings = static_cast<int>(tuning.size());
  const int count = static_cast<int>(column.midi.size());
  std::vector<Shape> shapes;
  int bestPlayed = -1;
  std::vector<int> current(static_cast<size_t>(strings), -1);
  std::vector<float> amps(static_cast<size_t>(strings), 0.f);
  std::vector<char> used(static_cast<size_t>(strings), 0);
  std::function<void(int, int)> search = [&](int index, int played) {
    if (played + (count - index) < bestPlayed) return;
    if (index == count) {
      if (played > bestPlayed) {
        bestPlayed = played;
        shapes.clear();
      }
      if (played == bestPlayed) {
        Shape shape;
        shape.frets = current;
        shape.amps = amps;
        shape.local = localCost(current);
        shapes.push_back(std::move(shape));
      }
      return;
    }
    const int pitch = column.midi[static_cast<size_t>(index)];
    for (int stringIndex = 0; stringIndex < strings; ++stringIndex) {
      if (used[static_cast<size_t>(stringIndex)]) continue;
      const int fret = pitch - tuning[static_cast<size_t>(stringIndex)];
      if (fret < 0 || fret > maxFret) continue;
      used[static_cast<size_t>(stringIndex)] = 1;
      current[static_cast<size_t>(stringIndex)] = fret;
      amps[static_cast<size_t>(stringIndex)] = column.amp[static_cast<size_t>(index)];
      search(index + 1, played + 1);
      current[static_cast<size_t>(stringIndex)] = -1;
      amps[static_cast<size_t>(stringIndex)] = 0.f;
      used[static_cast<size_t>(stringIndex)] = 0;
    }
    search(index + 1, played);
  };
  search(0, 0);
  return shapes;
}

int cellForTime(const TranscribeRequest& request, double time) {
  int best = -1;
  double bestDistance = 1e9;
  for (int cell = 0; cell < static_cast<int>(request.cellTime.size()); ++cell) {
    const double start = request.cellTime[static_cast<size_t>(cell)];
    const double end = start + request.cellDuration[static_cast<size_t>(cell)];
    if (time < start - 0.03 || time >= end + 0.02) continue;
    const double distance = std::abs(time - start);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = cell;
    }
  }
  return best;
}

}  // namespace

std::vector<TabEvent> tabFromNotes(const std::vector<PitchNote>& notes, const TranscribeRequest& request) {
  std::vector<TabEvent> events;
  const int cells = static_cast<int>(request.cellQn.size());
  if (cells == 0 || request.tuning.empty() || notes.empty()) return events;
  if (request.cellTime.size() != request.cellQn.size() || request.cellDuration.size() != request.cellQn.size() ||
      request.cellQnDuration.size() != request.cellQn.size()) {
    return events;
  }

  std::vector<PitchNote> ordered = notes;
  std::sort(ordered.begin(), ordered.end(), [](const PitchNote& a, const PitchNote& b) {
    if (a.start != b.start) return a.start < b.start;
    return a.amplitude > b.amplitude;
  });

  std::vector<Column> columns;
  for (const PitchNote& note : ordered) {
    if (columns.empty() || note.start - columns.back().time > 0.045) {
      columns.push_back(Column{note.start, {note.midi}, {note.amplitude}});
      continue;
    }
    Column& column = columns.back();
    const auto existing = std::find(column.midi.begin(), column.midi.end(), note.midi);
    if (existing != column.midi.end()) {
      float& amp = column.amp[static_cast<size_t>(existing - column.midi.begin())];
      amp = std::max(amp, note.amplitude);
    } else {
      column.midi.push_back(note.midi);
      column.amp.push_back(note.amplitude);
    }
  }

  const int stringCount = static_cast<int>(request.tuning.size());
  for (Column& column : columns) {
    if (static_cast<int>(column.midi.size()) <= stringCount) continue;
    std::vector<int> order(column.midi.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return column.amp[static_cast<size_t>(a)] > column.amp[static_cast<size_t>(b)]; });
    order.resize(static_cast<size_t>(stringCount));
    Column trimmed;
    trimmed.time = column.time;
    for (int index : order) {
      trimmed.midi.push_back(column.midi[static_cast<size_t>(index)]);
      trimmed.amp.push_back(column.amp[static_cast<size_t>(index)]);
    }
    column = std::move(trimmed);
  }

  struct State {
    std::vector<int> frets;
    std::vector<float> amps;
    double cost = 0;
    int previous = -1;
  };
  std::vector<std::vector<State>> layers;
  layers.reserve(columns.size());
  for (const Column& column : columns) {
    const std::vector<Shape> shapes = shapesFor(column, request.tuning, request.maxFret);
    if (shapes.empty()) continue;
    std::vector<State> next;
    if (layers.empty()) {
      for (const Shape& shape : shapes) next.push_back(State{shape.frets, shape.amps, shape.local, -1});
    } else {
      const std::vector<State>& previous = layers.back();
      for (int index = 0; index < static_cast<int>(previous.size()); ++index) {
        for (const Shape& shape : shapes) {
          const double cost = previous[static_cast<size_t>(index)].cost + shape.local +
                              transitionCost(previous[static_cast<size_t>(index)].frets, shape.frets, request.tuning, request.maxFret);
          next.push_back(State{shape.frets, shape.amps, cost, index});
        }
      }
    }
    std::sort(next.begin(), next.end(), [](const State& a, const State& b) { return a.cost < b.cost; });
    if (next.size() > 12) next.resize(12);
    layers.push_back(std::move(next));
  }
  if (layers.empty()) return events;

  std::vector<int> chosen(layers.size(), 0);
  for (int layer = static_cast<int>(layers.size()) - 2; layer >= 0; --layer) {
    chosen[static_cast<size_t>(layer)] = layers[static_cast<size_t>(layer + 1)][static_cast<size_t>(chosen[static_cast<size_t>(layer + 1)])].previous;
  }

  struct Accum {
    std::vector<int> frets;
    std::vector<float> amps;
    bool used = false;
  };
  std::vector<Accum> placed(static_cast<size_t>(cells));
  for (Accum& cell : placed) {
    cell.frets.assign(static_cast<size_t>(stringCount), -1);
    cell.amps.assign(static_cast<size_t>(stringCount), 0.f);
  }
  int layerIndex = 0;
  for (const Column& column : columns) {
    const std::vector<Shape> probe = shapesFor(column, request.tuning, request.maxFret);
    if (probe.empty()) continue;
    const int cell = cellForTime(request, column.time);
    const State& state = layers[static_cast<size_t>(layerIndex)][static_cast<size_t>(chosen[static_cast<size_t>(layerIndex)])];
    ++layerIndex;
    if (cell < 0) continue;
    Accum& accum = placed[static_cast<size_t>(cell)];
    accum.used = true;
    for (int stringIndex = 0; stringIndex < stringCount; ++stringIndex) {
      const int fret = state.frets[static_cast<size_t>(stringIndex)];
      if (fret < 0) continue;
      const float amp = state.amps[static_cast<size_t>(stringIndex)];
      if (accum.frets[static_cast<size_t>(stringIndex)] >= 0 && accum.amps[static_cast<size_t>(stringIndex)] >= amp) continue;
      accum.frets[static_cast<size_t>(stringIndex)] = fret;
      accum.amps[static_cast<size_t>(stringIndex)] = amp;
    }
  }

  for (int cell = 0; cell < cells; ++cell) {
    const Accum& accum = placed[static_cast<size_t>(cell)];
    if (!accum.used) continue;
    if (std::none_of(accum.frets.begin(), accum.frets.end(), [](int fret) { return fret >= 0; })) continue;
    TabEvent event;
    event.qn = request.cellQn[static_cast<size_t>(cell)];
    event.qnDuration = request.cellQnDuration[static_cast<size_t>(cell)];
    event.frets = accum.frets;
    for (int stringIndex = 0; stringIndex < stringCount; ++stringIndex) {
      if (accum.frets[static_cast<size_t>(stringIndex)] >= 0) event.attacks |= 1 << stringIndex;
    }
    events.push_back(std::move(event));
  }
  return events;
}

std::vector<TabEvent> transcribe(const TranscribeRequest& request, std::atomic<float>* progress) {
  const int cells = static_cast<int>(request.cellQn.size());
  if (!request.samples || request.sampleCount <= 0 || cells == 0 || request.tuning.empty()) return {};
  if (request.cellTime.size() != request.cellQn.size() || request.cellDuration.size() != request.cellQn.size() ||
      request.cellQnDuration.size() != request.cellQn.size()) {
    return {};
  }
  const int lowMidi = std::max(21, *std::min_element(request.tuning.begin(), request.tuning.end()));
  const int highMidi = std::min(108, *std::max_element(request.tuning.begin(), request.tuning.end()) + request.maxFret);
  const std::vector<PitchNote> notes = analyzePitch(request.samples, request.sampleCount, request.sampleRate, lowMidi, highMidi, progress);
  TranscribeRequest timing = request;
  for (double& time : timing.cellTime) time -= request.startTime;
  return tabFromNotes(notes, timing);
}

}  // namespace guitartabs
