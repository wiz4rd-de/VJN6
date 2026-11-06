#include "vjn6/bridge.h"

#include <cstring>
#include <memory>

#include "files/bulk.h"
#include "util/json.h"
#include "vjn6/files.h"

namespace vjn6 {

namespace {
using json::JParser;
using json::JValue;

const char* kButtons[] = {"range", "pulse", "saw", "sub", "chorusOff", "chorusI", "chorusII", "portamento",
                          "hold", "arpOn", "bank", "group", "patch", "manual", "write", "poly1", "poly2",
                          "keyTranspose", "lfoTrig"};
constexpr int kNumButtons = sizeof(kButtons) / sizeof(kButtons[0]);

int buttonIndex(const std::string& s) {
  for (int i = 0; i < kNumButtons; ++i)
    if (s == kButtons[i]) return i;
  return -1;
}

long long getInt(const JValue& o, const char* key, long long dflt = 0) {
  const JValue* v = o.get(key);
  return (v && v->kind == JValue::Int) ? v->i : dflt;
}
bool getBool(const JValue& o, const char* key, bool dflt = false) {
  const JValue* v = o.get(key);
  if (!v) return dflt;
  if (v->kind == JValue::Bool || v->kind == JValue::Int) return v->i != 0;
  return dflt;
}
std::string getStr(const JValue& o, const char* key) {
  const JValue* v = o.get(key);
  return (v && v->kind == JValue::String) ? v->s : std::string();
}
Source getSrc(const JValue& o, Source dflt) {
  const std::string s = getStr(o, "src");
  if (s == "panel") return Source::Panel;
  if (s == "overlay") return Source::Overlay;
  if (s == "keyboard") return Source::Keyboard;
  return dflt;
}
const char* noticeKind(NoticeKind k) {
  switch (k) {
    case NoticeKind::Loaded: return "loaded";
    case NoticeKind::BankLoaded: return "bankLoaded";
    case NoticeKind::Imported: return "imported";
    case NoticeKind::Protected: return "protected";
    case NoticeKind::Malformed: return "malformed";
    case NoticeKind::Unreadable: return "unreadable";
    case NoticeKind::Renamed: return "renamed";
  }
  return "";
}
void appendIntArray(std::string& o, const int16_t* v, int n) {
  o.push_back('[');
  for (int i = 0; i < n; ++i) {
    if (i) o.push_back(',');
    o += std::to_string(v[i]);
  }
  o.push_back(']');
}
void appendPatch(std::string& o, const Patch& p) {
  o.push_back('[');
  for (int i = 0; i < kNumFields; ++i) {
    if (i) o.push_back(',');
    o += std::to_string(p.v[i]);
  }
  o.push_back(']');
}
void appendBits(std::string& o, uint64_t bits) {
  o.push_back('[');
  bool first = true;
  for (int i = 0; i < L_COUNT; ++i) {
    if (!(bits & (1ull << i))) continue;
    if (!first) o.push_back(',');
    first = false;
    o += std::to_string(i);
  }
  o.push_back(']');
}
}  // namespace

static bool parseObject(const std::string& s, JValue& v) {
  JParser p(s.data(), s.size());
  if (!p.parse(v) || v.kind != JValue::Object) return false;
  return true;
}

Bridge::Bridge(Instrument& inst) : inst_(inst) {}

std::string Bridge::base64(const uint8_t* d, std::size_t n) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  o.reserve((n + 2) / 3 * 4);
  for (std::size_t i = 0; i < n; i += 3) {
    const uint32_t a = d[i], b = i + 1 < n ? d[i + 1] : 0, c = i + 2 < n ? d[i + 2] : 0;
    const uint32_t x = (a << 16) | (b << 8) | c;
    o.push_back(t[(x >> 18) & 63]);
    o.push_back(t[(x >> 12) & 63]);
    o.push_back(i + 1 < n ? t[(x >> 6) & 63] : '=');
    o.push_back(i + 2 < n ? t[x & 63] : '=');
  }
  return o;
}

