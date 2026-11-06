// VJN6 web module (SPEC §5.7, §7.1 R-E1 "Web", §15.2; protocol and exported API: docs/BRIDGE.md
// "Web module API").
//
// The browser main thread owns the Instrument's producer/consumer ends and the JSON bridge
// (vjn6::Bridge). The core renders in a Wasm Audio Worklet that shares this module's memory, at the
// AudioContext's sample rate (no resampler), 128-frame quanta, stereo. Until the worklet renders (before
// the first user gesture, or when no audio output is possible), a main-thread timer drives the core
// with silent render() calls so that commands, LED messages and state keep working (DECISIONS.md
// "Fallback driver"). An atomic flag guarantees that only one thread renders at a time; while the
// worklet is processing it owns rendering.
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/webaudio.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "vjn6/bridge.h"
#include "vjn6/instrument.h"

namespace {

constexpr int kMaxBlock = 128;                 // Web Audio render quantum
constexpr uint32_t kWorkletStack = 256 * 1024; // audio worklet thread stack (multiple of 16)
constexpr double kFallbackPeriodMs = 10.0;     // main-thread driver period
constexpr double kMaxCatchUpS = 0.25;          // at most this much audio per driver tick (throttled timers)
constexpr double kWorkletStallMs = 250.0;      // worklet silent this long -> main thread drives again
constexpr double kDefaultRate = 48000.0;       // only when Web Audio is missing and no rate was given
constexpr const char* kProcessorName = "vjn6";

struct Web {
  std::unique_ptr<vjn6::Instrument> inst;
  std::unique_ptr<vjn6::Bridge> bridge;
  double fs = kDefaultRate;

  // main thread
  std::vector<std::string> outbox;  // replies of vjn6_command / vjn6_load_file, sent with the next poll
  std::string pollText;
  std::vector<uint8_t> stateBytes;
  EMSCRIPTEN_WEBAUDIO_T ctx = 0;
  EMSCRIPTEN_WEBAUDIO_T node = 0;
  bool workletStarting = false;
  bool processorReady = false;
  bool audioFailed = false;
  bool startRequested = false;
  double lastDriveMs = 0.0;
  double pendingSamples = 0.0;
  uint32_t seenQuanta = 0;
  double seenQuantaMs = -1e300;
  float silentL[kMaxBlock] = {};
  float silentR[kMaxBlock] = {};

