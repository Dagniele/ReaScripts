#include "basic_pitch.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace guitartabs {
namespace {

constexpr int kBins = 88;
constexpr int kMidiOffset = 21;

float at(const float* matrix, int time, int freq) { return matrix[time * kBins + freq]; }

void setAt(float* matrix, int time, int freq, float value) { matrix[time * kBins + freq] = value; }

double midiHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

bool integerHarmonic(int upper, int lower) {
  if (upper <= lower) return false;
  const double ratio = midiHz(upper) / midiHz(lower);
  const int partial = static_cast<int>(std::lround(ratio));
  if (partial < 2 || partial > 6) return false;
  return std::abs(ratio - partial) / static_cast<double>(partial) < 0.03;
}

bool stackedOctave(int upper, int lower) {
  const int delta = upper - lower;
  return delta == 12 || delta == 24;
}

std::vector<float> inferredOnsets(const float* note, const float* onset, int frames) {
  std::vector<float> diff(static_cast<size_t>(frames * kBins), 0.f);
  float maxOnset = 0.f;
  for (int time = 0; time < frames; ++time) {
    for (int freq = 0; freq < kBins; ++freq) maxOnset = std::max(maxOnset, at(onset, time, freq));
  }
  for (int lag = 1; lag <= 2; ++lag) {
    for (int time = 0; time < frames; ++time) {
      for (int freq = 0; freq < kBins; ++freq) {
        const float previous = time >= lag ? at(note, time - lag, freq) : 0.f;
        const float delta = at(note, time, freq) - previous;
        float& slot = diff[static_cast<size_t>(time * kBins + freq)];
        slot = lag == 1 ? delta : std::min(slot, delta);
      }
    }
  }
  float maxDiff = 0.f;
  for (int time = 0; time < frames; ++time) {
    for (int freq = 0; freq < kBins; ++freq) {
      float& slot = diff[static_cast<size_t>(time * kBins + freq)];
      if (time < 2 || slot < 0.f) slot = 0.f;
      maxDiff = std::max(maxDiff, slot);
    }
  }
  std::vector<float> mixed(static_cast<size_t>(frames * kBins), 0.f);
  const float scale = maxDiff > 1e-8f ? maxOnset / maxDiff : 0.f;
  for (int time = 0; time < frames; ++time) {
    for (int freq = 0; freq < kBins; ++freq) {
      const float grown = diff[static_cast<size_t>(time * kBins + freq)] * scale;
      mixed[static_cast<size_t>(time * kBins + freq)] = std::max(at(onset, time, freq), grown);
    }
  }
  return mixed;
}

struct Peak {
  int time;
  int freq;
};

void silenceBand(std::vector<float>& energy, int frames, int time, int freq) {
  if (time < 0 || time >= frames || freq < 0 || freq >= kBins) return;
  setAt(energy.data(), time, freq, 0.f);
}

void clearSpan(std::vector<float>& energy, int frames, int start, int end, int freq) {
  for (int time = start; time < end; ++time) {
    silenceBand(energy, frames, time, freq);
    silenceBand(energy, frames, time, freq - 1);
    silenceBand(energy, frames, time, freq + 1);
  }
}

double meanBand(const float* note, int start, int end, int freq) {
  if (end <= start) return 0;
  double sum = 0;
  for (int time = start; time < end; ++time) sum += at(note, time, freq);
  return sum / static_cast<double>(end - start);
}

std::vector<PitchNote> dropWeakHarmonics(std::vector<PitchNote> notes) {
  std::vector<char> drop(notes.size(), 0);
  for (size_t i = 0; i < notes.size(); ++i) {
    for (size_t j = 0; j < notes.size(); ++j) {
      if (i == j || drop[j]) continue;
      const PitchNote& lower = notes[i];
      const PitchNote& upper = notes[j];
      if (!integerHarmonic(upper.midi, lower.midi)) continue;
      const double upperLength = std::max(1e-4, upper.end - upper.start);
      const double overlap = std::min(lower.end, upper.end) - std::max(lower.start, upper.start);
      if (overlap < 0.55 * upperLength) continue;
      if (std::abs(lower.start - upper.start) > 0.05) continue;
      // A plucked string rings its octave. A fretted octave that is clearly louder than the lower note is kept.
      if (!stackedOctave(upper.midi, lower.midi) && upper.amplitude > lower.amplitude * 0.92f) continue;
      if (stackedOctave(upper.midi, lower.midi) && upper.amplitude > lower.amplitude * 1.35f) continue;
      drop[j] = 1;
    }
  }
  std::vector<PitchNote> kept;
  kept.reserve(notes.size());
  for (size_t i = 0; i < notes.size(); ++i) {
    if (!drop[i]) kept.push_back(notes[i]);
  }
  return kept;
}

}  // namespace

