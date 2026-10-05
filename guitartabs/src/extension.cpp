#define REAPERAPI_IMPLEMENT
#include "reaper_api.hpp"
#include "session.hpp"
#include "ui_embed.hpp"

#include <memory>
#include <string>
#include <vector>

namespace guitartabs {

class Extension {
 public:
  static Extension& instance() {
    static Extension ext;
    return ext;
  }

  int load(reaper_plugin_info_t* rec) {
    if (REAPERAPI_LoadAPI(rec->GetFunc) != 0) {
      if (ShowMessageBox) {
        ShowMessageBox("Guitar Tabs could not load the REAPER API it needs.", "Guitar Tabs", 0);
      }
      return 0;
    }
    command_ = rec->Register("custom_action", &action_);
    if (!command_) return 0;
    rec->Register("hookcommand2", reinterpret_cast<void*>(&Extension::hook));
    rec->Register("timer", reinterpret_cast<void*>(&Extension::timer));
    loaded_ = true;
    return 1;
  }

  void unload() {
    sessions_.clear();
    loaded_ = false;
  }

  int commandId() const { return command_; }

  void openSelected() {
    if (!loaded_) return;
    const int selected = CountSelectedTracks(nullptr);
    if (selected <= 0) {
      ShowMessageBox("Select one or more tracks, then run Guitar Tabs again.", "Guitar Tabs", 0);
      return;
    }
    const std::string html = loadHtml();
    if (html.empty()) {
      ShowMessageBox("Guitar Tabs was built without its interface.", "Guitar Tabs", 0);
      return;
    }
    ReaProject* project = EnumProjects(-1, nullptr, 0);
    for (int i = 0; i < selected; ++i) {
      MediaTrack* track = GetSelectedTrack(nullptr, i);
      if (!track) continue;
      char guid[128] = {};
      if (!GetSetMediaTrackInfo_String(track, "GUID", guid, false) || guid[0] == 0) continue;
      if (Session* existing = find(guid)) {
        existing->bringToFront();
        continue;
      }
      sessions_.push_back(std::make_unique<Session>(guid, html, project));
    }
  }

  void onTimer() {
    for (auto& session : sessions_) session->tick();
    std::erase_if(sessions_, [](const std::unique_ptr<Session>& session) { return session->closed(); });
  }

 private:
  static bool hook(KbdSectionInfo*, int command, int, int, int, HWND) {
    if (command != instance().commandId()) return false;
    instance().openSelected();
    return true;
  }

  static void timer() { instance().onTimer(); }

  Session* find(const std::string& guid) {
    for (auto& session : sessions_) {
      if (session->guid() == guid && !session->closed()) return session.get();
    }
    return nullptr;
  }

  static std::string loadHtml() {
    if (kEmbeddedUiSize == 0) return {};
    return std::string(reinterpret_cast<const char*>(kEmbeddedUi), kEmbeddedUiSize);
  }

  custom_action_register_t action_{
      0,
      "DAGNIELE_GUITARTABS_OPEN",
      "Dagniele: Open Guitar Tabs for selected track",
      nullptr,
  };
  int command_ = 0;
  bool loaded_ = false;
  std::vector<std::unique_ptr<Session>> sessions_;
};

}  // namespace guitartabs

extern "C" REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
  if (!rec) {
    guitartabs::Extension::instance().unload();
    return 0;
  }
  if (rec->caller_version != REAPER_PLUGIN_VERSION) return 0;
  return guitartabs::Extension::instance().load(rec);
}
