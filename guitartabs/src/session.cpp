#include "session.hpp"

#include "reaper_api.hpp"
#include "transcribe.hpp"
#include "webview.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace guitartabs {
namespace {

constexpr int kSampleRate = 22050;
constexpr double kMaxSeconds = 12 * 60;
constexpr double kChunkSeconds = 1.0;
constexpr const char* kExtName = "Dagniele_GuitarTabs";

struct CacheKey {
  std::string value;
};

std::unordered_map<std::string, TabDocument>& cache() {
  static std::unordered_map<std::string, TabDocument> docs;
  return docs;
}

std::string cacheKey(void* project, const std::string& guid) {
  return std::to_string(reinterpret_cast<uintptr_t>(project)) + "|" + guid;
}

std::string trackGuid(MediaTrack* track) {
  char buffer[128] = {};
  if (!GetSetMediaTrackInfo_String(track, "GUID", buffer, false)) return {};
  return buffer;
}

std::string trackTitle(MediaTrack* track) {
  char buffer[512] = {};
  if (!GetSetMediaTrackInfo_String(track, "P_NAME", buffer, false) || buffer[0] == 0) return "Untitled track";
  return buffer;
}

MediaTrack* findTrack(void* project, const std::string& guid) {
  const int count = CountTracks(static_cast<ReaProject*>(project));
  for (int i = 0; i < count; ++i) {
    MediaTrack* track = GetTrack(static_cast<ReaProject*>(project), i);
    if (track && trackGuid(track) == guid) return track;
  }
  return nullptr;
}

TabDocument loadDocument(void* project, const std::string& guid) {
  const std::string key = cacheKey(project, guid);
  if (const auto it = cache().find(key); it != cache().end()) return it->second;

  std::vector<char> buffer(1024 * 1024, 0);
  const int got = GetProjExtState(static_cast<ReaProject*>(project), kExtName, guid.c_str(), buffer.data(),
                                   static_cast<int>(buffer.size()) - 1);
  buffer.back() = 0;
  if (got <= 0 || buffer[0] == 0) return TabDocument::makeDefault();
  try {
    TabDocument doc = fromJson(parse(buffer.data()));
    cache()[key] = doc;
    return doc;
  } catch (const std::exception&) {
    return TabDocument::makeDefault();
  }
}

void saveDocument(void* project, const std::string& guid, const TabDocument& doc) {
  cache()[cacheKey(project, guid)] = doc;
  const std::string json = stringify(toJson(doc));
  SetProjExtState(static_cast<ReaProject*>(project), kExtName, guid.c_str(), json.c_str());
  MarkProjectDirty(static_cast<ReaProject*>(project));
}

bool selectionRange(void* project, double& start, double& end) {
  start = 0;
  end = 0;
  GetSet_LoopTimeRange2(static_cast<ReaProject*>(project), false, false, &start, &end, false);
  return end > start + 1e-4;
}

bool namesMultiSelect(const std::string& action) {
  std::string lower = action;
  for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (lower.find("toggle item selection") != std::string::npos) return true;
  if (lower.find("add item") != std::string::npos && lower.find("selection") != std::string::npos) return true;
  if (lower.find("leaving other items selected") != std::string::npos) return true;
  return false;
}

bool actionSelectsMultiple(const char* action) {
  if (!action || action[0] == 0) return false;
  const std::string text = action;
  if (text.size() >= 2 && text.compare(text.size() - 2, 2, " m") == 0) {
    const int id = std::atoi(text.c_str());
    return id >= 7 && id <= 22;
  }
  if (text.size() >= 2 && text.compare(text.size() - 2, 2, " c") == 0) {
    const int command = std::atoi(text.c_str());
    const char* name = kbd_getTextFromCmd ? kbd_getTextFromCmd(command, nullptr) : nullptr;
    return name && namesMultiSelect(name);
  }
  return namesMultiSelect(text);
}

std::vector<int> multiSelectFlags() {
  std::vector<int> flags;
  if (GetMouseModifier) {
    for (int flag = 1; flag <= 15; ++flag) {
      char action[320] = {};
      GetMouseModifier("MM_CTX_ITEM_CLK", flag, action, static_cast<int>(sizeof(action)));
      if (actionSelectsMultiple(action)) flags.push_back(flag);
    }
  }
  if (flags.empty()) flags.push_back(2);
  return flags;
}

Json measureMap(void* project, const TabDocument& doc) {
  auto* proj = static_cast<ReaProject*>(project);
  double endLimit = GetProjectLength(proj);
  double qn0 = 0;
  double qn1 = 4;
  int num = 4;
  int den = 4;
  double tempo = 120;
  TimeMap_GetMeasureInfo(proj, 0, &qn0, &qn1, &num, &den, &tempo);
  const double barQn = std::max(0.25, qn1 - qn0);
  if (endLimit < 0.25) endLimit = TimeMap2_QNToTime(proj, qn0 + barQn * 16.0);
  if (!doc.events.empty()) {
    const TabEvent& last = doc.events.back();
    endLimit = std::max(endLimit, TimeMap2_QNToTime(proj, last.qn + last.qnDuration));
  }

  Json measures = Json::array();
  double endQn = qn0;
  for (int measure = 0; measure < 4000; ++measure) {
    const double start = TimeMap_GetMeasureInfo(proj, measure, &qn0, &qn1, &num, &den, &tempo);
    if (!std::isfinite(start)) break;
    const double finish = TimeMap2_QNToTime(proj, qn1);
    if (!(finish > start + 1e-6)) break;
    Json item = Json::object();
    item.set("n", Json::number(measure + 1));
    item.set("t0", Json::number(start));
    item.set("t1", Json::number(finish));
    item.set("qn0", Json::number(qn0));
    item.set("qn1", Json::number(qn1));
    item.set("num", Json::number(num));
    item.set("den", Json::number(den));
    item.set("bpm", Json::number(tempo));
    measures.push(std::move(item));
    endQn = qn1;
    if (finish >= endLimit - 1e-4 && measure > 0) break;
  }

  Json root = Json::object();
  root.set("measures", std::move(measures));
  root.set("endQn", Json::number(endQn));
  return root;
}

}  // namespace

