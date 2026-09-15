#pragma once

#include "jazz/ui/SizeClass.h"

#include <functional>

namespace jazz::ui
{

/** A panel that covers the app until it is dismissed - the page's `<dialog>`.

    Built as a child component rather than a window: modal loops are off in this
    build (they behave badly on mobile), and an in-app overlay is also what a
    touch layout wants, where a floating window has nowhere to float.

    Subclasses fill @ref contentArea and lay their own children out in
    @ref contentResized.
*/
class OverlayPanel : public juce::Component
{
public:
    explicit OverlayPanel (juce::String panelTitle);

    void setPanelTitle (juce::String newTitle);

    /** Shows the panel over @p parent, sized to it. */
    void showOver (juce::Component& parent);
    void dismiss();

    /** Called after the panel closes, however it was closed. */
    std::function<void()> onDismissed;

    void paint (juce::Graphics& g) override;
    void resized() override final;
    void mouseDown (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

protected:
    /** The card's inside, below the title row. */
    juce::Rectangle<int> contentArea() const;

    virtual void contentResized (juce::Rectangle<int> area) = 0;
    virtual void paintContent (juce::Graphics&, juce::Rectangle<int>) {}

    /** Card width as a fraction of the parent, capped so a wide desktop window
        does not stretch a list of chords across a metre of screen.
    */
    virtual int preferredCardWidth (int availableWidth) const;
    virtual int preferredCardHeight (int availableHeight) const;

private:
    juce::Rectangle<int> cardBounds() const;

    juce::String title;
    juce::TextButton closeButton { "x" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverlayPanel)
};

/** The page's link buttons: small caps, letter-spaced, underlined in blush.

    JUCE draws a button as a filled rounded rect by default, which is the one
    thing the lead sheet has none of.
*/
class LinkButton : public juce::Button
{
public:
    explicit LinkButton (const juce::String& buttonText);

    /** Draws dimmed and ignores clicks, for a control that has nothing to act on. */
    void setMuted (bool shouldBeMuted);

    /** Draws as the one you are on - used for the tabs, where the point is to
        say which view you are looking at rather than which you cannot click.
    */
    void setSelected (bool shouldBeSelected);

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    /** Width this button needs for its text. */
    int preferredWidth() const;

private:
    bool muted { false };
    bool selected { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkButton)
};

/** The page's filled button - charcoal, small caps - for the one or two actions
    that are doing rather than navigating.
*/
class SolidButton : public juce::Button
{
public:
    explicit SolidButton (const juce::String& buttonText);

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    int preferredWidth() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolidButton)
};

/** Dresses a viewport's scrollbars in the lead-sheet palette.

    Setting the colour on the viewport itself does nothing - the scrollbars are
    separate components - which is how one stays JUCE blue on a cream page.
*/
void styleViewport (juce::Viewport& viewport);

/** Small-caps section label, letter-spaced, as the page sets its headings. */
void drawEyebrow (juce::Graphics& g,
                  const juce::String& text,
                  juce::Rectangle<int> area,
                  juce::Justification justification,
                  juce::Colour colour,
                  float height = 11.0f);

/** Letter-spaced text, which JUCE has no direct support for. */
void drawTracked (juce::Graphics& g,
                  const juce::String& text,
                  juce::Rectangle<int> area,
                  juce::Justification justification,
                  float tracking);

/** Width @ref drawTracked needs for @p text in the current font. */
float trackedWidth (const juce::Font& font, const juce::String& text, float tracking);

} // namespace jazz::ui