std::vector<PitchNote> decodePitch(const float* note, const float* onset, const double* frameTime, int frames, int minMidi,
                                   int maxMidi, float onsetThreshold, float frameThreshold, int minFrames) {
  std::vector<PitchNote> notes;
  if (!note || !onset || !frameTime || frames < 3) return notes;
  minMidi = std::clamp(minMidi, kMidiOffset, kMidiOffset + kBins - 1);
  maxMidi = std::clamp(maxMidi, minMidi, kMidiOffset + kBins - 1);

  std::vector<float> frame(note, note + static_cast<size_t>(frames * kBins));
  std::vector<float> onsetCopy(onset, onset + static_cast<size_t>(frames * kBins));
  for (int freq = 0; freq < kBins; ++freq) {
    const int midi = freq + kMidiOffset;
    if (midi >= minMidi && midi <= maxMidi) continue;
    for (int time = 0; time < frames; ++time) {
      setAt(frame.data(), time, freq, 0.f);
      setAt(onsetCopy.data(), time, freq, 0.f);
    }
  }

  const std::vector<float> mixed = inferredOnsets(frame.data(), onsetCopy.data(), frames);
  std::vector<Peak> peaks;
  for (int time = 1; time < frames - 1; ++time) {
    for (int freq = 0; freq < kBins; ++freq) {
      const float value = at(mixed.data(), time, freq);
      if (value < onsetThreshold) continue;
      if (value <= at(mixed.data(), time - 1, freq) || value <= at(mixed.data(), time + 1, freq)) continue;
      peaks.push_back({time, freq});
    }
  }
  std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) {
    if (a.time != b.time) return a.time > b.time;
    return a.freq > b.freq;
  });

  std::vector<float> energy = frame;
  constexpr int kEnergySlop = 11;
  auto pushNote = [&](int start, int end, int freq) {
    if (end - start <= minFrames || start < 0 || end > frames) return;
    PitchNote item;
    item.midi = freq + kMidiOffset;
    item.start = frameTime[start];
    item.end = end < frames ? frameTime[end] : frameTime[frames - 1] + (256.0 / 22050.0);
    if (item.end < item.start) item.end = item.start + (256.0 / 22050.0);
    item.amplitude = static_cast<float>(meanBand(frame.data(), start, end, freq));
    notes.push_back(item);
  };

  for (const Peak& peak : peaks) {
    if (peak.time >= frames - 1) continue;
    int end = peak.time + 1;
    int below = 0;
    while (end < frames - 1 && below < kEnergySlop) {
      if (at(energy.data(), end, peak.freq) < frameThreshold) ++below;
      else below = 0;
      ++end;
    }
    end -= below;
    if (end - peak.time <= minFrames) continue;
    clearSpan(energy, frames, peak.time, end, peak.freq);
    pushNote(peak.time, end, peak.freq);
  }

  while (true) {
    int mid = 0;
    int freq = 0;
    float best = 0.f;
    for (int time = 0; time < frames; ++time) {
      for (int bin = 0; bin < kBins; ++bin) {
        const float value = at(energy.data(), time, bin);
        if (value > best) {
          best = value;
          mid = time;
          freq = bin;
        }
      }
    }
    if (best <= frameThreshold) break;
    silenceBand(energy, frames, mid, freq);

    int end = mid + 1;
    int below = 0;
    while (end < frames - 1 && below < kEnergySlop) {
      if (at(energy.data(), end, freq) < frameThreshold) ++below;
      else below = 0;
      silenceBand(energy, frames, end, freq);
      silenceBand(energy, frames, end, freq - 1);
      silenceBand(energy, frames, end, freq + 1);
      ++end;
    }
    end = end - 1 - below;

    below = 0;
    int start = mid - 1;
    while (start > 0 && below < kEnergySlop) {
      if (at(energy.data(), start, freq) < frameThreshold) ++below;
      else below = 0;
      silenceBand(energy, frames, start, freq);
      silenceBand(energy, frames, start, freq - 1);
      silenceBand(energy, frames, start, freq + 1);
      --start;
    }
    start = start + 1 + below;
    if (start < 0) start = 0;
    if (end >= frames) end = frames - 1;
    if (end - start <= minFrames) continue;
    pushNote(start, end, freq);
  }

  std::sort(notes.begin(), notes.end(), [](const PitchNote& a, const PitchNote& b) {
    if (a.start != b.start) return a.start < b.start;
    return a.midi < b.midi;
  });
  return dropWeakHarmonics(std::move(notes));
}

}  // namespace guitartabs