struct Session::ResultSlot {
  std::mutex mu;
  bool cancelled = false;
  bool done = false;
  std::atomic<float> progress{0};
  std::vector<TabEvent> events;
  std::string error;
};

Session::Session(std::string guid, std::string html, void* project)
    : guid_(std::move(guid)), project_(project), doc_(loadDocument(project, guid_)) {
  if (MediaTrack* track = findTrack(project_, guid_)) trackName_ = trackTitle(track);
  if (trackName_.empty()) trackName_ = "Untitled track";
  const std::string title = "Guitar Tabs — " + trackName_;
  view_ = WebView::open(
      title, html,
      [this](const std::string& message) {
        try {
          onMessage(message);
        } catch (const std::exception& ex) {
          pushStatus("error", 0, ex.what());
        }
      },
      [this] { closed_ = true; });
  if (!view_) closed_ = true;
}

Session::~Session() {
  view_.reset();
  if (slot_) {
    std::lock_guard lock(slot_->mu);
    slot_->cancelled = true;
  }
  if (worker_.joinable()) worker_.join();
  destroyAccessor();
}

void Session::bringToFront() {
  if (view_) view_->bringToFront();
}

void Session::tick() {
  ReaProject* current = EnumProjects(-1, nullptr, 0);
  if (current != project_) {
    closed_ = true;
    return;
  }
  if (MediaTrack* track = static_cast<MediaTrack*>(resolveTrack())) {
    const std::string name = trackTitle(track);
    if (name != trackName_) {
      trackName_ = name;
      if (view_) view_->setTitle("Guitar Tabs — " + trackName_);
      pushState();
    }
  }
  if (phase_ == Phase::Reading) tickRead();
  else if (phase_ == Phase::Analyzing) tickWorker();
  pushTransport();
  if (++mapTick_ % 30 == 1) pushMap();
}

void* Session::resolveTrack() const { return findTrack(project_, guid_); }

void Session::emit(const char* function, const Json& body) {
  if (!view_) return;
  view_->eval(std::string("window.") + function + "(" + stringify(body) + ")");
}

void Session::pushState() {
  Json state = toJson(doc_);
  state.set("trackName", Json::string(trackName_.empty() ? "Untitled track" : trackName_));
  state.set("trackGuid", Json::string(guid_));
  state.set("presetList", presetsJson(doc_.stringCount));
  state.set("alive", Json::boolean(resolveTrack() != nullptr));
  Json flags = Json::array();
  for (int flag : multiSelectFlags()) flags.push(Json::number(flag));
  state.set("selectFlags", std::move(flags));
  emit("gtApplyState", state);
}

void Session::pushMap() { emit("gtApplyMap", measureMap(project_, doc_)); }

