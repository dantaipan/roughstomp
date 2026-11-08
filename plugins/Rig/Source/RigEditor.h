#pragma once

#include "Presets.h"
#include "RigProcessor.h"
#include "SlotView.h"
#include "rs_ui/Pedal.h"

/** A box for something loaded from a file (amp capture, cabinet IR):
    title, Load/Clear buttons, the file's name, any problem, and an on switch. */
class FilePanel final : public juce::Component
{
public:
    /** How it's dressed: an amp head (tolex, piping, name plate), a power amp
        (dark metal), or a cab (grille cloth on the right). */
    enum class Style { amp, power, cab };

    FilePanel (Style, const juce::String& title, const juce::String& emptyText,
               juce::AudioProcessorValueTreeState&, const juce::ParameterID& onParam);

    void show (const juce::File&, const juce::String& problem);

    std::function<void()> onLoad, onClear;

    /** Room on the right for extra controls (the amp's knobs). */
    juce::Rectangle<int> getExtraArea() const noexcept { return extraArea; }
    void setExtraWidth (int w) { extraWidth = w; resized(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Style style;
    juce::String title, emptyText;
    bool hasFile = false;
    juce::TextButton load { "Load..." }, clear { "Clear" };
    juce::Label name, problem;
    juce::ToggleButton on { "On" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment;
    juce::Rectangle<int> extraArea;
    int extraWidth = 0;
};

//==============================================================================
/** Preset browsing in the header: previous / name (a menu) / next / save. */
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (PresetManager&);

    /** Call now and then: picks up preset changes and the modified mark. */
    void refresh();

    void resized() override;

private:
    void showMenu();
    void saveAs();
    void report (const juce::String& error);

    PresetManager& presets;
    juce::TextButton previous { "<" }, next { ">" }, name, save { "Save" };
    std::unique_ptr<juce::AlertWindow> dialog;
};

//==============================================================================
/** The tuner: note name, how many cents off, and a needle. Shown over the
    amp row while the tuner is on. */
class TunerDisplay final : public juce::Component
{
public:
    /** Call regularly with the processor's latest reading. */
    void update (const RigProcessor::TunerReading&);

    void paint (juce::Graphics&) override;

private:
    juce::String note;
    int octave = 0;
    float cents = 0.0f;         // smoothed
    int framesSinceNote = 1000; // readings since the last clear one
};

//==============================================================================
class RigEditor final : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer,   // pedals are dragged between slots
                        private juce::Timer
{
public:
    explicit RigEditor (RigProcessor&);
    ~RigEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** The design-size surface everything sits on; scaled to the window. */
    struct Board final : public juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        std::function<void()> onResize;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
        void resized() override { if (onResize) onResize(); }
    };

    static constexpr double minScale = 0.7, maxScale = 1.5;
    static int designWidth();
    static int designHeight();
    void paintBoard (juce::Graphics&);
    void layoutBoard();

    void timerCallback() override;
    void chooseAmp (rig::AmpStage);
    void chooseCabinet();
    void refreshPanels();

    RigProcessor& rigProcessor;
    rs::ui::PedalLookAndFeel lookAndFeel;
    Board board;   // before the controls: they're its children

    std::array<std::unique_ptr<SlotView>, rig::slotsPerSection> preSlots, loopSlots;

    rs::ui::PedalKnob input, gateThreshold, ampGain, ampLevel, loopSend, loopReturn, loopMix, output;
    juce::ToggleButton gateOnButton { "Gate" };
    juce::AudioProcessorValueTreeState::ButtonAttachment gateOnAttachment;
    juce::TextButton tunerButton { "TUNER" };
    juce::AudioProcessorValueTreeState::ButtonAttachment tunerAttachment;
    TunerDisplay tuner;
    PresetBar presetBar;
    int ticks = 0;
    juce::ToggleButton loopOnButton { "Loop on" };
    juce::AudioProcessorValueTreeState::ButtonAttachment loopOnAttachment;

    FilePanel ampPanel, powerPanel, cabPanel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastFolder;

    juce::Rectangle<int> preArea, ampArea, loopArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigEditor)
};
