#pragma once

#include "RigProcessor.h"
#include "rs_ui/Pedal.h"

/** One pedal slot: a menu to pick the pedal, its knobs and switches, and a
    footswitch. Rebuilds itself whenever the slot's pedal changes. */
class SlotView final : public juce::Component
{
public:
    SlotView (RigProcessor&, rig::Section, int slot);

    /** Call regularly: follows pedal changes (including restored sessions)
        and the bypass LED. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    static rs::ui::PedalColours coloursFor (const fx::PedalInfo*);

private:
    void rebuild();

    struct Choice
    {
        std::unique_ptr<juce::ComboBox> box;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    /** A vertical slider (graphic EQ band or level), labelled underneath. */
    struct Fader
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String label;
        const fx::ParamInfo* info = nullptr;
    };

    bool isGraphicEq() const noexcept { return shown != nullptr && shown->style == fx::PedalInfo::Style::graphicEq; }
    void paintEqCurve (juce::Graphics&);

    RigProcessor& rigProcessor;
    const rig::Section section;
    const int index;

    const fx::PedalInfo* shown = nullptr;
    bool built = false;
    rs::ui::PedalColours colours;

    juce::ComboBox typeBox;
    std::vector<std::unique_ptr<rs::ui::PedalKnob>> knobs;
    std::vector<Choice> choices;
    std::vector<Fader> faders;
    juce::Rectangle<int> labelRow;   // under the faders
    std::unique_ptr<rs::ui::Footswitch> footswitch;
    std::unique_ptr<rs::ui::Led> led;
};
