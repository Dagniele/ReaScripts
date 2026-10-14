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

void fitMarks(TabEvent& event, int count) {
  event.marks.resize(static_cast<size_t>(count), 0);
  event.bends.resize(static_cast<size_t>(count), 0);
}

uint32_t withRivalCleared(uint32_t mask, uint32_t bit) {
  if (bit == kMarkHammer) mask &= ~kMarkPull;
  else if (bit == kMarkPull) mask &= ~kMarkHammer;
  else if (bit == kMarkSlideUp) mask &= ~kMarkSlideDown;
  else if (bit == kMarkSlideDown) mask &= ~kMarkSlideUp;
  else if (bit == kMarkSlideBelow) mask &= ~kMarkSlideAbove;
  else if (bit == kMarkSlideAbove) mask &= ~kMarkSlideBelow;
  else if (bit == kMarkHarmonic) mask &= ~kMarkPinch;
  else if (bit == kMarkPinch) mask &= ~kMarkHarmonic;
  else if (bit == kMarkSlap) mask &= ~kMarkPop;
  else if (bit == kMarkPop) mask &= ~kMarkSlap;
  return mask;
}

bool knownBend(int quarters) {
  return quarters == 0 || quarters == 1 || quarters == 2 || quarters == 4 || quarters == 6 || quarters == 8;
}

TabEvent* eventAt(TabDocument& doc, double qn) {
  const double step = divisionStep(doc.division);
  const double snapped = std::floor(qn / step + 1e-6) * step;
  for (TabEvent& event : doc.events) {
    if (std::abs(event.qn - snapped) <= step * 0.25) return &event;
  }
  return nullptr;
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
    std::vector<uint32_t> marks(static_cast<size_t>(count), 0);
    std::vector<int> bends(static_cast<size_t>(count), 0);
    int attacks = 0;
    auto take = [&](int dest, int src) {
      if (src < 0 || src >= static_cast<int>(event.frets.size())) return;
      frets[static_cast<size_t>(dest)] = event.frets[static_cast<size_t>(src)];
      if (src < static_cast<int>(event.marks.size())) marks[static_cast<size_t>(dest)] = event.marks[static_cast<size_t>(src)];
      if (src < static_cast<int>(event.bends.size())) bends[static_cast<size_t>(dest)] = event.bends[static_cast<size_t>(src)];
      if (src < 16 && (event.attacks & (1 << src))) attacks |= 1 << dest;
    };
    if (count > old) {
      const int add = count - old;
      for (int i = 0; i < old; ++i) take(add + i, i);
    } else {
      const int drop = old - count;
      for (int i = 0; i < count; ++i) take(i, drop + i);
    }
    event.frets = std::move(frets);
    event.marks = std::move(marks);
    event.bends = std::move(bends);
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
    fitMarks(event, stringCount);
    const int current = event.frets[static_cast<size_t>(stringIndex)];
    const bool attacked = (event.attacks & (1 << stringIndex)) != 0;
    if (current == fret && (fret < 0 || attacked)) return false;
    event.frets[static_cast<size_t>(stringIndex)] = fret;
    if (fret >= 0) event.attacks |= 1 << stringIndex;
    else {
      event.attacks &= ~(1 << stringIndex);
      event.marks[static_cast<size_t>(stringIndex)] = 0;
      event.bends[static_cast<size_t>(stringIndex)] = 0;
    }
    dropEmpty();
    return true;
  }

  if (fret < 0) return false;
  TabEvent event;
  event.qn = snapped;
  event.qnDuration = step;
  event.frets.assign(static_cast<size_t>(stringCount), -1);
  event.marks.assign(static_cast<size_t>(stringCount), 0);
  event.bends.assign(static_cast<size_t>(stringCount), 0);
  event.frets[static_cast<size_t>(stringIndex)] = fret;
  event.attacks = 1 << stringIndex;
  events.push_back(std::move(event));
  sortEvents();
  return true;
}

bool TabDocument::setMark(double qn, int stringIndex, uint32_t bit, bool on) {
  if (stringIndex < 0 || stringIndex >= stringCount || bit == 0 || (bit & (bit - 1)) != 0 || bit > kMarkRake) return false;
  TabEvent* event = eventAt(*this, qn);
  if (!event || stringIndex >= static_cast<int>(event->frets.size()) || event->frets[static_cast<size_t>(stringIndex)] < 0) return false;
  fitMarks(*event, stringCount);
  uint32_t mask = event->marks[static_cast<size_t>(stringIndex)];
  if (on) mask = withRivalCleared(mask | bit, bit);
  else mask &= ~bit;
  if (mask == event->marks[static_cast<size_t>(stringIndex)]) return false;
  event->marks[static_cast<size_t>(stringIndex)] = mask;
  return true;
}

bool TabDocument::setBend(double qn, int stringIndex, int quarters) {
  if (stringIndex < 0 || stringIndex >= stringCount || !knownBend(quarters)) return false;
  TabEvent* event = eventAt(*this, qn);
  if (!event || stringIndex >= static_cast<int>(event->frets.size()) || event->frets[static_cast<size_t>(stringIndex)] < 0) return false;
  fitMarks(*event, stringCount);
  if (event->bends[static_cast<size_t>(stringIndex)] == quarters) return false;
  event->bends[static_cast<size_t>(stringIndex)] = quarters;
  return true;
}