void Session::pushTransport() {
  auto* proj = static_cast<ReaProject*>(project_);
  const int playState = GetPlayStateEx(proj);
  const bool playing = (playState & 1) != 0;
  const bool paused = (playState & 2) != 0;
  const double time = (playing || paused) ? GetPlayPositionEx(proj) : GetCursorPositionEx(proj);
  const double qn = TimeMap2_timeToQN(proj, time);
  double measureStart = 0;
  double measureEnd = 0;
  const int measure = TimeMap_QNToMeasures(proj, qn, &measureStart, &measureEnd);
  int num = 4;
  int den = 4;
  double tempo = 120;
  TimeMap_GetMeasureInfo(proj, std::max(0, measure), &measureStart, &measureEnd, &num, &den, &tempo);

  double sel0 = 0;
  double sel1 = 0;
  const bool hasSel = selectionRange(project_, sel0, sel1);
  Json body = Json::object();
  body.set("playTime", Json::number(time));
  body.set("playQn", Json::number(qn));
  body.set("playing", Json::boolean(playing));
  body.set("paused", Json::boolean(paused));
  body.set("bpm", Json::number(tempo));
  body.set("hasSel", Json::boolean(hasSel));
  body.set("selTime0", Json::number(sel0));
  body.set("selTime1", Json::number(sel1));
  body.set("selQn0", Json::number(hasSel ? TimeMap2_timeToQN(proj, sel0) : 0));
  body.set("selQn1", Json::number(hasSel ? TimeMap2_timeToQN(proj, sel1) : 0));
  emit("gtApplyTransport", body);
}

void Session::pushStatus(const std::string& phase, float progress, const std::string& detail) {
  Json body = Json::object();
  body.set("phase", Json::string(phase));
  body.set("progress", Json::number(progress));
  body.set("detail", Json::string(detail));
  emit("gtApplyStatus", body);
}

void Session::save() const { saveDocument(project_, guid_, doc_); }

void Session::undo() {
  if (undo_.empty()) return;
  redo_.push_back(stringify(toJson(doc_)));
  try {
    doc_ = fromJson(parse(undo_.back()));
  } catch (const std::exception&) {
    redo_.pop_back();
    return;
  }
  undo_.pop_back();
  save();
  pushState();
  pushMap();
}

void Session::redo() {
  if (redo_.empty()) return;
  undo_.push_back(stringify(toJson(doc_)));
  try {
    doc_ = fromJson(parse(redo_.back()));
  } catch (const std::exception&) {
    undo_.pop_back();
    return;
  }
  redo_.pop_back();
  save();
  pushState();
  pushMap();
}

void Session::onMessage(const std::string& text) {
  Json message = parse(text);
  const std::string type = message.text("type");
  if (type == "ready") {
    pushState();
    pushMap();
    pushTransport();
    pushStatus("idle", 0, "");
    return;
  }
  if (type == "undo") {
    undo();
    return;
  }
  if (type == "redo") {
    redo();
    return;
  }
  if (phase_ != Phase::Idle && type != "ready") {
    if (type == "detect") pushStatus(phase_ == Phase::Reading ? "reading" : "detecting", 0, "Detection is already running");
    return;
  }

  const TabDocument before = doc_;
  bool changed = false;
  if (type == "setStringCount") {
    doc_.setStringCount(message.integer("count", doc_.stringCount));
    changed = true;
  } else if (type == "setPreset") {
    changed = doc_.setPreset(message.text("name"));
  } else if (type == "setTuning") {
    std::vector<int> notes;
    if (const std::vector<Json>* list = message.array("notes")) {
      for (const Json& note : *list) {
        if (note.type == Json::Type::Number) notes.push_back(static_cast<int>(std::llround(note.num)));
      }
    }
    doc_.setTuning(std::move(notes));
    changed = true;
  } else if (type == "setDivision") {
    doc_.setDivision(message.integer("division", doc_.division));
    changed = doc_.division != before.division;
  } else if (type == "setCell") {
    changed = doc_.setCell(message.number("qn", 0), message.integer("string", -1), message.integer("fret", -1));
  } else if (type == "clearCell") {
    changed = doc_.setCell(message.number("qn", 0), message.integer("string", -1), -1);
  } else if (type == "clearCells") {
    if (const std::vector<Json>* cells = message.array("cells")) {
      for (const Json& cell : *cells) {
        if (doc_.setCell(cell.number("qn", 0), cell.integer("string", -1), -1)) changed = true;
      }
    }
  } else if (type == "clearRange") {
    auto* proj = static_cast<ReaProject*>(project_);
    double start = 0;
    double end = 0;
    double q0 = 0;
    double q1 = 0;
    if (selectionRange(project_, start, end)) {
      q0 = TimeMap2_timeToQN(proj, start);
      q1 = TimeMap2_timeToQN(proj, end);
    } else if (!doc_.events.empty()) {
      q0 = doc_.events.front().qn;
      q1 = doc_.events.back().qn + doc_.events.back().qnDuration;
    }
    changed = doc_.clearRange(q0, q1) > 0;
  } else if (type == "detect") {
    beginDetect(message.boolean("overwrite", false));
    return;
  }

  if (!changed) return;
  if (stringify(toJson(before)) == stringify(toJson(doc_))) return;
  undo_.push_back(stringify(toJson(before)));
  redo_.clear();
  if (undo_.size() > 60) undo_.erase(undo_.begin());
  save();
  pushState();
  if (type == "setStringCount" || type == "clearRange") pushMap();
}

