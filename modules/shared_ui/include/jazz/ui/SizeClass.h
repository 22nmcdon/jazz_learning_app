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

/** Shared palette, kept in one place so components stay visually consistent. */
namespace theme
{
    inline const juce::Colour background     { 0xff16161a };
    inline const juce::Colour surface        { 0xff21212a };
    inline const juce::Colour surfaceRaised  { 0xff2c2c38 };
    inline const juce::Colour outline        { 0xff3a3a48 };
    inline const juce::Colour text           { 0xfff2f2f7 };
    inline const juce::Colour textDim        { 0xff9a9aa8 };
    inline const juce::Colour accent         { 0xff4dd0c0 };
    inline const juce::Colour accentMuted    { 0xff2f6f68 };
    inline const juce::Colour warning        { 0xffe8a13a };
    inline const juce::Colour problem        { 0xffe5605f };
    inline const juce::Colour good           { 0xff6fcf7f };

    /** Font sizes scale up a little on touch, where the screen is nearer. */
    float bodyFontSize();
    float headingFontSize();
}

} // namespace jazz::ui
