#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jazz::ui
{

/** Width-driven layout class, in the style of iOS size classes.

    Components branch on size class, never on platform: a narrow desktop window
    gets the same compact layout a phone does, which is what makes the single
    responsive UI testable on a laptop.
*/
enum class SizeClass
{
    compact,   ///< phone portrait, or a narrow window
    regular,   ///< tablet, phone landscape, small desktop window
    expanded   ///< full desktop window
};

/** Breakpoints in logical pixels. */
namespace breakpoints
{
    inline constexpr int regular = 600;
    inline constexpr int expanded = 1000;
}

inline SizeClass sizeClassForWidth (int width)
{
    if (width < breakpoints::regular)  return SizeClass::compact;
    if (width < breakpoints::expanded) return SizeClass::regular;
    return SizeClass::expanded;
}

/** How the user is pointing at the UI. Affects presentation - dropdown versus
    bottom sheet, hover versus tap target - but never which components exist.
*/
enum class InteractionMode
{
    pointer,
    touch
};

/** Touch on mobile builds, pointer elsewhere, unless overridden for testing a
    touch layout on a desktop machine.
*/
InteractionMode currentInteractionMode();
void setInteractionModeOverride (InteractionMode mode);
void clearInteractionModeOverride();

/** Minimum comfortable tap target for the current interaction mode. */
int minimumTouchTarget();

/** Shared palette, kept in one place so components stay visually consistent.

    These are the lead-sheet colours the browser shell uses: warm paper rather
    than a dark editor, because the thing on screen is a chart, not a DAW.
*/
namespace theme
{
    inline const juce::Colour cream          { 0xfffaf6f0 };
    inline const juce::Colour creamDeep      { 0xfff2ebe1 };
    inline const juce::Colour paper          { 0xfffffcf7 };
    inline const juce::Colour charcoal       { 0xff241f1d };
    inline const juce::Colour blush          { 0xffd9a6a0 };
    inline const juce::Colour blushDeep      { 0xffc07f79 };
    inline const juce::Colour gold           { 0xffb08d57 };
    inline const juce::Colour line           { 0xffe4d9cc };
    inline const juce::Colour text           { 0xff3a332f };
    inline const juce::Colour textSoft       { 0xff6b615a };
    inline const juce::Colour sage           { 0xff6f7f63 };
    inline const juce::Colour rust           { 0xffa4553f };

    /** The older names the panels were written against, kept so one palette
        change does not mean rewriting every component's colour choices.
    */
    inline const juce::Colour background     = cream;
    inline const juce::Colour surface        = paper;
    inline const juce::Colour surfaceRaised  = creamDeep;
    inline const juce::Colour outline        = line;
    inline const juce::Colour textDim        = textSoft;
    inline const juce::Colour accent         = blushDeep;
    inline const juce::Colour accentMuted    = blush;
    inline const juce::Colour warning        = gold;
    inline const juce::Colour problem        = rust;
    inline const juce::Colour good           = sage;

    /** Font sizes scale up a little on touch, where the screen is nearer. */
    float bodyFontSize();
    float headingFontSize();

    /** The page asks for web fonts by name and lets the browser fall back.
        JUCE has no such chain, so these resolve a preference list against the
        families actually installed and remember the answer.
    */
    juce::Font serif (float height, bool bold = false, bool italic = false);
    juce::Font sans (float height, bool bold = false);
    juce::Font hand (float height);
    juce::Font mono (float height);
}

} // namespace jazz::ui