void Session::beginDetect(bool overwrite) {
  MediaTrack* track = static_cast<MediaTrack*>(resolveTrack());
  if (!track) {
    pushStatus("error", 0, "This track is no longer in the project");
    return;
  }
  auto* proj = static_cast<ReaProject*>(project_);
  double sel0 = 0;
  double sel1 = 0;
  const bool hasSel = selectionRange(project_, sel0, sel1);
  double time0 = hasSel ? sel0 : 0;
  double time1 = hasSel ? sel1 : GetProjectLength(proj);
  if (time1 - time0 < 0.05) {
    pushStatus("error", 0, "There is no audio range to detect");
    return;
  }

  const double step = divisionStep(doc_.division);
  double q0 = TimeMap2_timeToQN(proj, time0);
  double q1 = TimeMap2_timeToQN(proj, time1);
  if (!(q1 > q0 + 1e-4)) {
    pushStatus("error", 0, "There is no audio range to detect");
    return;
  }
  const double qStart = std::floor(q0 / step + 1e-8) * step;
  double qEnd = std::ceil(q1 / step - 1e-8) * step;
  if (!(qEnd > qStart)) qEnd = qStart + step;

  double audio0 = TimeMap2_QNToTime(proj, qStart);
  double audio1 = TimeMap2_QNToTime(proj, qEnd);
  truncated_ = false;
  if (audio1 - audio0 > kMaxSeconds) {
    audio1 = audio0 + kMaxSeconds;
    qEnd = TimeMap2_timeToQN(proj, audio1);
    truncated_ = true;
  }

  if (doc_.countInRange(qStart, qEnd) > 0 && !overwrite) {
    Json body = Json::object();
    body.set("count", Json::number(doc_.countInRange(qStart, qEnd)));
    body.set("qn0", Json::number(qStart));
    body.set("qn1", Json::number(qEnd));
    emit("gtConfirmOverwrite", body);
    return;
  }

  cellQn_.clear();
  cellTime_.clear();
  cellDuration_.clear();
  cellQnDuration_.clear();
  const int cellCount = std::min(20000, static_cast<int>(std::llround((qEnd - qStart) / step)));
  cellQn_.reserve(static_cast<size_t>(std::max(0, cellCount)));
  for (int i = 0; i < cellCount; ++i) {
    const double q = qStart + step * i;
    const double next = (i + 1 == cellCount) ? qEnd : q + step;
    const double t = TimeMap2_QNToTime(proj, q);
    const double tNext = TimeMap2_QNToTime(proj, next);
    if (!(tNext > t)) continue;
    cellQn_.push_back(q);
    cellTime_.push_back(t);
    cellDuration_.push_back(tNext - t);
    cellQnDuration_.push_back(std::max(1e-4, next - q));
  }
  if (cellQn_.empty()) {
    pushStatus("error", 0, "Could not build a beat grid for this range");
    return;
  }

  int channels = static_cast<int>(std::lround(GetMediaTrackInfo_Value(track, "I_NCHAN")));
  if (channels < 1) channels = 2;
  if (channels > 16) channels = 16;
  destroyAccessor();
  accessor_ = CreateTrackAudioAccessor(track);
  if (!accessor_) {
    pushStatus("error", 0, "Could not read audio from this track");
    return;
  }
  channels_ = channels;
  readStart_ = audio0;
  readEnd_ = audio1;
  detectQn0_ = qStart;
  detectQn1_ = qEnd;
  mono_.clear();
  mono_.reserve(static_cast<size_t>((audio1 - audio0) * kSampleRate) + 8);
  phase_ = Phase::Reading;
  pushStatus("reading", 0.02, truncated_ ? "Reading the first 12 minutes" : "Reading track audio");
}

void Session::destroyAccessor() {
  if (!accessor_) return;
  DestroyAudioAccessor(static_cast<AudioAccessor*>(accessor_));
  accessor_ = nullptr;
}

