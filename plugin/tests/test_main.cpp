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
#endif
  const juce::ScopedJuceInitialiser_GUI juceInit;
  return Catch::Session().run(argc, argv);
}
