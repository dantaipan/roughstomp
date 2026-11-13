#pragma once

#include "RigProcessor.h"
#include "ViewSettings.h"
#include "rs_ui/Pedal.h"

/** One pedal slot: a menu to pick the pedal, then its controls, either as a
    hardware pedal (knobs, footswitch) or as plain sliders, as the view
    preference says. Rebuilds itself whenever the slot's pedal or the view
    changes. */
class SlotView final : public juce::Component,
                       public juce::DragAndDropTarget,
                       private juce::ChangeListener
{
public:
    SlotView (RigProcessor&, rig::Section, int slot);
    ~SlotView() override;

    /** Call regularly: follows pedal changes (including restored sessions)
        and the bypass LED. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    static rs::ui::PedalColours coloursFor (const fx::PedalInfo*);

    // A NAM capture slot's buttons: what to do is up to the editor
    std::function<void()> onLoadCapture, onClearCapture, onBrowseCapture, onShowCaptureTone;

    // Drag a pedal by its body onto another slot to swap them
    void mouseDrag (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    void rebuild();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // the view preference
    bool slidersView() const noexcept { return sliders; }
    void paintPedal (juce::Graphics&, juce::Rectangle<float> body);
    void paintSliders (juce::Graphics&, juce::Rectangle<float> body);
    void layoutPedal (juce::Rectangle<int>);
    void layoutSliders (juce::Rectangle<int>);
    bool isCapture() const noexcept;
    /** The capture's name, model or problem, and its buttons. Returns what's left. */
    juce::Rectangle<int> layoutCapture (juce::Rectangle<int>);
    void refreshCapture();
    void mouseUp (const juce::MouseEvent&) override;

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

    /** The slider view: a control's name and value over a slider. */
    struct Row
    {
        std::unique_ptr<juce::Label> name, value;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    bool isGraphicEq() const noexcept { return shown != nullptr && shown->style == fx::PedalInfo::Style::graphicEq; }
    void paintEqMarkings (juce::Graphics&);

    RigProcessor& rigProcessor;
    const rig::Section section;
    const int index;

    const fx::PedalInfo* shown = nullptr;
    bool built = false;
    rs::ui::PedalColours colours;
    // The pedal's own look (its colours), for its controls; declared before
    // them so it outlives them
    std::unique_ptr<rs::ui::PedalLookAndFeel> pedalLook;

    bool dropHighlight = false;

    juce::ComboBox typeBox;
    std::vector<std::unique_ptr<rs::ui::PedalKnob>> knobs;
    std::vector<Choice> choices;
    std::vector<Fader> faders;
    juce::Rectangle<int> labelRow;   // under the faders
    std::unique_ptr<rs::ui::Footswitch> footswitch;
    std::unique_ptr<rs::ui::Led> led;

    // The slider view's controls
    juce::SharedResourcePointer<ViewSettings> view;
    bool sliders = false;
    std::vector<Row> rows;
    std::unique_ptr<juce::TextButton> onButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

    // A NAM capture slot
    std::unique_ptr<juce::Label> captureName, captureInfo;
    std::unique_ptr<juce::TextButton> loadButton, tone3000Button, clearButton;
    bool captureFromTone3000 = false;
    juce::Rectangle<int> captureMarkArea;   // the T3K mark, beside the model's name
};
