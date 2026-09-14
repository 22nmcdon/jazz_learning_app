#include "jazz/ui/SizeClass.h"

#include <optional>

namespace jazz::ui
{

namespace
{
    std::optional<InteractionMode> modeOverride;
}

InteractionMode currentInteractionMode()
{
    if (modeOverride.has_value())
        return *modeOverride;

   #if JUCE_IOS || JUCE_ANDROID
    return InteractionMode::touch;
   #else
    // A touchscreen laptop reports touch as soon as one is used.
    return juce::Desktop::getInstance().getMainMouseSource().isTouch() ? InteractionMode::touch
                                                                       : InteractionMode::pointer;
   #endif
}

void setInteractionModeOverride (InteractionMode mode) { modeOverride = mode; }
void clearInteractionModeOverride()                    { modeOverride.reset(); }

int minimumTouchTarget()
{
    return currentInteractionMode() == InteractionMode::touch ? 44 : 28;
}

namespace theme
{
    float bodyFontSize()
    {
        return currentInteractionMode() == InteractionMode::touch ? 16.0f : 14.0f;
    }

    float headingFontSize()
    {
        return currentInteractionMode() == InteractionMode::touch ? 21.0f : 18.0f;
    }
}

} // namespace jazz::ui
