// The built UI (ui/dist) embedded with juce_add_binary_data and served to the WebBrowserComponent.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

namespace vjn6plugin {

// url: the path the resource provider receives ("/" or "/assets/x.js", optionally with ?query).
std::optional<juce::WebBrowserComponent::Resource> uiResource(const juce::String& url);

// Number of embedded files (tests).
int uiResourceCount();

}  // namespace vjn6plugin
