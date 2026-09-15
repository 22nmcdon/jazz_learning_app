#include "jazz/ui/Overlay.h"

namespace jazz::ui
{

using namespace juce;

namespace
{
    constexpr int titleRowHeight = 44;
    constexpr int cardPadding = 20;
}

float trackedWidth (const Font& font, const String& text, float tracking)
{
    if (text.isEmpty())
        return 0.0f;

    return GlyphArrangement::getStringWidth (font, text)
           + tracking * static_cast<float> (text.length() - 1);
}

void drawTracked (Graphics& g,
                  const String& text,
                  Rectangle<int> area,
                  Justification justification,
                  float tracking)
{
    const auto font = g.getCurrentFont();
    const auto total = trackedWidth (font, text, tracking);

    auto x = static_cast<float> (area.getX());

    if (justification.testFlags (Justification::horizontallyCentred))
        x = static_cast<float> (area.getCentreX()) - total * 0.5f;
    else if (justification.testFlags (Justification::right))
        x = static_cast<float> (area.getRight()) - total;

    auto y = static_cast<float> (area.getCentreY()) + font.getHeight() * 0.32f;

    if (justification.testFlags (Justification::top))
        y = static_cast<float> (area.getY()) + font.getHeight();

    for (int i = 0; i < text.length(); ++i)
    {
        const auto glyph = String::charToString (text[i]);
        g.drawSingleLineText (glyph, roundToInt (x), roundToInt (y));
        x += GlyphArrangement::getStringWidth (font, glyph) + tracking;
    }
}

void styleViewport (Viewport& viewport)
{
    for (auto* bar : { &viewport.getVerticalScrollBar(), &viewport.getHorizontalScrollBar() })
    {
        bar->setColour (ScrollBar::thumbColourId, theme::blush);
        bar->setColour (ScrollBar::trackColourId, theme::cream);
        bar->setColour (ScrollBar::backgroundColourId, Colours::transparentBlack);
    }
}

void drawEyebrow (Graphics& g,
                  const String& text,
                  Rectangle<int> area,
                  Justification justification,
                  Colour colour,
                  float height)
{
    g.setColour (colour);
    g.setFont (theme::sans (height));
    drawTracked (g, text.toUpperCase(), area, justification, height * 0.18f);
}

//==============================================================================
LinkButton::LinkButton (const String& buttonText) : Button (buttonText)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void LinkButton::setMuted (bool shouldBeMuted)
{
    if (muted == shouldBeMuted)
        return;

    muted = shouldBeMuted;
    setEnabled (! shouldBeMuted);
    repaint();
}

int LinkButton::preferredWidth() const
{
    const auto font = theme::sans (11.0f);
    return roundToInt (trackedWidth (font, getButtonText().toUpperCase(), 11.0f * 0.16f)) + 6;
}

void LinkButton::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void LinkButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    const auto active = (highlighted || down) && ! muted;

    // Selected wins over muted: a tab you are on is the one to read, not the
    // one to ignore.
    const auto colour = selected ? theme::charcoal
                      : muted    ? theme::textSoft.withAlpha (0.45f)
                      : active   ? theme::blushDeep
                                 : theme::charcoal;

    g.setColour (colour);
    g.setFont (theme::sans (11.0f));

    auto area = getLocalBounds();
    drawTracked (g, getButtonText().toUpperCase(), area.withTrimmedBottom (4),
                 Justification::centredLeft, 11.0f * 0.16f);

    // The underline is the button: there is no fill and no border.
    g.setColour (selected ? theme::blushDeep
               : muted    ? theme::line
               : active   ? theme::blushDeep
                          : theme::blush);

    g.fillRect (area.getX(), area.getBottom() - 3,
                jmin (preferredWidth(), area.getWidth()), selected ? 2 : 1);
}

//==============================================================================
SolidButton::SolidButton (const String& buttonText) : Button (buttonText)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

int SolidButton::preferredWidth() const
{
    const auto font = theme::sans (10.5f);
    return roundToInt (trackedWidth (font, getButtonText().toUpperCase(), 10.5f * 0.14f)) + 30;
}

void SolidButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    const auto enabled = isEnabled();
    auto fill = theme::charcoal;

    if (! enabled)      fill = theme::charcoal.withAlpha (0.35f);
    else if (down)      fill = theme::blushDeep.darker (0.2f);
    else if (highlighted) fill = theme::blushDeep;

    g.setColour (fill);
    g.fillRect (getLocalBounds());

    g.setColour (enabled ? theme::cream : theme::cream.withAlpha (0.6f));
    g.setFont (theme::sans (10.5f));
    drawTracked (g, getButtonText().toUpperCase(), getLocalBounds(),
                 Justification::centred, 10.5f * 0.14f);
}

//==============================================================================
OverlayPanel::OverlayPanel (String panelTitle) : title (std::move (panelTitle))
{
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    addAndMakeVisible (closeButton);
    closeButton.setButtonText (String (CharPointer_UTF8 ("\xc3\x97")));
    closeButton.setColour (TextButton::buttonColourId, Colours::transparentBlack);
    closeButton.setColour (TextButton::buttonOnColourId, Colours::transparentBlack);
    closeButton.setColour (TextButton::textColourOffId, theme::textSoft);
    closeButton.setColour (TextButton::textColourOnId, theme::blushDeep);
    closeButton.onClick = [this] { dismiss(); };
}

void OverlayPanel::setPanelTitle (String newTitle)
{
    title = std::move (newTitle);
    repaint();
}

void OverlayPanel::showOver (Component& parent)
{
    if (getParentComponent() != &parent)
        parent.addAndMakeVisible (this);
    else
        setVisible (true);

    toFront (true);
    setBounds (parent.getLocalBounds());
    grabKeyboardFocus();
}

void OverlayPanel::dismiss()
{
    setVisible (false);

    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();

    if (onDismissed != nullptr)
        onDismissed();
}

int OverlayPanel::preferredCardWidth (int availableWidth) const
{
    return jmin (640, availableWidth - 48);
}

int OverlayPanel::preferredCardHeight (int availableHeight) const
{
    return jmin (560, availableHeight - 48);
}

Rectangle<int> OverlayPanel::cardBounds() const
{
    const auto width = jmax (200, preferredCardWidth (getWidth()));
    const auto height = jmax (160, preferredCardHeight (getHeight()));

    return Rectangle<int> (width, height).withCentre (getLocalBounds().getCentre());
}

Rectangle<int> OverlayPanel::contentArea() const
{
    return cardBounds().reduced (cardPadding).withTrimmedTop (titleRowHeight - cardPadding + 8);
}

void OverlayPanel::paint (Graphics& g)
{
    // A scrim, so the chart behind reads as out of reach rather than merely
    // behind something.
    g.fillAll (theme::charcoal.withAlpha (0.34f));

    const auto card = cardBounds();

    g.setColour (theme::paper);
    g.fillRect (card);
    g.setColour (theme::line);
    g.drawRect (card, 1);

    auto titleRow = card.reduced (cardPadding, 0).withHeight (titleRowHeight);
    drawEyebrow (g, title, titleRow, Justification::centredLeft, theme::textSoft, 11.0f);

    g.setColour (theme::line);
    g.fillRect (card.getX() + cardPadding, titleRow.getBottom(),
                card.getWidth() - cardPadding * 2, 1);

    paintContent (g, contentArea());
}

void OverlayPanel::resized()
{
    const auto card = cardBounds();
    closeButton.setBounds (card.getRight() - cardPadding - 28, card.getY() + 8, 28, 28);
    contentResized (contentArea());
}

void OverlayPanel::mouseDown (const MouseEvent& event)
{
    // Clicking the scrim closes, the way clicking outside a dialog does.
    if (! cardBounds().contains (event.getPosition()))
        dismiss();
}

bool OverlayPanel::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::escapeKey)
    {
        dismiss();
        return true;
    }

    return false;
}

} // namespace jazz::ui
