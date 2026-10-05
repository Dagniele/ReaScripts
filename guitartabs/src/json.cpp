#include "json.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace guitartabs {
namespace {

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  Json parseValue() {
    skip();
    if (pos_ >= text_.size()) throw std::runtime_error("Unexpected end of JSON");
    const char c = text_[pos_];
    if (c == 'n') return literal("null", Json::null());
    if (c == 't') return literal("true", Json::boolean(true));
    if (c == 'f') return literal("false", Json::boolean(false));
    if (c == '"') return Json::string(parseString());
    if (c == '[') return parseArray();
    if (c == '{') return parseObject();
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return Json::number(parseNumber());
    throw std::runtime_error("Invalid JSON value");
  }

  void finish() {
    skip();
    if (pos_ != text_.size()) throw std::runtime_error("Trailing data in JSON");
  }

 private:
  Json literal(std::string_view word, Json value) {
    if (text_.substr(pos_, word.size()) != word) throw std::runtime_error("Invalid JSON literal");
    pos_ += word.size();
    return value;
  }

  Json parseArray() {
    ++pos_;
    Json out = Json::array();
    skip();
    if (consume(']')) return out;
    while (true) {
      out.push(parseValue());
      skip();
      if (consume(']')) return out;
      if (!consume(',')) throw std::runtime_error("Expected ',' in array");
    }
  }

  Json parseObject() {
    ++pos_;
    Json out = Json::object();
    skip();
    if (consume('}')) return out;
    while (true) {
      skip();
      if (pos_ >= text_.size() || text_[pos_] != '"') throw std::runtime_error("Expected object key");
      std::string key = parseString();
      skip();
      if (!consume(':')) throw std::runtime_error("Expected ':'");
      out.set(std::move(key), parseValue());
      skip();
      if (consume('}')) return out;
      if (!consume(',')) throw std::runtime_error("Expected ',' in object");
    }
  }

  std::string parseString() {
    ++pos_;
    std::string out;
    while (pos_ < text_.size()) {
      char c = text_[pos_++];
      if (c == '"') return out;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) throw std::runtime_error("Bad string escape");
      char e = text_[pos_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out.push_back(e);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          if (pos_ + 4 > text_.size()) throw std::runtime_error("Bad unicode escape");
          int code = 0;
          for (int i = 0; i < 4; ++i) {
            char h = text_[pos_++];
            code <<= 4;
            if (h >= '0' && h <= '9') code += h - '0';
            else if (h >= 'a' && h <= 'f') code += h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') code += h - 'A' + 10;
            else throw std::runtime_error("Bad unicode escape");
          }
          appendUtf8(out, code);
          break;
        }
        default:
          throw std::runtime_error("Bad string escape");
      }
    }
    throw std::runtime_error("Unterminated string");
  }

  double parseNumber() {
    const char* begin = text_.data() + pos_;
    const char* end = text_.data() + text_.size();
    double value = 0;
    auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{}) throw std::runtime_error("Invalid number");
    pos_ = static_cast<size_t>(result.ptr - text_.data());
    return value;
  }

  void skip() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
  }

  bool consume(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  static void appendUtf8(std::string& out, int code) {
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  std::string_view text_;
  size_t pos_ = 0;
};

void writeString(std::string& out, const std::string& value) {
  out.push_back('"');
  for (unsigned char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

void writeNumber(std::string& out, double value) {
  if (!std::isfinite(value)) {
    out += "0";
    return;
  }
  if (std::floor(value) == value && std::abs(value) < 1e15) {
    out += std::to_string(static_cast<long long>(value));
    return;
  }
  char buf[64];
  auto result = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::general, 10);
  if (result.ec != std::errc{}) {
    out += "0";
    return;
  }
  out.append(buf, result.ptr);
}

void writeJson(std::string& out, const Json& value) {
  switch (value.type) {
    case Json::Type::Null:
      out += "null";
      break;
    case Json::Type::Bool:
      out += value.b ? "true" : "false";
      break;
    case Json::Type::Number:
      writeNumber(out, value.num);
      break;
    case Json::Type::String:
      writeString(out, value.str);
      break;
    case Json::Type::Array: {
      out.push_back('[');
      for (size_t i = 0; i < value.arr.size(); ++i) {
        if (i) out.push_back(',');
        writeJson(out, value.arr[i]);
      }
      out.push_back(']');
      break;
    }
    case Json::Type::Object: {
      out.push_back('{');
      for (size_t i = 0; i < value.obj.size(); ++i) {
        if (i) out.push_back(',');
        writeString(out, value.obj[i].first);
        out.push_back(':');
        writeJson(out, value.obj[i].second);
      }
      out.push_back('}');
      break;
    }
  }
}

}  // namespace

Json Json::null() { return {}; }

Json Json::boolean(bool value) {
  Json j;
  j.type = Type::Bool;
  j.b = value;
  return j;
}

Json Json::number(double value) {
  Json j;
  j.type = Type::Number;
  j.num = value;
  return j;
}

Json Json::string(std::string value) {
  Json j;
  j.type = Type::String;
  j.str = std::move(value);
  return j;
}

Json Json::array() {
  Json j;
  j.type = Type::Array;
  return j;
}

Json Json::object() {
  Json j;
  j.type = Type::Object;
  return j;
}

Json& Json::push(Json value) {
  type = Type::Array;
  arr.push_back(std::move(value));
  return *this;
}

Json& Json::set(const std::string& key, Json value) {
  type = Type::Object;
  for (auto& entry : obj) {
    if (entry.first == key) {
      entry.second = std::move(value);
      return *this;
    }
  }
  obj.emplace_back(key, std::move(value));
  return *this;
}

const Json* Json::find(std::string_view key) const {
  for (const auto& entry : obj) {
    if (entry.first == key) return &entry.second;
  }
  return nullptr;
}

bool Json::boolean(std::string_view key, bool fallback) const {
  const Json* v = find(key);
  if (!v || v->type != Type::Bool) return fallback;
  return v->b;
}

double Json::number(std::string_view key, double fallback) const {
  const Json* v = find(key);
  if (!v || v->type != Type::Number) return fallback;
  return v->num;
}

int Json::integer(std::string_view key, int fallback) const {
  const Json* v = find(key);
  if (!v || v->type != Type::Number) return fallback;
  return static_cast<int>(std::llround(v->num));
}

std::string Json::text(std::string_view key, const std::string& fallback) const {
  const Json* v = find(key);
  if (!v || v->type != Type::String) return fallback;
  return v->str;
}

const std::vector<Json>* Json::array(std::string_view key) const {
  const Json* v = find(key);
  if (!v || v->type != Type::Array) return nullptr;
  return &v->arr;
}

Json parse(std::string_view text) {
  Parser parser(text);
  Json value = parser.parseValue();
  parser.finish();
  return value;
}

std::string stringify(const Json& value) {
  std::string out;
  out.reserve(256);
  writeJson(out, value);
  return out;
}

}  // namespace guitartabs