void Session::tickRead() {
  if (!accessor_) {
    phase_ = Phase::Idle;
    return;
  }
  const double remain = readEnd_ - (readStart_ + static_cast<double>(mono_.size()) / kSampleRate);
  if (remain <= 0.001) {
    startWorker();
    return;
  }
  const int frames = std::max(1, static_cast<int>(std::min(kChunkSeconds, remain) * kSampleRate));
  const double position = readStart_ + static_cast<double>(mono_.size()) / kSampleRate;
  std::vector<double> buffer(static_cast<size_t>(frames) * static_cast<size_t>(channels_), 0.0);
  const int result = GetAudioAccessorSamples(static_cast<AudioAccessor*>(accessor_), kSampleRate, channels_, position, frames, buffer.data());
  if (result < 0) {
    destroyAccessor();
    phase_ = Phase::Idle;
    pushStatus("error", 0, "Reading audio failed");
    return;
  }
  for (int i = 0; i < frames; ++i) {
    double sample = 0;
    for (int channel = 0; channel < channels_; ++channel) {
      sample += buffer[static_cast<size_t>(i) * static_cast<size_t>(channels_) + static_cast<size_t>(channel)];
    }
    mono_.push_back(static_cast<float>(sample / channels_));
  }
  const double span = std::max(0.001, readEnd_ - readStart_);
  const double done = static_cast<double>(mono_.size()) / kSampleRate;
  pushStatus("reading", static_cast<float>(std::clamp(done / span, 0.0, 1.0) * 0.55), "Reading track audio");
  if (done + 0.001 >= readEnd_ - readStart_) startWorker();
}

void Session::startWorker() {
  destroyAccessor();
  auto audio = std::make_shared<std::vector<float>>(std::move(mono_));
  mono_.clear();
  float peak = 0;
  for (float sample : *audio) peak = std::max(peak, std::abs(sample));
  if (audio->empty() || peak < 1e-5f) {
    phase_ = Phase::Idle;
    pushStatus("empty", 0, "No audio found on this track in that range");
    return;
  }

  if (worker_.joinable()) worker_.join();
  slot_ = std::make_shared<ResultSlot>();
  auto slot = slot_;
  TranscribeRequest request;
  request.sampleRate = kSampleRate;
  request.startTime = readStart_;
  request.cellQn = cellQn_;
  request.cellTime = cellTime_;
  request.cellDuration = cellDuration_;
  request.cellQnDuration = cellQnDuration_;
  request.tuning = doc_.tuning;
  phase_ = Phase::Analyzing;
  worker_ = std::thread([audio, slot, request]() mutable {
    request.samples = audio->data();
    request.sampleCount = static_cast<int>(audio->size());
    try {
      auto events = transcribe(request, &slot->progress);
      std::lock_guard lock(slot->mu);
      if (!slot->cancelled) slot->events = std::move(events);
    } catch (const std::exception& ex) {
      std::lock_guard lock(slot->mu);
      slot->error = ex.what();
    }
    std::lock_guard lock(slot->mu);
    slot->done = true;
  });
  pushStatus("detecting", 0.55, "Detecting pitches");
}

void Session::tickWorker() {
  if (!slot_) {
    phase_ = Phase::Idle;
    return;
  }
  const float progress = slot_->progress.load(std::memory_order_relaxed);
  pushStatus("detecting", 0.55f + 0.45f * progress, "Detecting pitches");
  bool done = false;
  {
    std::lock_guard lock(slot_->mu);
    done = slot_->done;
  }
  if (!done) return;
  if (worker_.joinable()) worker_.join();

  std::vector<TabEvent> events;
  std::string error;
  {
    std::lock_guard lock(slot_->mu);
    events = std::move(slot_->events);
    error = slot_->error;
  }
  slot_.reset();
  phase_ = Phase::Idle;
  if (!error.empty()) {
    pushStatus("error", 0, error);
    return;
  }

  const TabDocument before = doc_;
  doc_.replaceRange(detectQn0_, detectQn1_, std::move(events));
  undo_.push_back(stringify(toJson(before)));
  redo_.clear();
  save();
  pushState();
  int inRange = 0;
  for (const TabEvent& event : doc_.events) {
    if (event.qn < detectQn0_ - 1e-4 || event.qn >= detectQn1_ - 1e-6) continue;
    inRange += static_cast<int>(std::count_if(event.frets.begin(), event.frets.end(), [](int fret) { return fret >= 0; }));
  }
  if (inRange == 0) pushStatus("empty", 1, "No pitched notes found");
  else pushStatus("idle", 1, std::to_string(inRange) + (inRange == 1 ? " note placed" : " notes placed"));
}

}  // namespace guitartabs
