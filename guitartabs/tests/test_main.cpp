#include "document.hpp"
#include "json.hpp"
#include "transcribe.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* text, int line) {
  if (condition) return;
  std::cerr << "FAIL line " << line << ": " << text << "\n";
  ++g_failures;
}

#define CHECK(cond) check((cond), #cond, __LINE__)

std::vector<float> tone(int sampleRate, double seconds, const std::vector<std::pair<double, float>>& partials) {
  const int count = static_cast<int>(sampleRate * seconds);
  std::vector<float> buffer(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    const double time = static_cast<double>(i) / sampleRate;
    float sample = 0;
    for (const auto& partial : partials) sample += partial.second * static_cast<float>(std::sin(2.0 * M_PI * partial.first * time));
    buffer[static_cast<size_t>(i)] = sample;
  }
  return buffer;
}

}  // namespace

int main() {
  const auto dropD = guitartabs::findPreset(6, "Drop D");
  CHECK(dropD.has_value());
  CHECK(dropD->notes.size() == 6);
  CHECK(dropD->notes.front() == 38);
  CHECK(guitartabs::findPreset(6, "Drop A")->notes.front() == 33);
  CHECK(guitartabs::findPreset(6, "Drop C")->notes[1] == 43);
  CHECK(guitartabs::defaultTuning(6)[0] == 40);
  CHECK(guitartabs::defaultTuning(7)[0] == 35);
  CHECK(guitartabs::defaultTuning(8)[0] == 30);

  guitartabs::TabDocument doc = guitartabs::TabDocument::makeDefault();
  CHECK(doc.setCell(0.0, 0, 3));
  CHECK(doc.events.size() == 1);
  CHECK(doc.events[0].frets[0] == 3);
  CHECK(doc.setCell(0.0, 0, 3) == false);
  doc.setStringCount(7);
  CHECK(doc.stringCount == 7);
  CHECK(doc.tuning.size() == 7);
  CHECK(doc.tuning[0] == 35);
  CHECK(doc.events[0].frets[1] == 3);
  doc.setStringCount(6);
  CHECK(doc.events[0].frets[0] == 3);

  doc.setDivision(8);
  CHECK(doc.setCell(1.0, 2, 5));
  CHECK(doc.countInRange(0.0, 2.0) == 2);
  CHECK(doc.clearRange(0.9, 2.0) == 1);
  CHECK(doc.events.size() == 1);

  const std::string saved = guitartabs::stringify(guitartabs::toJson(doc));
  const guitartabs::TabDocument loaded = guitartabs::fromJson(guitartabs::parse(saved));
  CHECK(loaded.stringCount == 6);
  CHECK(loaded.events.size() == 1);
  CHECK(loaded.events[0].frets[0] == 3);
  CHECK(guitartabs::parse("{\"ok\":true,\"n\":1.5}").boolean("ok", false));
  CHECK(std::abs(guitartabs::parse("{\"ok\":true,\"n\":1.5}").number("n", 0) - 1.5) < 1e-9);

  const std::vector<int> standard = guitartabs::defaultTuning(6);
  const std::vector<int> openE = guitartabs::assignFrets({40, 47, 52, 56, 59, 64}, standard, 22);
  const std::vector<int> expected = {0, 2, 2, 1, 0, 0};
  CHECK(openE == expected);
  const std::vector<int> power = guitartabs::assignFrets({38, 45}, guitartabs::findPreset(6, "Drop D")->notes, 22);
  CHECK(power[0] == 0);
  CHECK(power[1] == 0);

  constexpr int kRate = 22050;
  const auto dyad = tone(kRate, 1.2, {{82.4069, 0.45f}, {123.4708, 0.32f}});
  const std::vector<int> dyadNotes = guitartabs::detectMidis(dyad.data(), static_cast<int>(dyad.size()), kRate, 36, 72, 6);
  CHECK(std::find(dyadNotes.begin(), dyadNotes.end(), 40) != dyadNotes.end());
  CHECK(std::find(dyadNotes.begin(), dyadNotes.end(), 47) != dyadNotes.end());

  const auto single = tone(kRate, 1.2, {{82.4069, 0.5f}, {164.8138, 0.22f}, {247.0, 0.12f}});
  const std::vector<int> singleNotes = guitartabs::detectMidis(single.data(), static_cast<int>(single.size()), kRate, 36, 76, 6);
  if (std::find(singleNotes.begin(), singleNotes.end(), 40) == singleNotes.end() ||
      std::find(singleNotes.begin(), singleNotes.end(), 52) != singleNotes.end()) {
    std::cerr << "single-note pitches:";
    for (int note : singleNotes) std::cerr << " " << note;
    std::cerr << "\n";
  }
  CHECK(std::find(singleNotes.begin(), singleNotes.end(), 40) != singleNotes.end());
  CHECK(std::find(singleNotes.begin(), singleNotes.end(), 52) == singleNotes.end());

  guitartabs::TranscribeRequest request;
  request.samples = dyad.data();
  request.sampleCount = static_cast<int>(dyad.size());
  request.sampleRate = kRate;
  request.startTime = 0;
  request.tuning = standard;
  request.cellQn = {0, 0.5};
  request.cellTime = {0, 0.5};
  request.cellDuration = {0.5, 0.5};
  request.cellQnDuration = {0.5, 0.5};
  const auto events = guitartabs::transcribe(request, nullptr);
  CHECK(!events.empty());
  bool voiced = false;
  for (const auto& event : events) {
    if (!event.frets.empty() && event.frets[0] == 0 && event.frets[1] == 2) voiced = true;
  }
  if (!voiced) {
    std::cerr << "transcribed frets:";
    for (const auto& event : events) {
      std::cerr << " [";
      for (int fret : event.frets) std::cerr << fret << " ";
      std::cerr << "]";
    }
    std::cerr << "\n";
  }
  CHECK(voiced);

  if (g_failures) {
    std::cerr << g_failures << " failed\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
