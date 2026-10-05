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

double spectralEnergy(const std::vector<float>& mag) {
  double sum = 0;
  for (float value : mag) sum += static_cast<double>(value) * value;
  return sum;
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
  std::vector<int> previousNotes;
  std::vector<float> previousMag;
  float previousRms = 0;
  const int stringCount = static_cast<int>(request.tuning.size());

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
      const auto mag = spectrumSpan(request.samples, request.sampleCount, sliceStart, sliceEnd, fftSize);
      std::vector<float> residual = mag;
      if (previousMag.size() == mag.size()) {
        float framePeak = 0;
        float prevPeak = 0;
        for (float value : mag) framePeak = std::max(framePeak, value);
        for (float value : previousMag) prevPeak = std::max(prevPeak, value);
        const float carry = prevPeak > 1e-8f ? framePeak / prevPeak * 0.85f : 0;
        for (size_t bin = 0; bin < residual.size(); ++bin) residual[bin] = std::max(0.f, mag[bin] - previousMag[bin] * carry);
      }
      const bool sustain = !previousMag.empty() && spectralEnergy(residual) < spectralEnergy(mag) * 0.22 &&
                           std::any_of(previous.begin(), previous.end(), [](int fret) { return fret >= 0; });
      if (sustain) {
        frets = previous;
      } else {
        const std::vector<float>& source = previousMag.empty() ? mag : residual;
        std::vector<int> notes = pickNotes(source, fftSize, request.sampleRate, lowMidi, highMidi, stringCount);
        if (notes.empty() && &source != &mag) notes = pickNotes(mag, fftSize, request.sampleRate, lowMidi, highMidi, stringCount);
        if (!previousNotes.empty()) {
          std::vector<int> fresh;
          for (int note : notes) {
            const bool held = std::any_of(previousNotes.begin(), previousNotes.end(), [&](int old) { return std::abs(old - note) <= 1; });
            if (!held) fresh.push_back(note);
          }
          if (!fresh.empty()) notes = std::move(fresh);
        }
        frets = assignFrets(notes, request.tuning, request.maxFret);
        previousNotes = std::move(notes);
      }
      previousMag = mag;
    } else {
      previousNotes.clear();
      previousMag.clear();
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
