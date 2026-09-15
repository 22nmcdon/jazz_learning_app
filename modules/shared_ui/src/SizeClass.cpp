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

    namespace
    {
        /** First installed family from @p preferred, or JUCE's default.

            The browser is handed a font stack and picks; JUCE is handed one
            name and silently gives you the default if it is missing, which is
            how a chart ends up in the system UI face. Resolving the stack once
            per family keeps the two shells looking alike on a machine that has
            the fonts, without pinning either to a face that may not exist.
        */
        juce::String resolve (juce::StringArray preferred)
        {
            static juce::StringArray installed = [] {
                auto names = juce::Font::findAllTypefaceNames();
                return names;
            }();

            for (const auto& name : preferred)
                if (installed.contains (name))
                    return name;

            return juce::Font::getDefaultSansSerifFontName();
        }
    }

    juce::Font serif (float height, bool bold, bool italic)
    {
        static const auto family = resolve ({ "Playfair Display", "Georgia", "Palatino",
                                              "Palatino Linotype", "P052", "URW Bookman",
                                              "Times New Roman", "Liberation Serif",
                                              "Nimbus Roman", "DejaVu Serif" });

        auto style = bold ? juce::Font::bold : juce::Font::plain;

        if (italic)
            style = static_cast<juce::Font::FontStyleFlags> (style | juce::Font::italic);

        return juce::Font (juce::FontOptions (family, height, style));
    }

    juce::Font sans (float height, bool bold)
    {
        static const auto family = resolve ({ "Jost", "Futura", "Avenir Next", "Segoe UI",
                                              "Helvetica Neue", "Liberation Sans",
                                              "Nimbus Sans", "DejaVu Sans" });

        return juce::Font (juce::FontOptions (family, height, bold ? juce::Font::bold
                                                                   : juce::Font::plain));
    }

    juce::Font hand (float height)
    {
        // The chord symbols on the page are set in a handwriting face, the way
        // a chart is written by hand. Where there is no such face installed a
        // bold serif reads closer to pen than the UI sans does.
        static const auto family = resolve ({ "Kalam", "Bradley Hand", "Segoe Print" });

        if (family == juce::Font::getDefaultSansSerifFontName())
            return serif (height, true);

        return juce::Font (juce::FontOptions (family, height, juce::Font::bold));
    }

    juce::Font mono (float height)
    {
        static const auto family = resolve ({ "Menlo", "Consolas", "SF Mono", "Courier New",
                                              "Liberation Mono", "DejaVu Sans Mono",
                                              "Nimbus Mono PS" });

        return juce::Font (juce::FontOptions (family, height, juce::Font::plain));
    }
}

} // namespace jazz::ui