bool TabDocument::pasteNotes(const std::vector<PastedNote>& notes) {
  bool changed = false;
  const double step = divisionStep(division);
  for (const PastedNote& note : notes) {
    if (note.stringIndex < 0 || note.stringIndex >= stringCount || note.fret < 0 || note.fret > 24) continue;
    const double snapped = std::max(0.0, std::floor(note.qn / step + 1e-6) * step);
    if (!eventAt(*this, snapped)) {
      TabEvent created;
      created.qn = snapped;
      created.qnDuration = step;
      created.frets.assign(static_cast<size_t>(stringCount), -1);
      created.marks.assign(static_cast<size_t>(stringCount), 0);
      created.bends.assign(static_cast<size_t>(stringCount), 0);
      events.push_back(std::move(created));
      sortEvents();
    }
    TabEvent* event = eventAt(*this, snapped);
    if (!event) continue;
    fitMarks(*event, stringCount);
    const size_t index = static_cast<size_t>(note.stringIndex);
    const uint32_t mark = note.mark & kMarkAll;
    const int bend = knownBend(note.bend) ? note.bend : 0;
    const bool attacked = note.attack;
    const bool sameFret = event->frets[index] == note.fret;
    const bool sameMark = event->marks[index] == mark && event->bends[index] == bend;
    const bool sameAttack = ((event->attacks & (1 << note.stringIndex)) != 0) == attacked;
    if (sameFret && sameMark && sameAttack) continue;
    event->frets[index] = note.fret;
    event->marks[index] = mark;
    event->bends[index] = bend;
    if (attacked) event->attacks |= 1 << note.stringIndex;
    else event->attacks &= ~(1 << note.stringIndex);
    changed = true;
  }
  return changed;
}

bool TabDocument::duplicateNotes(const std::vector<NoteRef>& cells) {
  std::vector<PastedNote> copies;
  double minQn = 0;
  double maxQn = 0;
  for (const NoteRef& cell : cells) {
    if (cell.stringIndex < 0 || cell.stringIndex >= stringCount) continue;
    TabEvent* event = eventAt(*this, cell.qn);
    if (!event || cell.stringIndex >= static_cast<int>(event->frets.size())) continue;
    const int fret = event->frets[static_cast<size_t>(cell.stringIndex)];
    if (fret < 0) continue;
    fitMarks(*event, stringCount);
    if (copies.empty()) {
      minQn = event->qn;
      maxQn = event->qn;
    } else {
      minQn = std::min(minQn, event->qn);
      maxQn = std::max(maxQn, event->qn);
    }
    PastedNote copy;
    copy.qn = event->qn;
    copy.stringIndex = cell.stringIndex;
    copy.fret = fret;
    copy.mark = event->marks[static_cast<size_t>(cell.stringIndex)];
    copy.bend = event->bends[static_cast<size_t>(cell.stringIndex)];
    copy.attack = (event->attacks & (1 << cell.stringIndex)) != 0;
    copies.push_back(copy);
  }
  if (copies.empty()) return false;
  const double offset = (maxQn - minQn) + divisionStep(division);
  for (PastedNote& copy : copies) copy.qn += offset;
  return pasteNotes(copies);
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
    fitMarks(event, stringCount);
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
    Json marks = Json::array();
    Json bends = Json::array();
    for (size_t i = 0; i < event.frets.size(); ++i) {
      frets.push(Json::number(event.frets[i]));
      marks.push(Json::number(i < event.marks.size() ? event.marks[i] : 0));
      bends.push(Json::number(i < event.bends.size() ? event.bends[i] : 0));
    }
    Json item = Json::object();
    item.set("qn", Json::number(event.qn));
    item.set("d", Json::number(event.qnDuration));
    item.set("f", std::move(frets));
    item.set("k", std::move(marks));
    item.set("bn", std::move(bends));
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
      event.marks.assign(static_cast<size_t>(doc.stringCount), 0);
      event.bends.assign(static_cast<size_t>(doc.stringCount), 0);
      if (const std::vector<Json>* frets = item.array("f")) {
        for (size_t i = 0; i < frets->size() && i < event.frets.size(); ++i) {
          if ((*frets)[i].type == Json::Type::Number) {
            event.frets[i] = std::clamp(static_cast<int>(std::llround((*frets)[i].num)), -1, 24);
          }
        }
      }
      if (const std::vector<Json>* marks = item.array("k")) {
        for (size_t i = 0; i < marks->size() && i < event.marks.size(); ++i) {
          if ((*marks)[i].type == Json::Type::Number) event.marks[i] = static_cast<uint32_t>(std::llround((*marks)[i].num)) & kMarkAll;
        }
      }
      if (const std::vector<Json>* bends = item.array("bn")) {
        for (size_t i = 0; i < bends->size() && i < event.bends.size(); ++i) {
          if ((*bends)[i].type != Json::Type::Number) continue;
          const int quarters = static_cast<int>(std::llround((*bends)[i].num));
          event.bends[i] = knownBend(quarters) ? quarters : 0;
        }
      }
      if (!eventEmpty(event)) doc.events.push_back(std::move(event));
    }
  }
  doc.sortEvents();
  return doc;
}

}  // namespace guitartabs