bool Bridge::unbase64(const std::string& s, std::vector<uint8_t>& out) {
  out.clear();
  uint32_t acc = 0;
  int bits = 0;
  for (char ch : s) {
    int v;
    if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
    else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
    else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
    else if (ch == '+' || ch == '-') v = 62;
    else if (ch == '/' || ch == '_') v = 63;
    else if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ') continue;
    else return false;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
    }
  }
  return true;
}

std::string Bridge::notice(const std::string& kind, const std::string& file, int i, int j) {
  std::string o = "{\"t\":\"notice\",\"kind\":";
  json::appendString(o, kind);
  o += ",\"file\":";
  json::appendString(o, file);
  o += ",\"i\":" + std::to_string(i) + ",\"j\":" + std::to_string(j) + "}";
  return o;
}

std::string Bridge::status(const std::string& text) {
  std::string o = "{\"t\":\"status\",\"text\":";
  json::appendString(o, text);
  return o + "}";
}

std::string Bridge::snapshotJson(const Snapshot& s, bool withBank) {
  std::string o;
  o.reserve(withBank ? 16384 : 1024);
  o += "{\"t\":\"state\",\"seq\":" + std::to_string(s.seq);
  o += ",\"mode\":" + std::to_string(s.settings.v[S_MODE]);
  o += ",\"edit\":";
  appendPatch(o, s.edit);
  o += ",\"name\":";
  json::appendString(o, s.editName.s);
  o += ",\"stored\":";
  appendPatch(o, s.stored);
  o += std::string(",\"edited\":") + (s.edited ? "true" : "false");
  o += std::string(",\"manual\":") + (s.manual ? "true" : "false");
  o += ",\"slot\":" + std::to_string(s.currentSlot);
  o += ",\"pendingBank\":" + std::to_string(s.pendingBank);
  o += ",\"pendingGroup\":" + std::to_string(s.pendingGroup);
  o += ",\"perf\":";
  appendIntArray(o, s.perf.v, P_COUNT);
  o += ",\"settings\":";
  appendIntArray(o, s.settings.v, S_COUNT);
  o += ",\"led\":{\"text\":";
  json::appendString(o, std::string(s.led.text, 2));
  o += std::string(",\"dp\":") + (s.led.dp ? "true" : "false");
  o += std::string(",\"blink\":") + (s.led.blink ? "true" : "false");
  o += ",\"message\":" + std::to_string(static_cast<int>(s.led.message)) + "}";
  o += ",\"lit\":";
  appendBits(o, s.buttonLeds);
  o += ",\"blinking\":";
  appendBits(o, s.buttonBlink);
  o += std::string(",\"writeArmed\":") + (s.writeArmed ? "true" : "false");
  o += std::string(",\"ktArmed\":") + (s.ktArmed ? "true" : "false");
  o += std::string(",\"lfoTrig\":") + (s.lfoTrigHeld ? "true" : "false");
  o += std::string(",\"push\":") + (s.push ? "true" : "false");
  o += ",\"lever\":" + std::to_string(static_cast<int>(s.lever * 8191.0f + (s.lever >= 0 ? 0.5f : -0.5f)));
  o += ",\"latency\":" + std::to_string(s.latency);
  o += ",\"bankVersion\":" + std::to_string(s.bankVersion);
  if (withBank) {
    o += ",\"bank\":[";
    for (int i = 0; i < kNumSlots; ++i) {
      if (i) o.push_back(',');
      o += "{\"n\":";
      json::appendString(o, s.bank[i].name.s);
      o += ",\"p\":";
      appendPatch(o, s.bank[i].patch);
      o.push_back('}');
    }
    o.push_back(']');
  }
  o.push_back('}');
  return o;
}

