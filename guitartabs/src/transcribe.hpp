#pragma once

#include "document.hpp"

#include <atomic>
#include <vector>

namespace guitartabs {

struct TranscribeRequest {
  const float* samples = nullptr;
  int sampleCount = 0;
  int sampleRate = 22050;
  double startTime = 0;
  std::vector<double> cellQn;
  std::vector<double> cellTime;
  std::vector<double> cellDuration;
  std::vector<double> cellQnDuration;
  std::vector<int> tuning;
  int maxFret = 22;
};

std::vector<int> assignFrets(const std::vector<int>& pitches, const std::vector<int>& tuning, int maxFret);

std::vector<int> detectMidis(const float* samples, int count, int sampleRate, int lowMidi, int highMidi, int maxNotes);

std::vector<TabEvent> transcribe(const TranscribeRequest& request, std::atomic<float>* progress);

}  // namespace guitartabs
