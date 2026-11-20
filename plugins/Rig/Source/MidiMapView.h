#pragma once

#include "RigProcessor.h"

namespace midimap
{
/** Marks a control as moving this parameter, so the MIDI map can find it.
    Tag the component that should light up (a knob with its label, a
    footswitch, a slider). */
void tag (juce::Component&, const juce::ParameterID&);
juce::String taggedParameter (const juce::Component&);
} // namespace midimap

/** MIDI map mode, over the whole board: every control that MIDI can move is
    outlined, the mapped ones with their CC. Click a control, then move a knob,
    fader or footswitch on the controller to map it; click it again to stop
    listening. Right-click a mapped one to forget it or (a switch) choose
    whether it flips on each press. Clicks never reach the controls beneath,
    except in the pass-through area (the button that ends the mode). */
class MidiMapOverlay final : public juce::Component,
                             private juce::Timer,
                             private juce::ChangeListener
{
public:
    MidiMapOverlay (RigProcessor&, juce::Component& board);
    ~MidiMapOverlay() override;

    /** Where clicks go through to what's beneath, and where the hint goes. */
    void setPassThrough (juce::Rectangle<int> area) { passThrough = area; }
    void setHintArea (juce::Rectangle<int> area) { hintArea = area; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override { return ! passThrough.contains (x, y); }
    void visibilityChanged() override;

private:
    struct Target
    {
        juce::Component* component = nullptr;
        juce::Rectangle<int> bounds;   // in the overlay
        juce::String paramId;
    };
    std::vector<Target> findTargets() const;
    const Target* targetAt (const std::vector<Target>&, juce::Point<int>) const;
    void showMenu (const Target&);

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    RigProcessor& rigProcessor;
    juce::Component& board;
    juce::Rectangle<int> passThrough, hintArea;
};
