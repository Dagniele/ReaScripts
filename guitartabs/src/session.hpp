#pragma once

#include "document.hpp"

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace guitartabs {

class Session {
 public:
  Session(std::string guid, std::string html, void* project);
  ~Session();

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  const std::string& guid() const { return guid_; }
  bool closed() const { return closed_; }
  void tick();
  void bringToFront();

 private:
  struct ResultSlot;

  void onMessage(const std::string& text);
  void emit(const char* function, const Json& body);
  void pushState();
  void pushMap();
  void pushTransport();
  void pushStatus(const std::string& phase, float progress, const std::string& detail);
  void save() const;
  void undo();
  void redo();
  void beginDetect(bool overwrite);
  void destroyAccessor();
  void tickRead();
  void startWorker();
  void tickWorker();
  void* resolveTrack() const;
  void* project() const { return project_; }

  std::string guid_;
  void* project_ = nullptr;
  TabDocument doc_;
  std::unique_ptr<class WebView> view_;
  bool closed_ = false;
  std::string trackName_;
  std::vector<std::string> undo_;
  std::vector<std::string> redo_;

  enum class Phase { Idle, Reading, Analyzing };
  Phase phase_ = Phase::Idle;
  void* accessor_ = nullptr;
  int channels_ = 2;
  double readStart_ = 0;
  double readEnd_ = 0;
  std::vector<float> mono_;
  std::vector<double> cellQn_;
  std::vector<double> cellTime_;
  std::vector<double> cellDuration_;
  std::vector<double> cellQnDuration_;
  double detectQn0_ = 0;
  double detectQn1_ = 0;
  bool truncated_ = false;
  std::shared_ptr<ResultSlot> slot_;
  std::thread worker_;
  int mapTick_ = 0;
};

}  // namespace guitartabs
