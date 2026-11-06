#include "UiResources.h"

#include <cstring>
#include <map>

#include "VJN6UiData.h"
#include "VJN6UiPaths.h"

namespace vjn6plugin {

namespace {
struct Entry {
  const char* data;
  int size;
};

// URL path -> embedded file. plugin/CMakeLists.txt stages every file of the bundle as ui<N>.<ext>
// (juce_add_binary_data keeps only file names) and generates the path table VJN6UiPaths.h.
const std::map<juce::String, Entry>& table() {
  static const std::map<juce::String, Entry> t = [] {
    std::map<juce::String, Entry> byStagedName;
    for (int i = 0; i < VJN6UiData::namedResourceListSize; ++i) {
      int size = 0;
      const char* data = VJN6UiData::getNamedResource(VJN6UiData::namedResourceList[i], size);
      const char* original = VJN6UiData::getNamedResourceOriginalFilename(VJN6UiData::namedResourceList[i]);
      if (data != nullptr && original != nullptr) byStagedName[juce::String(original)] = {data, size};
    }
    std::map<juce::String, Entry> m;
    for (const auto& p : kUiPaths) {
      const auto it = byStagedName.find(juce::String(p.staged));
      if (it != byStagedName.end()) m[juce::String(juce::CharPointer_UTF8(p.url))] = it->second;
    }
    return m;
  }();
  return t;
}

const char* mimeFor(const juce::String& path) {
  const auto ext = path.fromLastOccurrenceOf(".", false, false).toLowerCase();
  static const std::map<juce::String, const char*> mimes = {
      {"html", "text/html"},     {"htm", "text/html"},          {"js", "text/javascript"},
      {"mjs", "text/javascript"}, {"css", "text/css"},           {"json", "application/json"},
      {"map", "application/json"}, {"svg", "image/svg+xml"},     {"woff2", "font/woff2"},
      {"woff", "font/woff"},     {"ttf", "font/ttf"},           {"otf", "font/otf"},
      {"wasm", "application/wasm"}, {"txt", "text/plain"},      {"ico", "image/x-icon"},
      {"webmanifest", "application/manifest+json"}};
  const auto it = mimes.find(ext);
  return it != mimes.end() ? it->second : "application/octet-stream";
}
}  // namespace

std::optional<juce::WebBrowserComponent::Resource> uiResource(const juce::String& url) {
  auto path = url.upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false);
  path = juce::URL::removeEscapeChars(path).trimCharactersAtStart("/");
  if (path.isEmpty()) path = "index.html";
  const auto& t = table();
  auto it = t.find(path);
  if (it == t.end()) return std::nullopt;
  juce::WebBrowserComponent::Resource r;
  r.data.resize(static_cast<size_t>(it->second.size));
  if (it->second.size > 0) std::memcpy(r.data.data(), it->second.data, static_cast<size_t>(it->second.size));
  r.mimeType = mimeFor(path);
  return r;
}

int uiResourceCount() { return static_cast<int>(table().size()); }

}  // namespace vjn6plugin
