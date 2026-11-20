#include "MidiMapView.h"

#include "rs_ui/Family.h"

namespace
{
namespace fam = rs::ui::family;
const juce::Identifier tagKey { "rsMidiParam" };

/** The component a click at p (in parent) would reach, ignoring `skip`. */
juce::Component* topmostAt (juce::Component& parent, juce::Point<int> p, const juce::Component* skip)
{
    for (int i = parent.getNumChildComponents(); --i >= 0;)
    {
        auto* child = parent.getChildComponent (i);
        if (child == skip || ! child->isVisible())
            continue;
        const auto local = child->getLocalPoint (&parent, p);
        if (child->getLocalBounds().contains (local))
            return topmostAt (*child, local, skip);
    }
    return &parent;
}
} // namespace

void midimap::tag (juce::Component& c, const juce::ParameterID& id)
{
    c.getProperties().set (tagKey, id.getParamID());
}

juce::String midimap::taggedParameter (const juce::Component& c)
{
    return c.getProperties()[tagKey].toString();
}

//==============================================================================
MidiMapOverlay::MidiMapOverlay (RigProcessor& p, juce::Component& b) : rigProcessor (p), board (b)
{
    rigProcessor.getMidi().getMappings().addChangeListener (this);
}

MidiMapOverlay::~MidiMapOverlay()
{
    rigProcessor.getMidi().getMappings().removeChangeListener (this);
    rigProcessor.getMidi().learn ({});
}

void MidiMapOverlay::visibilityChanged()
{
    if (isVisible())
        startTimerHz (10);
    else
    {
        stopTimer();
        rigProcessor.getMidi().learn ({});
    }
}

void MidiMapOverlay::timerCallback()
{
    repaint();   // controls come and go with the pedals; learning ends when a CC comes in
}

void MidiMapOverlay::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

std::vector<MidiMapOverlay::Target> MidiMapOverlay::findTargets() const
{
    std::vector<Target> targets;
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (child == this || ! child->isVisible())
                continue;
            if (const auto id = midimap::taggedParameter (*child); id.isNotEmpty())
            {
                // Only what can be seen (not, say, the amps under the tuner)
                const auto bounds = getLocalArea (child, child->getLocalBounds());
                const auto centre = board.getLocalPoint (this, bounds.getCentre());
                auto* top = topmostAt (board, centre, this);
                if (top == child || child->isParentOf (top))
                    targets.push_back ({ child, bounds, id });
                continue;
            }
            visit (*child);
        }
    };
    visit (board);
    return targets;
}

const MidiMapOverlay::Target* MidiMapOverlay::targetAt (const std::vector<Target>& targets, juce::Point<int> p) const
{
    const Target* best = nullptr;
    for (const auto& t : targets)
        if (t.bounds.contains (p) && (best == nullptr || t.bounds.getWidth() * t.bounds.getHeight() < best->bounds.getWidth() * best->bounds.getHeight()))
            best = &t;
    return best;
}

void MidiMapOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.35f));

    auto& midi = rigProcessor.getMidi();
    const auto learning = midi.getLearning();
    g.setFont (fam::font (11.0f, true));

    for (const auto& t : findTargets())
    {
        const auto r = t.bounds.toFloat().expanded (2.0f);   // clear of the control's own edge
        const auto mapping = midi.getMappings().forParameter (t.paramId);

        if (t.paramId == learning)
        {
            g.setColour (fam::amber.withAlpha (0.3f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (fam::amber);
            g.drawRoundedRectangle (r, 4.0f, 2.0f);
        }
        else if (mapping.has_value())
        {
            g.setColour (fam::amber.withAlpha (0.16f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (fam::amber.withAlpha (0.8f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
        else
        {
            g.setColour (fam::cream.withAlpha (0.3f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }

        // A badge: the CC, or that it's listening
        const auto text = t.paramId == learning ? juce::String ("Move...")
                        : mapping.has_value() ? "CC " + juce::String (mapping->cc)
                        : juce::String();
        if (text.isNotEmpty())
        {
            // On the top edge, so it hides as little of the control as it can
            const float w = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text)) + 10.0f;
            const auto badge = juce::Rectangle<float> (w, 15.0f).withCentre ({ r.getCentreX(), juce::jmax (8.0f, r.getY()) });
            g.setColour (fam::amber);
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (fam::recess);
            g.drawText (text, badge, juce::Justification::centred, false);
        }
    }

    // The hint, over the presets
    if (! hintArea.isEmpty())
    {
        g.setColour (fam::strip);
        g.fillRoundedRectangle (hintArea.toFloat(), 4.0f);
        g.setColour (fam::cream);
        g.setFont (fam::font (13.0f));
        const auto hint = learning.isNotEmpty() ? juce::String ("Now move a knob, fader or footswitch on your controller")
                                                : juce::String ("Click a control, then move one on your controller. Right-click to forget.");
        g.drawFittedText (hint, hintArea.reduced (8, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void MidiMapOverlay::mouseDown (const juce::MouseEvent& e)
{
    const auto targets = findTargets();
    const auto* t = targetAt (targets, e.getPosition());
    auto& midi = rigProcessor.getMidi();

    if (t == nullptr)
    {
        midi.learn ({});
    }
    else if (e.mods.isPopupMenu())
    {
        showMenu (*t);
        return;
    }
    else
        midi.learn (midi.getLearning() == t->paramId ? juce::String() : t->paramId);   // a second click stops listening
    repaint();
}

void MidiMapOverlay::showMenu (const Target& t)
{
    auto& midi = rigProcessor.getMidi();
    const auto mapping = midi.getMappings().forParameter (t.paramId);
    const auto* param = rigProcessor.apvts.getParameter (t.paramId);
    const auto name = param != nullptr ? param->getName (64) : t.paramId;

    juce::PopupMenu menu;
    menu.addSectionHeader (name);
    menu.addItem ("Learn", [this, id = t.paramId] { rigProcessor.getMidi().learn (id); repaint(); });
    if (mapping.has_value())
    {
        if (param != nullptr && param->isBoolean())
            menu.addItem ("Flip on each press (momentary footswitch)", true, mapping->toggle,
                          [this, id = t.paramId, toggle = mapping->toggle] { rigProcessor.getMidi().getMappings().setToggle (id, ! toggle); });
        menu.addItem ("Forget CC " + juce::String (mapping->cc),
                      [this, id = t.paramId] { rigProcessor.getMidi().getMappings().forget (id); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
