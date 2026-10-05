#include "transcribe.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <limits>

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

float frameRms(const float* samples, int count) {
  if (count <= 0) return 0;
  double sum = 0;
  for (int i = 0; i < count; ++i) sum += static_cast<double>(samples[i]) * samples[i];
  return static_cast<float>(std::sqrt(sum / count));
}

int chooseFftSize(const std::vector<int>& tuning, double medianDuration) {
  const int lowest = *std::min_element(tuning.begin(), tuning.end());
  if (lowest <= 35) return 8192;
  if (medianDuration < 0.14) return 2048;
  return 4096;
}

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

struct YinPitch {
  int midi = -1;
  float cmnd = 1.f;
};

YinPitch estimateMidi(const float* samples, int count, int sampleRate, int lowMidi, int highMidi) {
  if (!samples || count < 128 || sampleRate < 1000) return {};
  const int lowest = std::clamp(lowMidi, 20, 96);
  const int highest = std::clamp(highMidi, lowest, 108);
  const int maxTau = std::min(count / 2, static_cast<int>(std::floor(sampleRate / midiFrequency(lowest))));
  const int minTau = std::max(2, static_cast<int>(std::ceil(sampleRate / midiFrequency(highest))));
  if (maxTau <= minTau + 2) return {};

  int fftSize = 1;
  while (fftSize < count * 2) fftSize <<= 1;
  std::vector<std::complex<float>> spectrumBins(static_cast<size_t>(fftSize));
  for (int i = 0; i < count; ++i) spectrumBins[static_cast<size_t>(i)] = samples[i];
  fft(spectrumBins);
  for (auto& bin : spectrumBins) bin = std::norm(bin);
  fft(spectrumBins);
  for (auto& bin : spectrumBins) bin = std::conj(bin);
  const float scale = 1.f / static_cast<float>(fftSize);

  std::vector<double> prefix(static_cast<size_t>(count) + 1, 0.0);
  for (int i = 0; i < count; ++i) prefix[static_cast<size_t>(i) + 1] = prefix[static_cast<size_t>(i)] + static_cast<double>(samples[i]) * samples[i];

  std::vector<float> cmnd(static_cast<size_t>(maxTau) + 1, 1.f);
  double running = 0;
  int bestTau = -1;
  for (int tau = 1; tau <= maxTau; ++tau) {
    const double left = prefix[static_cast<size_t>(count - tau)];
    const double right = prefix[static_cast<size_t>(count)] - prefix[static_cast<size_t>(tau)];
    const double correlation = static_cast<double>(spectrumBins[static_cast<size_t>(tau)].real()) * scale;
    const double difference = std::max(0.0, left + right - 2.0 * correlation);
    running += difference;
    cmnd[static_cast<size_t>(tau)] = running > 1e-12 ? static_cast<float>(difference * tau / running) : 1.f;
    if (tau >= minTau && (bestTau < 0 || cmnd[static_cast<size_t>(tau)] < cmnd[static_cast<size_t>(bestTau)])) bestTau = tau;
  }

  int tau = -1;
  for (int candidate = minTau; candidate <= maxTau; ++candidate) {
    if (cmnd[static_cast<size_t>(candidate)] < 0.15f) {
      while (candidate + 1 <= maxTau && cmnd[static_cast<size_t>(candidate + 1)] < cmnd[static_cast<size_t>(candidate)]) ++candidate;
      tau = candidate;
      break;
    }
  }
  if (tau < 0) {
    if (bestTau < 0 || cmnd[static_cast<size_t>(bestTau)] > 0.5f) return {};
    tau = bestTau;
  }

  float refined = static_cast<float>(tau);
  if (tau > minTau && tau < maxTau) {
    const float earlier = cmnd[static_cast<size_t>(tau - 1)];
    const float here = cmnd[static_cast<size_t>(tau)];
    const float later = cmnd[static_cast<size_t>(tau + 1)];
    const float denom = 2.f * (earlier - 2.f * here + later);
    if (std::abs(denom) > 1e-8f) {
      const float delta = (earlier - later) / denom;
      if (std::abs(delta) < 1.f) refined = static_cast<float>(tau) + delta;
    }
  }
  if (refined < 1.f) return {};
  const double frequency = sampleRate / static_cast<double>(refined);
  const double midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
  YinPitch pitch;
  pitch.cmnd = cmnd[static_cast<size_t>(tau)];
  pitch.midi = std::clamp(static_cast<int>(std::lround(midi)), lowest, highest);
  return pitch;
}

std::vector<int> detectMidis(const float* samples, int count, int sampleRate, int lowMidi, int highMidi, int maxNotes) {
  if (!samples || count < 64 || sampleRate < 1000) return {};
  int fftSize = 4096;
  while (fftSize > count && fftSize > 1024) fftSize >>= 1;
  const auto mag = spectrum(samples, count, count / 2, fftSize);
  return pickNotes(mag, fftSize, sampleRate, lowMidi, highMidi, maxNotes);
}

