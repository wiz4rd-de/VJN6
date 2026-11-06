// Editor: embedded UI bundle, window constraints and zoom (§4.4, §5.1).
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "PluginEditor.h"
#include "UiResources.h"
#include "helpers.h"

using namespace vjn6test;
using vjn6plugin::VJN6Editor;

TEST_CASE("The UI bundle is embedded and served by the resource provider", "[UI]") {
  CHECK(vjn6plugin::uiResourceCount() > 0);
  const auto index = vjn6plugin::uiResource("/");
  REQUIRE(index.has_value());
  CHECK(index->mimeType == "text/html");
  CHECK(!index->data.empty());
  const auto same = vjn6plugin::uiResource("/index.html?x=1");
  REQUIRE(same.has_value());
  CHECK(same->data == index->data);
  CHECK(!vjn6plugin::uiResource("/no/such/file.js").has_value());
}

TEST_CASE("Every file of the UI bundle is served byte for byte", "[UI]") {
  const juce::File dist(VJN6_UI_DIST);
  int files = 0;
  for (const auto& entry : juce::RangedDirectoryIterator(dist, true, "*", juce::File::findFiles)) {
    const auto f = entry.getFile();
    if (f.getFileName() == ".DS_Store") continue;
    const auto rel = f.getRelativePathFrom(dist).replaceCharacter('\\', '/');
    INFO(rel);
    const auto r = vjn6plugin::uiResource("/" + rel);
    REQUIRE(r.has_value());
    juce::MemoryBlock expected;
    REQUIRE(f.loadFileAsData(expected));
    CHECK(r->data.size() == expected.getSize());
    CHECK(std::memcmp(r->data.data(), expected.getData(), expected.getSize()) == 0);
    if (rel.endsWith(".js")) CHECK(r->mimeType == "text/javascript");
    if (rel.endsWith(".css")) CHECK(r->mimeType == "text/css");
    if (rel.endsWith(".woff2")) CHECK(r->mimeType == "font/woff2");
    ++files;
  }
  CHECK(files == vjn6plugin::uiResourceCount());
}

TEST_CASE("Zoom sizes: percentage x 1400 x 560; Fit picks 100 % or the largest 10 % step that fits", "[UI]") {
  CHECK(VJN6Editor::zoomSize(100) == juce::Point<int>(1400, 560));
  CHECK(VJN6Editor::zoomSize(50) == juce::Point<int>(700, 280));
  CHECK(VJN6Editor::zoomSize(200) == juce::Point<int>(2800, 1120));
  CHECK(VJN6Editor::fitSize({0, 0, 2560, 1440}) == juce::Point<int>(1400, 560));
  CHECK(VJN6Editor::fitSize({0, 0, 1280, 800}) == juce::Point<int>(1120, 448));  // 80 %
  CHECK(VJN6Editor::fitSize({0, 0, 600, 300}) == juce::Point<int>(700, 280));    // never below 50 %
}

TEST_CASE("Editor: fixed aspect constrainer, zoom setting resizes, a manual resize selects Fit, reopens at last size",
          "[UI]") {
  auto p = makeProcessor();
  TestHost host(*p);
  std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorAndMakeActive());
  REQUIRE(editor != nullptr);
  auto* c = editor->getConstrainer();
  REQUIRE(c != nullptr);
  CHECK(c->getFixedAspectRatio() == 2.5);
  CHECK(c->getMinimumWidth() == 700);
  CHECK(c->getMinimumHeight() == 280);
  CHECK(c->getMaximumWidth() == 2800);
  CHECK(c->getMaximumHeight() == 1120);
  CHECK(editor->getWidth() * 2 == editor->getHeight() * 5);

  p->handleUiMessage(R"({"t":"setting","s":7,"v":150})");
  host.run(2);
  CHECK(p->zoomSetting() == 150);
  CHECK(editor->getWidth() == 2100);
  CHECK(editor->getHeight() == 840);

  editor->setSize(1000, 400);  // the user drags the window corner
  host.run(2);
  CHECK(p->zoomSetting() == 0);
  p->editorBeingDeleted(editor.get());  // as the wrapper does
  editor.reset();
  host.run(1);

  std::unique_ptr<juce::AudioProcessorEditor> again(p->createEditorAndMakeActive());
  CHECK(again->getWidth() == 1000);
  CHECK(again->getHeight() == 400);
  p->editorBeingDeleted(again.get());
}

#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>

TEST_CASE("Editor: the embedded UI loads in the WebView and connects", "[UI][webview]") {
  auto p = makeProcessor();
  std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorAndMakeActive());
  editor->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
  editor->setVisible(true);
  const auto deadline = juce::Time::getMillisecondCounter() + 15000;
  while (!p->isUiConnected() && juce::Time::getMillisecondCounter() < deadline)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);  // the message loop (timers, WebKit)
  CHECK(p->isUiConnected());  // the page called vjn6Command({"t":"ready"})
  p->editorBeingDeleted(editor.get());
  editor.reset();
  CHECK(!p->isUiConnected());
}
#endif
