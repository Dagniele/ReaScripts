#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace guitartabs {

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;

  static Json null();
  static Json boolean(bool value);
  static Json number(double value);
  static Json string(std::string value);
  static Json array();
  static Json object();

  Json& push(Json value);
  Json& set(const std::string& key, Json value);

  const Json* find(std::string_view key) const;
  bool boolean(std::string_view key, bool fallback) const;
  double number(std::string_view key, double fallback) const;
  int integer(std::string_view key, int fallback) const;
  std::string text(std::string_view key, const std::string& fallback = {}) const;
  const std::vector<Json>* array(std::string_view key) const;
};

Json parse(std::string_view text);
std::string stringify(const Json& value);

}  // namespace guitartabs