std::vector<TabEvent> transcribe(const TranscribeRequest& request, std::atomic<float>* progress) {
  std::vector<TabEvent> events;
  const int cells = static_cast<int>(request.cellQn.size());
  if (!request.samples || request.sampleCount <= 0 || cells == 0 || request.tuning.empty()) return events;
  if (request.cellTime.size() != request.cellQn.size() || request.cellDuration.size() != request.cellQn.size() ||
      request.cellQnDuration.size() != request.cellQn.size()) {
    return events;
  }

  float peak = 0;
  for (int i = 0; i < request.sampleCount; ++i) peak = std::max(peak, std::abs(request.samples[i]));
  const float gate = std::max(0.0025f, peak * 0.035f);

  std::vector<double> durations = request.cellDuration;
  std::sort(durations.begin(), durations.end());
  const double medianDuration = durations[durations.size() / 2];
  const int fftSize = chooseFftSize(request.tuning, medianDuration);
  const int lowMidi = std::max(20, *std::min_element(request.tuning.begin(), request.tuning.end()) - 2);
  const int highMidi = std::min(96, *std::max_element(request.tuning.begin(), request.tuning.end()) + request.maxFret);

  std::vector<int> previous(request.tuning.size(), -1);
  float previousRms = 0;
  const int stringCount = static_cast<int>(request.tuning.size());
  const double lowestFrequency = midiFrequency(lowMidi);
  const int minimumSamples = std::clamp(static_cast<int>(request.sampleRate / lowestFrequency * 4.0), 512, request.sampleRate / 2);

  for (int cell = 0; cell < cells; ++cell) {
    if (progress && cell % 8 == 0) {
      progress->store(static_cast<float>(cell) / static_cast<float>(cells), std::memory_order_relaxed);
    }
    const int sliceStart = std::clamp(static_cast<int>((request.cellTime[static_cast<size_t>(cell)] - request.startTime) * request.sampleRate), 0,
                                       request.sampleCount);
    const int sliceEnd = std::clamp(
        static_cast<int>((request.cellTime[static_cast<size_t>(cell)] + request.cellDuration[static_cast<size_t>(cell)] - request.startTime) *
                         request.sampleRate),
        sliceStart, request.sampleCount);
    const float energy = frameRms(request.samples + sliceStart, std::max(1, sliceEnd - sliceStart));

    std::vector<int> frets(static_cast<size_t>(stringCount), -1);
    if (energy >= gate) {
      int useStart = sliceStart;
      int useCount = std::max(0, sliceEnd - sliceStart);
      const int cap = std::max(minimumSamples, request.sampleRate / 2);
      if (useCount > cap) {
        useStart += (useCount - cap) / 2;
        useCount = cap;
      } else if (useCount < minimumSamples) {
        const int center = sliceStart + useCount / 2;
        useStart = center - minimumSamples / 2;
        useCount = minimumSamples;
      }
      useStart = std::clamp(useStart, 0, request.sampleCount);
      useCount = std::clamp(useCount, 0, request.sampleCount - useStart);

      const YinPitch pitch = estimateMidi(request.samples + useStart, useCount, request.sampleRate, lowMidi, highMidi);
      const auto mag = spectrumSpan(request.samples, request.sampleCount, useStart, useStart + useCount, fftSize);
      std::vector<int> spectral = pickNotes(mag, fftSize, request.sampleRate, lowMidi, highMidi, stringCount);
      std::vector<int> notes;
      if (pitch.midi >= 0 && pitch.cmnd < 0.2f) {
        int fundamental = pitch.midi;
        for (int extra : spectral) {
          if (extra < fundamental && nearHarmonic(fundamental, extra)) fundamental = extra;
        }
        notes.push_back(fundamental);
        for (int extra : spectral) {
          if (std::abs(extra - fundamental) <= 1) continue;
          if (nearHarmonic(extra, fundamental) || nearHarmonic(fundamental, extra)) continue;
          notes.push_back(extra);
          if (notes.size() >= 3) break;
        }
      } else if (!spectral.empty()) {
        notes = std::move(spectral);
      } else if (pitch.midi >= 0) {
        notes.push_back(pitch.midi);
      }
      frets = assignFrets(notes, request.tuning, request.maxFret);
      if (notes.size() == 1) {
        int previousString = -1;
        int previousFret = -1;
        for (int stringIndex = 0; stringIndex < stringCount; ++stringIndex) {
          if (previous[static_cast<size_t>(stringIndex)] < 0) continue;
          previousString = stringIndex;
          previousFret = previous[static_cast<size_t>(stringIndex)];
          break;
        }
        if (previousString >= 0) {
          const int stayed = notes[0] - request.tuning[static_cast<size_t>(previousString)];
          if (stayed >= 0 && stayed <= request.maxFret && std::abs(stayed - previousFret) <= 12) {
            std::fill(frets.begin(), frets.end(), -1);
            frets[static_cast<size_t>(previousString)] = stayed;
          }
        }
      }
    }

    int attacks = 0;
    const bool onset = energy > previousRms * 1.55f && energy > gate * 1.4f;
    for (int stringIndex = 0; stringIndex < stringCount; ++stringIndex) {
      const int fret = frets[static_cast<size_t>(stringIndex)];
      if (fret < 0) continue;
      if (cell == 0 || previous[static_cast<size_t>(stringIndex)] != fret || onset) attacks |= 1 << stringIndex;
    }

    const bool sounding = std::any_of(frets.begin(), frets.end(), [](int fret) { return fret >= 0; });
    if (sounding) {
      TabEvent event;
      event.qn = request.cellQn[static_cast<size_t>(cell)];
      event.qnDuration = request.cellQnDuration[static_cast<size_t>(cell)];
      event.frets = frets;
      event.attacks = attacks;
      events.push_back(std::move(event));
    }
    previous = frets;
    previousRms = energy;
  }

  if (progress) progress->store(1.f, std::memory_order_relaxed);
  return events;
}

}  // namespace guitartabs
