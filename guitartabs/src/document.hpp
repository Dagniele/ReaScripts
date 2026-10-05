#pragma once

#include "json.hpp"

#include <optional>
#include <string>
#include <vector>

namespace guitartabs {

struct TabEvent {
  double qn = 0;
  double qnDuration = 0.5;
  std::vector<int> frets;
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