bool Bridge::commandFromJson(const std::string& text, Command& c) {
  JValue v;
  if (!parseObject(text, v)) return false;
  const std::string t = getStr(v, "t");
  c = Command{};
  if (t == "field") {
    c = Command::setField(static_cast<int>(getInt(v, "f")), static_cast<int>(getInt(v, "v")), getSrc(v, Source::Panel));
    return c.a < kNumFields;
  }
  if (t == "perf") {
    c = Command::setPerf(static_cast<int>(getInt(v, "p")), static_cast<int>(getInt(v, "v")), getSrc(v, Source::Panel));
    return c.a < P_COUNT;
  }
  if (t == "setting") {
    c = Command::setSetting(static_cast<int>(getInt(v, "s")), static_cast<int>(getInt(v, "v")), getSrc(v, Source::Overlay));
    return c.a < S_COUNT;
  }
  if (t == "press" || t == "release") {
    const int b = buttonIndex(getStr(v, "b"));
    if (b < 0) return false;
    if (t == "press") c = Command::press(static_cast<Button>(b), static_cast<int>(getInt(v, "i")), getBool(v, "shift"), getSrc(v, Source::Panel));
    else c = Command::release(static_cast<Button>(b), getSrc(v, Source::Panel));
    return true;
  }
  if (t == "lever") {
    c.type = CommandType::Lever;
    c.src = getSrc(v, Source::Panel);
    c.f = static_cast<float>(static_cast<double>(getInt(v, "v")) / 8191.0);
    c.flags = static_cast<uint8_t>(getBool(v, "drag") ? 1 : 0);
    if (v.get("push")) c.flags = static_cast<uint8_t>(c.flags | 4 | (getBool(v, "push") ? 2 : 0));
    return true;
  }
  if (t == "push" || t == "pedal") {
    c.type = t == "push" ? CommandType::Push : CommandType::Pedal;
    c.src = Source::Keyboard;
    c.b = getBool(v, "on") ? 1 : 0;
    return true;
  }
  if (t == "note") {
    c = Command::note(static_cast<int>(getInt(v, "n")) & 127, static_cast<int>(getInt(v, "vel")) & 127, Source::Keyboard);
    return true;
  }
  if (t == "midiIn") {
    c.type = CommandType::MidiIn;
    c.src = Source::System;
    c.port = static_cast<uint8_t>(getInt(v, "port") & 15);
    const JValue* bytes = v.get("bytes");
    if (!bytes || bytes->kind != JValue::Array || bytes->arr.size() > 24) return false;
    c.len = static_cast<uint8_t>(bytes->arr.size());
    for (std::size_t i = 0; i < bytes->arr.size(); ++i) c.data[i] = static_cast<uint8_t>(bytes->arr[i].i & 0xFF);
    return true;
  }
  if (t == "selectSlot") {
    c = Command::selectSlot(static_cast<int>(getInt(v, "slot")), getSrc(v, Source::Overlay));
    return true;
  }
  if (t == "rename") {
    c.type = CommandType::Rename;
    c.src = Source::Overlay;
    c.i = static_cast<int32_t>(getInt(v, "slot"));
    const std::string n = files::normaliseName(getStr(v, "name"));
    c.len = static_cast<uint8_t>(n.size());
    std::memcpy(c.data, n.data(), n.size());
    return true;
  }
  if (t == "copySlot") {
    c.type = CommandType::CopySlot;
    c.src = Source::Overlay;
    c.i = static_cast<int32_t>(getInt(v, "from"));
    c.b2 = static_cast<uint8_t>(getInt(v, "to") - 1);
    return true;
  }
  if (t == "slotToEdit") {
    c.type = CommandType::SlotToEdit;
    c.src = Source::Overlay;
    c.i = static_cast<int32_t>(getInt(v, "slot"));
    return true;
  }
  if (t == "sendPatch") {
    c.type = CommandType::SendPatch;
    c.src = Source::Overlay;
    return true;
  }
  if (t == "releaseAll") {
    c.type = CommandType::ReleaseAll;
    c.src = Source::System;
    return true;
  }
  if (t == "message") {
    c.type = CommandType::ShowMessage;
    c.src = Source::System;
    c.a = static_cast<uint8_t>(getInt(v, "m"));
    return true;
  }
  return false;
}

