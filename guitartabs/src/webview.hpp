#pragma once

#include <functional>
#include <memory>
#include <string>

namespace guitartabs {

class WebView {
 public:
  virtual ~WebView() = default;
  virtual void setTitle(const std::string& title) = 0;
  virtual void eval(const std::string& js) = 0;
  virtual void bringToFront() = 0;

  static std::unique_ptr<WebView> open(const std::string& title, const std::string& html,
                                        std::function<void(const std::string&)> onMessage,
                                        std::function<void()> onClose);
};

}  // namespace guitartabs
