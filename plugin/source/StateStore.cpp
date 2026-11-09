#include "StateStore.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>

#include "state/state.h"

namespace vjn6plugin {

namespace {
std::filesystem::path fsPath(const juce::File& f) {
#if JUCE_WINDOWS
  return std::filesystem::path(f.getFullPathName().toWideCharPointer());
#else
  return std::filesystem::path(f.getFullPathName().toStdString());
#endif
}

juce::String errnoText(int err) { return juce::String(std::strerror(err)); }

std::FILE* openForWrite(const juce::File& f) {
#if JUCE_WINDOWS
  return _wfopen(f.getFullPathName().toWideCharPointer(), L"wb");
#else
  return std::fopen(f.getFullPathName().toRawUTF8(), "wb");
#endif
}
}  // namespace

StateStore::StateStore(juce::File directory)
    : dir_(std::move(directory)),
      tmpSuffix_("." + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()) + ".tmp") {}

juce::File StateStore::defaultDirectory() {
  const auto overrideDir = juce::SystemStats::getEnvironmentVariable("VJN6_STATE_DIR", {});
  if (overrideDir.isNotEmpty()) return juce::File(overrideDir);
#if JUCE_MAC
  return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Application Support/VJN6");
#elif JUCE_WINDOWS
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VJN6");
#else
  return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".config/VJN6");
#endif
}

StateStore::Startup StateStore::readStartupState(vjn6::StateBlob& blob) {
  blob.bytes.clear();
  const juce::File f = stateFile();
  if (!f.existsAsFile()) return Startup::FirstRun;

  juce::MemoryBlock data;
  const bool readOk = f.loadFileAsData(data);
  if (readOk && data.getSize() == 0) return Startup::FirstRun;

  bool valid = false;
  if (readOk) {
    blob.bytes.assign(static_cast<const uint8_t*>(data.getData()),
                      static_cast<const uint8_t*>(data.getData()) + data.getSize());
    auto probe = std::make_unique<vjn6::CoreState>();
    vjn6::firstRunState(*probe, true);
    const auto r = vjn6::decodeState(blob.bytes.data(), blob.bytes.size(), *probe);
    valid = r == vjn6::BlobResult::Ok || r == vjn6::BlobResult::OkNewer;
  }
  if (valid) return Startup::Loaded;

  // Unreadable or failing §4.7: keep it as state.bad (replacing an older one), reset with rE.
  std::error_code ec;
  std::filesystem::rename(fsPath(f), fsPath(badFile()), ec);
  if (ec) {
    std::filesystem::remove(fsPath(badFile()), ec);
    std::filesystem::rename(fsPath(f), fsPath(badFile()), ec);
  }
  blob.bytes.assign(1, 0);  // an invalid blob: the core resets to first-run state and shows rE
  return Startup::Reset;
}

juce::String StateStore::writeReplacing(const juce::File& target, const void* data, size_t size) {
  const juce::File tmp = target.getSiblingFile(target.getFileName() + tmpSuffix_);
  std::FILE* fp = openForWrite(tmp);
  if (fp == nullptr) return tmp.getFullPathName() + ": " + errnoText(errno);
  const bool wrote = size == 0 || std::fwrite(data, 1, size, fp) == size;
  const int writeErr = errno;
  const bool closed = std::fclose(fp) == 0;
  const int closeErr = errno;
  std::error_code ec;
  if (!wrote || !closed) {
    std::filesystem::remove(fsPath(tmp), ec);
    return tmp.getFullPathName() + ": " + errnoText(!wrote ? writeErr : closeErr);
  }
  std::filesystem::rename(fsPath(tmp), fsPath(target), ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(fsPath(tmp), ignored);
    return target.getFullPathName() + ": " + juce::String(ec.message());
  }
  return {};
}

juce::String StateStore::save(const vjn6::StateBlob& state, const std::string& bankJson) {
  std::error_code ec;
  std::filesystem::create_directories(fsPath(dir_), ec);
  if (ec || !dir_.isDirectory()) {
    return dir_.getFullPathName() + ": " + (ec ? juce::String(ec.message()) : juce::String("not a folder"));
  }
  auto err = writeReplacing(stateFile(), state.bytes.data(), state.bytes.size());
  if (err.isNotEmpty()) return err;
  return writeReplacing(bankFile(), bankJson.data(), bankJson.size());
}

void StateStore::markDirty(double nowMs) {
  if (!dirty_) {
    dirty_ = true;
    firstDirtyMs_ = nowMs;
  }
  lastDirtyMs_ = nowMs;
}

bool StateStore::saveDue(double nowMs) const {
  return dirty_ && (nowMs - lastDirtyMs_ >= 1000.0 || nowMs - firstDirtyMs_ >= 5000.0);
}

}  // namespace vjn6plugin
