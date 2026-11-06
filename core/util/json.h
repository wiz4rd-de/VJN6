// Minimal JSON reader shared by the file formats and the UI bridge (objects, arrays, strings,
// integers vs. non-integers, literals). Not for the audio thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace vjn6::json {

struct JValue {
  enum Kind { Null, Bool, Int, Float, String, Array, Object } kind = Null;
  long long i = 0;
  std::string s;
  std::vector<JValue> arr;
  std::vector<std::pair<std::string, JValue>> obj;
  const JValue* get(const char* key) const {
    const JValue* found = nullptr;
    for (const auto& kv : obj)
      if (kv.first == key) found = &kv.second;  // last duplicate wins
    return found;
  }
};

class JParser {
 public:
  JParser(const char* p, std::size_t n) : p_(p), n_(n) {}
  bool parse(JValue& v) {
    skip();
    if (!value(v, 0)) return false;
    skip();
    return i_ == n_;
  }

 private:
  void skip() {
    while (i_ < n_ && (p_[i_] == ' ' || p_[i_] == '\t' || p_[i_] == '\n' || p_[i_] == '\r')) ++i_;
  }
  bool lit(const char* w) {
    const std::size_t l = std::strlen(w);
    if (i_ + l > n_ || std::memcmp(p_ + i_, w, l) != 0) return false;
    i_ += l;
    return true;
  }
  static void putUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) {
      s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  bool hex4(uint32_t& v) {
    if (i_ + 4 > n_) return false;
    v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = p_[i_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
      else return false;
    }
    return true;
  }
  bool string(std::string& out) {
    if (i_ >= n_ || p_[i_] != '"') return false;
    ++i_;
    while (i_ < n_) {
      const char c = p_[i_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return false;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (i_ >= n_) return false;
      const char e = p_[i_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && i_ + 1 < n_ && p_[i_] == '\\' && p_[i_ + 1] == 'u') {
            const std::size_t save = i_;
            i_ += 2;
            uint32_t lo;
            if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              i_ = save;
              cp = 0xFFFD;
            }
          } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD;
          }
          putUtf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool number(JValue& v) {
    const std::size_t start = i_;
    if (i_ < n_ && p_[i_] == '-') ++i_;
    if (i_ >= n_ || p_[i_] < '0' || p_[i_] > '9') return false;
    if (p_[i_] == '0' && i_ + 1 < n_ && p_[i_ + 1] >= '0' && p_[i_ + 1] <= '9') return false;
    while (i_ < n_ && p_[i_] >= '0' && p_[i_] <= '9') ++i_;
    bool isFloat = false;
    if (i_ < n_ && p_[i_] == '.') {
      isFloat = true;
      ++i_;
      if (i_ >= n_ || p_[i_] < '0' || p_[i_] > '9') return false;
      while (i_ < n_ && p_[i_] >= '0' && p_[i_] <= '9') ++i_;
    }
    if (i_ < n_ && (p_[i_] == 'e' || p_[i_] == 'E')) {
      isFloat = true;
      ++i_;
      if (i_ < n_ && (p_[i_] == '+' || p_[i_] == '-')) ++i_;
      if (i_ >= n_ || p_[i_] < '0' || p_[i_] > '9') return false;
      while (i_ < n_ && p_[i_] >= '0' && p_[i_] <= '9') ++i_;
    }
    if (isFloat) {
      v.kind = JValue::Float;
      return true;
    }
    const std::size_t len = i_ - start;
    if (len > 12) {
      v.kind = JValue::Float;  // out of any valid range; treated as not an accepted integer
      return true;
    }
    v.kind = JValue::Int;
    v.i = std::stoll(std::string(p_ + start, len));
    return true;
  }
  bool value(JValue& v, int depth) {
    if (depth > 32 || i_ >= n_) return false;
    const char c = p_[i_];
    if (c == '{') {
      ++i_;
      v.kind = JValue::Object;
      skip();
      if (i_ < n_ && p_[i_] == '}') {
        ++i_;
        return true;
      }
      for (;;) {
        skip();
        std::string key;
        if (!string(key)) return false;
        skip();
        if (i_ >= n_ || p_[i_] != ':') return false;
        ++i_;
        skip();
        JValue child;
        if (!value(child, depth + 1)) return false;
        v.obj.emplace_back(std::move(key), std::move(child));
        skip();
        if (i_ < n_ && p_[i_] == ',') {
          ++i_;
          continue;
        }
        if (i_ < n_ && p_[i_] == '}') {
          ++i_;
          return true;
        }
        return false;
      }
    }
    if (c == '[') {
      ++i_;
      v.kind = JValue::Array;
      skip();
      if (i_ < n_ && p_[i_] == ']') {
        ++i_;
        return true;
      }
      for (;;) {
        skip();
        JValue child;
        if (!value(child, depth + 1)) return false;
        v.arr.push_back(std::move(child));
        skip();
        if (i_ < n_ && p_[i_] == ',') {
          ++i_;
          continue;
        }
        if (i_ < n_ && p_[i_] == ']') {
          ++i_;
          return true;
        }
        return false;
      }
    }
    if (c == '"') {
      v.kind = JValue::String;
      return string(v.s);
    }
    if (lit("true")) {
      v.kind = JValue::Bool;
      v.i = 1;
      return true;
    }
    if (lit("false")) {
      v.kind = JValue::Bool;
      v.i = 0;
      return true;
    }
    if (lit("null")) {
      v.kind = JValue::Null;
      return true;
    }
    return number(v);
  }
  const char* p_;
  std::size_t n_, i_ = 0;
};

// JSON string literal for ASCII/UTF-8 text (escapes quote, backslash and control characters).
inline void appendString(std::string& out, const std::string& s) {
  out.push_back('"');
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(static_cast<char>(c));
    } else if (c < 0x20) {
      static const char* hex = "0123456789abcdef";
      out += "\\u00";
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  out.push_back('"');
}

}  // namespace vjn6::json