  // shared with the audio worklet thread
  std::atomic<int> renderLock{0};
  std::atomic<uint32_t> workletQuanta{0};
};

Web g;

bool tryLock() {
  int expected = 0;
  return g.renderLock.compare_exchange_strong(expected, 1, std::memory_order_acquire, std::memory_order_relaxed);
}
void unlock() { g.renderLock.store(0, std::memory_order_release); }

// ---- audio worklet thread -------------------------------------------------------------------------

bool processAudio(int, const AudioSampleFrame*, int numOutputs, AudioSampleFrame* outputs, int, const AudioParamFrame*,
                  void*) {
  if (numOutputs < 1) return true;
  // one output with a fixed stereo channel count (outputChannelCount [2]); planar L then R
  AudioSampleFrame& out = outputs[0];
  const int n = out.samplesPerChannel;
  const int ch = out.numberOfChannels;
  int first = 0;
  if (ch >= 2 && n > 0 && tryLock()) {
    g.inst->render(out.data, out.data + n, n);
    unlock();
    first = 2;
  }
  for (int i = first * n; i < ch * n; ++i) out.data[i] = 0.0f;  // silence if the main thread was rendering
  g.workletQuanta.fetch_add(1, std::memory_order_release);
  return true;
}

// ---- main thread ------------------------------------------------------------------------------------

EM_JS_DEPS(vjn6_web, "$emscriptenRegisterAudioObject,$emscriptenGetAudioObject");

// Creates the AudioContext (suspended until a gesture resumes it). Returns 0 when Web Audio is missing
// or the context cannot be created; falls back to the device rate if the requested rate is refused.
EM_JS(int, vjn6_js_create_context, (double rate), {
  try {
    if (typeof AudioContext === 'undefined') return 0;
    var ctx = null;
    if (rate > 0) {
      try {
        ctx = new AudioContext({latencyHint: 'interactive', sampleRate: rate});
      } catch (e) {
        ctx = null;
      }
    }
    if (!ctx) ctx = new AudioContext({latencyHint: 'interactive'});
    return emscriptenRegisterAudioObject(ctx);
  } catch (e) {
    return 0;
  }
});

// Connects the worklet node to the context's destination; 0 on failure.
EM_JS(int, vjn6_js_connect_node, (int nodeHandle, int ctxHandle), {
  try {
    var node = emscriptenGetAudioObject(nodeHandle);
    var ctx = emscriptenGetAudioObject(ctxHandle);
    if (!node || !ctx) return 0;
    node.connect(ctx.destination);
    return 1;
  } catch (e) {
    return 0;
  }
});

void createNode() {
  if (g.node || !g.processorReady || g.audioFailed) return;
  int outputChannelCounts[1] = {2};
  EmscriptenAudioWorkletNodeCreateOptions opts = {};
  opts.numberOfInputs = 0;
  opts.numberOfOutputs = 1;
  opts.outputChannelCounts = outputChannelCounts;
  opts.channelCount = 2;
  opts.channelCountMode = WEBAUDIO_CHANNEL_COUNT_MODE_EXPLICIT;
  opts.channelInterpretation = WEBAUDIO_CHANNEL_INTERPRETATION_SPEAKERS;
  g.node = emscripten_create_wasm_audio_worklet_node(g.ctx, kProcessorName, &opts, &processAudio, nullptr);
  if (!g.node || !vjn6_js_connect_node(g.node, g.ctx)) {
    g.node = 0;
    g.audioFailed = true;
  }
}

void processorCreated(EMSCRIPTEN_WEBAUDIO_T, bool success, void*) {
  if (!success) {
    g.audioFailed = true;
    return;
  }
  g.processorReady = true;
  if (g.startRequested) createNode();
}

void workletThreadStarted(EMSCRIPTEN_WEBAUDIO_T ctx, bool success, void*) {
  if (!success) {
    g.audioFailed = true;
    return;
  }
  WebAudioWorkletProcessorCreateOptions opts = {};
  opts.name = kProcessorName;
  opts.numAudioParams = 0;
  opts.audioParamDescriptors = nullptr;
  emscripten_create_wasm_audio_worklet_processor_async(ctx, &opts, &processorCreated, nullptr);
}

void startWorkletThread() {
  if (g.workletStarting || !g.ctx) return;
  void* stack = std::aligned_alloc(16, kWorkletStack);
  if (!stack) {
    g.audioFailed = true;
    return;
  }
  g.workletStarting = true;
  emscripten_start_wasm_audio_worklet_thread_async(g.ctx, stack, kWorkletStack, &workletThreadStarted, nullptr);
}

// Main-thread fallback driver: silent renders at wall-clock rate while the worklet does not render.
void driveFallback(void*) {
  const double now = emscripten_get_now();
  const uint32_t q = g.workletQuanta.load(std::memory_order_acquire);
  if (q != g.seenQuanta) {
    g.seenQuanta = q;
    g.seenQuantaMs = now;
  }
  if (g.node && now - g.seenQuantaMs < kWorkletStallMs) {
    // the worklet owns rendering
    g.lastDriveMs = now;
    g.pendingSamples = 0.0;
    return;
  }
  double due = g.pendingSamples + (now - g.lastDriveMs) * 1e-3 * g.fs;
  g.lastDriveMs = now;
  if (due > kMaxCatchUpS * g.fs) due = kMaxCatchUpS * g.fs;
  if (due < 0.0) due = 0.0;
  const int n = static_cast<int>(std::floor(due));
  if (n <= 0 || !tryLock()) {
    g.pendingSamples = due;
    return;
  }
  for (int done = 0; done < n; done += kMaxBlock) {
    const int c = n - done < kMaxBlock ? n - done : kMaxBlock;
    g.inst->render(g.silentL, g.silentR, c);
  }
  unlock();
  g.pendingSamples = due - n;
}

}  // namespace

