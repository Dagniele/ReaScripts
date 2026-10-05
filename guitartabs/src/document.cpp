#include "document.hpp"

#include <algorithm>
#include <cmath>

namespace guitartabs {
namespace {

struct PresetRow {
  const char* name;
  int count;
  int notes[8];
};

constexpr PresetRow kPresets[] = {
    {"Standard (DGBE)", 4, {50, 55, 59, 64}},
    {"Bass Standard", 4, {28, 33, 38, 43}},
    {"Bass Drop D", 4, {26, 33, 38, 43}},
    {"Guitar (ADGBE)", 5, {45, 50, 55, 59, 64}},
    {"Bass Standard", 5, {23, 28, 33, 38, 43}},
    {"Bass Tenor", 5, {28, 33, 38, 43, 48}},
    {"Standard E", 6, {40, 45, 50, 55, 59, 64}},
    {"Drop D", 6, {38, 45, 50, 55, 59, 64}},
    {"Drop C", 6, {36, 43, 48, 53, 57, 62}},
    {"Drop B", 6, {35, 42, 47, 52, 56, 61}},
    {"Drop A", 6, {33, 40, 45, 50, 54, 59}},
    {"D Standard", 6, {38, 43, 48, 53, 57, 62}},
    {"C Standard", 6, {36, 41, 46, 51, 55, 60}},
    {"Half Step Down", 6, {39, 44, 49, 54, 58, 63}},
    {"Open G", 6, {38, 43, 50, 55, 59, 62}},
    {"Open D", 6, {38, 45, 50, 54, 57, 62}},
    {"DADGAD", 6, {38, 45, 50, 55, 57, 62}},
    {"Standard", 7, {35, 40, 45, 50, 55, 59, 64}},
    {"Drop A", 7, {33, 40, 45, 50, 55, 59, 64}},
    {"Drop G", 7, {31, 38, 43, 48, 53, 57, 62}},
    {"Standard", 8, {30, 35, 40, 45, 50, 55, 59, 64}},
    {"Drop E", 8, {28, 35, 40, 45, 50, 55, 59, 64}},
};

const char* defaultPresetName(int count) {
  switch (count) {
    case 4:
      return "Standard (DGBE)";
    case 5:
      return "Guitar (ADGBE)";
    case 6:
      return "Standard E";
    default:
      return "Standard";
  }
}

int clampCount(int count) { return std::clamp(count, 4, 8); }

int clampDivision(int division) {
  if (division <= 4) return 4;
  if (division >= 16) return 16;
  return 8;
}

bool eventEmpty(const TabEvent& event) {
  return std::none_of(event.frets.begin(), event.frets.end(), [](int fret) { return fret >= 0; });
}

std::vector<int> rowNotes(const PresetRow& row) {
  return std::vector<int>(row.notes, row.notes + row.count);
}

}  // namespace

double divisionStep(int division) { return 4.0 / clampDivision(division); }

std::vector<Preset> presetsFor(int stringCount) {
  stringCount = clampCount(stringCount);
  std::vector<Preset> out;
  for (const PresetRow& row : kPresets) {
    if (row.count == stringCount) out.push_back(Preset{row.name, rowNotes(row)});
  }
  return out;
}

std::optional<Preset> findPreset(int stringCount, const std::string& name) {
  for (const Preset& preset : presetsFor(stringCount)) {
    if (preset.name == name) return preset;
  }
  return std::nullopt;
}

std::string matchPreset(int stringCount, const std::vector<int>& tuning) {
  for (const Preset& preset : presetsFor(stringCount)) {
    if (preset.notes == tuning) return preset.name;
  }
  return "Custom";
}

std::vector<int> defaultTuning(int stringCount) {
  if (const std::optional<Preset> preset = findPreset(stringCount, defaultPresetName(stringCount))) return preset->notes;
  return {40, 45, 50, 55, 59, 64};
}

TabDocument TabDocument::makeDefault(int count) {
  TabDocument doc;
  doc.stringCount = clampCount(count);
  doc.preset = defaultPresetName(doc.stringCount);
  doc.tuning = defaultTuning(doc.stringCount);
  doc.division = 8;
  return doc;
}

void TabDocument::setStringCount(int count) {
  count = clampCount(count);
  if (count == stringCount) return;
  const std::vector<int> previous = tuning;
  const int old = stringCount;
  const std::vector<int> fallback = defaultTuning(count);
  std::vector<int> next(static_cast<size_t>(count));
  if (count > old) {
    const int add = count - old;
    for (int i = 0; i < add; ++i) next[static_cast<size_t>(i)] = fallback[static_cast<size_t>(i)];
    for (int i = 0; i < old; ++i) next[static_cast<size_t>(add + i)] = previous[static_cast<size_t>(i)];
  } else {
    const int drop = old - count;
    for (int i = 0; i < count; ++i) next[static_cast<size_t>(i)] = previous[static_cast<size_t>(drop + i)];
  }
  tuning = std::move(next);
  stringCount = count;
  preset = matchPreset(stringCount, tuning);

  for (TabEvent& event : events) {
    std::vector<int> frets(static_cast<size_t>(count), -1);
    int attacks = 0;
    if (count > old) {
      const int add = count - old;
      for (int i = 0; i < old && i < static_cast<int>(event.frets.size()); ++i) {
        frets[static_cast<size_t>(add + i)] = event.frets[static_cast<size_t>(i)];
        if (event.attacks & (1 << i)) attacks |= 1 << (add + i);
      }
    } else {
      const int drop = old - count;
      for (int i = 0; i < count; ++i) {
        const int src = drop + i;
        if (src < static_cast<int>(event.frets.size())) frets[static_cast<size_t>(i)] = event.frets[static_cast<size_t>(src)];
        if (src < 16 && (event.attacks & (1 << src))) attacks |= 1 << i;
      }
    }
    event.frets = std::move(frets);
    event.attacks = attacks;
  }
  dropEmpty();
}

bool TabDocument::setPreset(const std::string& name) {
  const std::optional<Preset> preset = findPreset(stringCount, name);
  if (!preset) return false;
  tuning = preset->notes;
  this->preset = preset->name;
  return true;
}

void TabDocument::setTuning(std::vector<int> notes) {
  if (static_cast<int>(notes.size()) != stringCount) return;
  for (int& note : notes) note = std::clamp(note, 16, 96);
  tuning = std::move(notes);
  preset = matchPreset(stringCount, tuning);
}

void TabDocument::setDivision(int value) { division = clampDivision(value); }

bool TabDocument::setCell(double qn, int stringIndex, int fret) {
  if (stringIndex < 0 || stringIndex >= stringCount) return false;
  if (fret < -1 || fret > 24) return false;
  const double step = divisionStep(division);
  const double snapped = std::floor(qn / step + 1e-6) * step;

  for (TabEvent& event : events) {
    if (std::abs(event.qn - snapped) > step * 0.25) continue;
    if (static_cast<int>(event.frets.size()) != stringCount) event.frets.assign(static_cast<size_t>(stringCount), -1);
    const int current = event.frets[static_cast<size_t>(stringIndex)];
    const bool attacked = (event.attacks & (1 << stringIndex)) != 0;
    if (current == fret && (fret < 0 || attacked)) return false;
    event.frets[static_cast<size_t>(stringIndex)] = fret;
    if (fret >= 0) event.attacks |= 1 << stringIndex;
    else event.attacks &= ~(1 << stringIndex);
    dropEmpty();
    return true;
  }

  if (fret < 0) return false;
  TabEvent event;
  event.qn = snapped;
  event.qnDuration = step;
  event.frets.assign(static_cast<size_t>(stringCount), -1);
  event.frets[static_cast<size_t>(stringIndex)] = fret;
  event.attacks = 1 << stringIndex;
  events.push_back(std::move(event));
  sortEvents();
  return true;
}

int TabDocument::countInRange(double qn0, double qn1) const {
  int count = 0;
  for (const TabEvent& event : events) {
    if (event.qn >= qn0 - 1e-4 && event.qn < qn1 - 1e-6) ++count;
  }
  return count;
}

int TabDocument::clearRange(double qn0, double qn1) {
  const auto before = events.size();
  events.erase(std::remove_if(events.begin(), events.end(),
                               [&](const TabEvent& event) {
                                 return event.qn >= qn0 - 1e-4 && event.qn < qn1 - 1e-6;
                               }),
               events.end());
  return static_cast<int>(before - events.size());
}

void TabDocument::replaceRange(double qn0, double qn1, std::vector<TabEvent> fresh) {
  clearRange(qn0, qn1);
  for (TabEvent& event : fresh) {
    if (static_cast<int>(event.frets.size()) != stringCount) {
      event.frets.resize(static_cast<size_t>(stringCount), -1);
    }
    if (!eventEmpty(event)) events.push_back(std::move(event));
  }
  sortEvents();
}

void TabDocument::sortEvents() {
  std::sort(events.begin(), events.end(), [](const TabEvent& a, const TabEvent& b) { return a.qn < b.qn; });
}

void TabDocument::dropEmpty() {
  events.erase(std::remove_if(events.begin(), events.end(), eventEmpty), events.end());
}

Json presetsJson(int stringCount) {
  Json list = Json::array();
  for (const Preset& preset : presetsFor(stringCount)) {
    Json notes = Json::array();
    for (int note : preset.notes) notes.push(Json::number(note));
    Json item = Json::object();
    item.set("name", Json::string(preset.name));
    item.set("notes", std::move(notes));
    list.push(std::move(item));
  }
  return list;
}

Json toJson(const TabDocument& doc) {
  Json tuning = Json::array();
  for (int note : doc.tuning) tuning.push(Json::number(note));

  Json events = Json::array();
  for (const TabEvent& event : doc.events) {
    Json frets = Json::array();
    for (int fret : event.frets) frets.push(Json::number(fret));
    Json item = Json::object();
    item.set("qn", Json::number(event.qn));
    item.set("d", Json::number(event.qnDuration));
    item.set("f", std::move(frets));
    item.set("a", Json::number(event.attacks));
    events.push(std::move(item));
  }

  Json root = Json::object();
  root.set("v", Json::number(doc.version));
  root.set("strings", Json::number(doc.stringCount));
  root.set("preset", Json::string(doc.preset));
  root.set("tuning", std::move(tuning));
  root.set("division", Json::number(doc.division));
  root.set("events", std::move(events));
  return root;
}

TabDocument fromJson(const Json& value) {
  TabDocument doc = TabDocument::makeDefault(value.integer("strings", 6));
  doc.division = value.integer("division", 8);
  doc.setDivision(doc.division);

  if (const std::vector<Json>* tuning = value.array("tuning")) {
    std::vector<int> notes;
    notes.reserve(tuning->size());
    for (const Json& note : *tuning) {
      if (note.type == Json::Type::Number) notes.push_back(static_cast<int>(std::llround(note.num)));
    }
    if (static_cast<int>(notes.size()) == doc.stringCount) doc.setTuning(std::move(notes));
  }
  const std::string preset = value.text("preset");
  if (!preset.empty() && preset != "Custom") doc.setPreset(preset);

  doc.events.clear();
  if (const std::vector<Json>* events = value.array("events")) {
    for (const Json& item : *events) {
      TabEvent event;
      event.qn = item.number("qn", 0);
      event.qnDuration = item.number("d", divisionStep(doc.division));
      event.attacks = item.integer("a", 0);
      event.frets.assign(static_cast<size_t>(doc.stringCount), -1);
      if (const std::vector<Json>* frets = item.array("f")) {
        for (size_t i = 0; i < frets->size() && i < event.frets.size(); ++i) {
          if ((*frets)[i].type == Json::Type::Number) {
            event.frets[i] = std::clamp(static_cast<int>(std::llround((*frets)[i].num)), -1, 24);
          }
        }
      }
      if (!eventEmpty(event)) doc.events.push_back(std::move(event));
    }
  }
  doc.sortEvents();
  return doc;
}

}  // namespace guitartabs
