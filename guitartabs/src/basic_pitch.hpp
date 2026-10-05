#pragma once

#include <atomic>
#include <vector>

namespace guitartabs {

struct PitchNote {
  int midi = 0;
  double start = 0;
  double end = 0;
  float amplitude = 0;
};

// Decode one Basic Pitch posteriorgram. `note` and `onset` are row-major
// [frames, 88], and `frameTime` is in seconds.
std::vector<PitchNote> decodePitch(const float* note, const float* onset, const double* frameTime, int frames, int minMidi,
                                   int maxMidi, float onsetThreshold, float frameThreshold, int minFrames);

// Spotify Basic Pitch (Apache 2.0): audio at any rate, mono, to MIDI notes.
std::vector<PitchNote> analyzePitch(const float* samples, int count, int sampleRate, int minMidi, int maxMidi,
                                    std::atomic<float>* progress);

}  // namespace guitartabs