bool Bridge::handleUiMessage(const std::string& text, std::vector<std::string>& out) {
  JValue v;
  if (!parseObject(text, v)) return false;
  const std::string t = getStr(v, "t");
  if (t == "ready") {
    full_ = true;
    return true;
  }
  if (t == "loadFile") {
    std::vector<uint8_t> bytes;
    if (!unbase64(getStr(v, "b64"), bytes)) {
      fileUnreadable(getStr(v, "name"), out);
      return true;
    }
    loadFile(getStr(v, "name"), bytes.data(), bytes.size(), out);
    return true;
  }
  if (t == "export") {
    const std::string what = getStr(v, "what");
    std::string name;
    std::vector<uint8_t> bytes;
    if (exportData(what, name, bytes)) {
      std::string o = "{\"t\":\"exportData\",\"what\":";
      json::appendString(o, what);
      o += ",\"name\":";
      json::appendString(o, name);
      o += ",\"b64\":\"" + base64(bytes.data(), bytes.size()) + "\"}";
      out.push_back(std::move(o));
    }
    return true;
  }
  Command c;
  if (!commandFromJson(text, c)) return false;
  inst_.post(c);
  return true;
}

void Bridge::fileUnreadable(const std::string& fileName, std::vector<std::string>& out) {
  Command c;
  c.type = CommandType::ShowMessage;
  c.src = Source::System;
  c.a = static_cast<uint8_t>(LedMessage::Format);
  inst_.post(c);
  out.push_back(notice("unreadable", fileName));
}

void Bridge::loadFile(const std::string& fileName, const uint8_t* data, std::size_t len, std::vector<std::string>& out) {
  lastFile_ = fileName;
  const int idx = inst_.acquireBulk();
  if (idx < 0) {
    fileUnreadable(fileName, out);
    return;
  }
  Bulk* b = inst_.bulk(idx);
  const files::Result r = files::parse(data, len, fileName, *b);
  if (r == files::Result::Malformed) {
    b->busy.store(0);
    Command c;
    c.type = CommandType::ShowMessage;
    c.src = Source::System;
    c.a = static_cast<uint8_t>(LedMessage::Format);
    inst_.post(c);
    out.push_back(notice("malformed", fileName));
    return;
  }
  Command c;
  c.type = CommandType::LoadBulk;
  c.src = Source::Overlay;
  c.i = idx;
  if (!inst_.post(c)) {
    b->busy.store(0);
    fileUnreadable(fileName, out);
  }
}

bool Bridge::exportData(const std::string& what, std::string& fileName, std::vector<uint8_t>& bytes) {
  auto s = std::make_unique<Snapshot>();
  inst_.snapshot(*s);
  if (what == "bankJson") {
    const std::string t = files::bankJson(s->bank);
    bytes.assign(t.begin(), t.end());
    fileName = files::bankFileNameJson();
  } else if (what == "bankSyx") {
    bytes = files::bankSyx(s->bank);
    fileName = files::bankFileNameSyx();
  } else if (what == "patchJson") {
    const std::string t = files::patchJson(s->edit, s->editName);
    bytes.assign(t.begin(), t.end());
    fileName = files::patchFileName(s->editName, ".vjn6");
  } else if (what == "patchSyx") {
    bytes = files::patchSyx(s->edit);
    fileName = files::patchFileName(s->editName, ".syx");
  } else {
    return false;
  }
  return true;
}

void Bridge::poll(std::vector<std::string>& out) {
  Event e;
  while (inst_.pollEvent(e)) {
    if (onEvent) onEvent(e);
    if (e.type == EventType::Notice) {
      out.push_back(notice(noticeKind(static_cast<NoticeKind>(e.a)), lastFile_, e.i, e.j));
    } else if (e.type == EventType::MidiOut && forwardMidiOut) {
      std::string o = "{\"t\":\"midiOut\",\"bytes\":[";
      for (int i = 0; i < e.len; ++i) {
        if (i) o.push_back(',');
        o += std::to_string(e.data[i]);
      }
      out.push_back(o + "]}");
    }
  }
  auto s = std::make_unique<Snapshot>();
  inst_.snapshot(*s);
  if (full_ || s->seq != lastSeq_) {
    const bool withBank = full_ || s->bankVersion != lastBank_;
    out.push_back(snapshotJson(*s, withBank));
    lastSeq_ = s->seq;
    lastBank_ = s->bankVersion;
    full_ = false;
  }
}

}  // namespace vjn6