extern "C" {

// Creates the instrument at the AudioContext rate. sampleRate > 0 is requested from the context;
// the instrument always runs at the rate the context actually has. Returns 1 on success.
EMSCRIPTEN_KEEPALIVE int vjn6_init(double sampleRate) {
  if (g.inst) return 1;
  g.ctx = vjn6_js_create_context(sampleRate);
  if (g.ctx) {
    const int rate = emscripten_audio_context_sample_rate(g.ctx);
    g.fs = rate > 0 ? rate : (sampleRate > 0 ? sampleRate : kDefaultRate);
  } else {
    g.fs = sampleRate > 0 ? sampleRate : kDefaultRate;
    g.audioFailed = true;
  }
  g.inst = std::make_unique<vjn6::Instrument>(g.fs, kMaxBlock);
  g.bridge = std::make_unique<vjn6::Bridge>(*g.inst);
  g.bridge->forwardMidiOut = true;
  g.lastDriveMs = emscripten_get_now();
  emscripten_set_interval(&driveFallback, kFallbackPeriodMs, nullptr);
  // Load the worklet scope now (allowed while the context is suspended); the node is created on the
  // gesture (vjn6_start_audio).
  startWorkletThread();
  return 1;
}

// State blob from IndexedDB; 0 length = first run. Returns 1 if the blob is valid.
EMSCRIPTEN_KEEPALIVE int vjn6_load_state(uint8_t* data, int len) {
  if (!g.inst) return 0;
  vjn6::StateBlob blob;
  if (data && len > 0) blob.bytes.assign(data, data + len);
  return g.inst->loadState(blob) ? 1 : 0;
}

// Serialises the state; returns its length; the bytes are at vjn6_state_ptr() until the next call.
EMSCRIPTEN_KEEPALIVE int vjn6_save_state(void) {
  if (!g.inst) return 0;
  vjn6::StateBlob blob;
  g.inst->saveState(blob);
  g.stateBytes.swap(blob.bytes);
  return static_cast<int>(g.stateBytes.size());
}

EMSCRIPTEN_KEEPALIVE uint8_t* vjn6_state_ptr(void) { return g.stateBytes.empty() ? nullptr : g.stateBytes.data(); }

// One UI -> core message. Replies (exportData, file notices) arrive with the next vjn6_poll.
EMSCRIPTEN_KEEPALIVE void vjn6_command(const char* json) {
  if (!g.bridge || !json) return;
  g.bridge->handleUiMessage(std::string(json), g.outbox);
}

// JSON array of core -> UI message objects; valid until the next call.
EMSCRIPTEN_KEEPALIVE const char* vjn6_poll(void) {
  std::vector<std::string> msgs;
  msgs.swap(g.outbox);
  if (g.bridge) g.bridge->poll(msgs);
  g.pollText.clear();
  g.pollText.push_back('[');
  for (std::size_t i = 0; i < msgs.size(); ++i) {
    if (i) g.pollText.push_back(',');
    g.pollText += msgs[i];
  }
  g.pollText.push_back(']');
  return g.pollText.c_str();
}

// File bytes from the file picker or a drop; notices come back through vjn6_poll.
EMSCRIPTEN_KEEPALIVE void vjn6_load_file(const char* name, uint8_t* data, int len) {
  if (!g.bridge) return;
  const std::string fileName = name ? name : "";
  if (len < 0 || (len > 0 && !data)) {
    g.bridge->fileUnreadable(fileName, g.outbox);
    return;
  }
  g.bridge->loadFile(fileName, data, static_cast<std::size_t>(len), g.outbox);
}

// Called on the first user gesture: resumes the context and creates the audio worklet node.
// Returns 0 when no audio output is possible ("No audio output available.").
EMSCRIPTEN_KEEPALIVE int vjn6_start_audio(void) {
  if (!g.inst || !g.ctx || g.audioFailed) return 0;
  g.startRequested = true;
  startWorkletThread();
  if (g.audioFailed) return 0;
  emscripten_resume_audio_context_sync(g.ctx);
  createNode();  // now if the worklet scope is ready, otherwise as soon as it is
  return g.audioFailed ? 0 : 1;
}

EMSCRIPTEN_KEEPALIVE int vjn6_audio_context(void) { return g.ctx; }

EMSCRIPTEN_KEEPALIVE int vjn6_audio_node(void) { return g.node; }

}  // extern "C"
