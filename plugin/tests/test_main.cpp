// Catch2 entry point with a JUCE message manager (parameters, timers and the editor need one).
#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

#if JUCE_LINUX || JUCE_BSD
// JUCE's WebKitGTK browser runs in a child process that re-executes this binary (as JUCE's own
// application main does).
extern "C" int juce_gtkWebkitMain(int argc, const char* const* argv);
#endif

int main(int argc, char* argv[]) {
#if JUCE_LINUX || JUCE_BSD
  if (argc >= 2 && juce::String(argv[1]) == "--juce-gtkwebkitfork-child") return juce_gtkWebkitMain(argc, argv);
  // The plugin code is built with JUCE_USE_EXTERNAL_TEMPORARY_SUBPROCESS: outside a standalone app JUCE
  // starts the WebKit child through juce_linux_subprocess_helper, which dlopen()s this executable. glibc
  // refuses to load a PIE that way, the helper exits and JUCE's next pipe write kills the tests (SIGPIPE).
  // A registered (never called) application factory makes JUCE execv this binary instead (handled above).
  // VJN6 itself tells standalone from plugin by wrapperType, not by isStandaloneApp().
  juce::JUCEApplicationBase::createInstance = []() -> juce::JUCEApplicationBase* { return nullptr; };
#endif
  const juce::ScopedJuceInitialiser_GUI juceInit;
  return Catch::Session().run(argc, argv);
}
