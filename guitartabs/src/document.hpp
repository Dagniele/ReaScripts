#pragma once

#include "json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace guitartabs {

constexpr uint32_t kMarkHammer = 1u << 0;
constexpr uint32_t kMarkPull = 1u << 1;
constexpr uint32_t kMarkSlideUp = 1u << 2;
constexpr uint32_t kMarkSlideDown = 1u << 3;
constexpr uint32_t kMarkSlideBelow = 1u << 4;
constexpr uint32_t kMarkSlideAbove = 1u << 5;
constexpr uint32_t kMarkVibrato = 1u << 6;
constexpr uint32_t kMarkPalm = 1u << 7;
constexpr uint32_t kMarkHarmonic = 1u << 8;
constexpr uint32_t kMarkPinch = 1u << 9;
constexpr uint32_t kMarkDead = 1u << 10;
constexpr uint32_t kMarkTap = 1u << 11;
constexpr uint32_t kMarkGhost = 1u << 12;
constexpr uint32_t kMarkLetRing = 1u << 13;
constexpr uint32_t kMarkStaccato = 1u << 14;
constexpr uint32_t kMarkAccent = 1u << 15;
constexpr uint32_t kMarkTremolo = 1u << 16;
constexpr uint32_t kMarkTrill = 1u << 17;
constexpr uint32_t kMarkSlap = 1u << 18;
constexpr uint32_t kMarkPop = 1u << 19;
constexpr uint32_t kMarkPreBend = 1u << 20;
constexpr uint32_t kMarkRelease = 1u << 21;
constexpr uint32_t kMarkRake = 1u << 22;
constexpr uint32_t kMarkAll = (kMarkRake << 1) - 1;

struct TabEvent {
  double qn = 0;
  double qnDuration = 0.5;
  std::vector<int> frets;
  std::vector<uint32_t> marks;
  std::vector<int> bends;
  int attacks = 0;
};

struct Preset {
  std::string name;
  std::vector<int> notes;
};

struct TabDocument {
  int version = 1;
  int stringCount = 6;
  std::string preset = "Standard E";
  std::vector<int> tuning;
  int division = 8;
  std::vector<TabEvent> events;

  static TabDocument makeDefault(int stringCount = 6);

  void setStringCount(int count);
  bool setPreset(const std::string& name);
  void setTuning(std::vector<int> notes);
  void setDivision(int division);
  bool setCell(double qn, int stringIndex, int fret);
  bool setMark(double qn, int stringIndex, uint32_t bit, bool on);
  bool setBend(double qn, int stringIndex, int quarters);
  int countInRange(double qn0, double qn1) const;
  int clearRange(double qn0, double qn1);
  void replaceRange(double qn0, double qn1, std::vector<TabEvent> fresh);
  void sortEvents();
  void dropEmpty();
};

std::vector<Preset> presetsFor(int stringCount);
std::optional<Preset> findPreset(int stringCount, const std::string& name);
std::string matchPreset(int stringCount, const std::vector<int>& tuning);
std::vector<int> defaultTuning(int stringCount);
double divisionStep(int division);

Json toJson(const TabDocument& doc);
TabDocument fromJson(const Json& value);
Json presetsJson(int stringCount);

}  // namespace guitartabs
