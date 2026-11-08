#include "RigEditor.h"

namespace
{
const juce::Colour background { 0xff141414 };
const juce::Colour panel      { 0xff1e1e1e };
const juce::Colour ink        { 0xfff3e6cf };
const juce::Colour warning    { 0xffff8a65 };

constexpr int margin = 12;
constexpr int slotWidth = 148, slotGap = 6, slotHeight = 244;
constexpr int sideWidth = 112;
constexpr int labelHeight = 20;
constexpr int ampRowHeight = 150;

void drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    g.setColour (ink.withAlpha (0.75f));
    g.setFont (rs::ui::pedalFont (13.0f));
    g.drawText (text, area.removeFromTop (labelHeight), juce::Justification::centredLeft);
}
} // namespace

//==============================================================================
FilePanel::FilePanel (const juce::String& t, const juce::String& empty,
                      juce::AudioProcessorValueTreeState& apvts, const juce::ParameterID& onParam)
    : title (t), emptyText (empty), onAttachment (apvts, onParam.getParamID(), on)
{
    load.onClick  = [this] { if (onLoad)  onLoad(); };
    clear.onClick = [this] { if (onClear) onClear(); };

    name.setFont (rs::ui::pedalFont (14.0f));
    name.setColour (juce::Label::textColourId, ink);
    name.setMinimumHorizontalScale (0.6f);
    problem.setFont (rs::ui::pedalFont (11.0f, false));
    problem.setColour (juce::Label::textColourId, warning);
    problem.setMinimumHorizontalScale (0.5f);

    for (auto* c : std::initializer_list<juce::Component*> { &load, &clear, &name, &problem, &on })
        addAndMakeVisible (c);
}

void FilePanel::show (const juce::File& file, const juce::String& problemText)
{
    const bool has = file != juce::File();
    name.setText (has ? file.getFileNameWithoutExtension() : emptyText, juce::dontSendNotification);
    name.setColour (juce::Label::textColourId, has ? ink : ink.withAlpha (0.4f));
    name.setTooltip (has ? file.getFullPathName() : juce::String());
    problem.setText (problemText, juce::dontSendNotification);
    problem.setTooltip (problemText);
    clear.setEnabled (has);
}

void FilePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (ink);
    g.setFont (rs::ui::pedalFont (15.0f));
    g.drawText (title, getLocalBounds().reduced (12, 8).removeFromTop (20), juce::Justification::centredLeft);
}

void FilePanel::resized()
{
    auto b = getLocalBounds().reduced (12, 8);
    extraArea = b.removeFromRight (extraWidth);

    auto top = b.removeFromTop (20);
    on.setBounds (top.removeFromRight (56));

    b.removeFromTop (6);
    name.setBounds (b.removeFromTop (24));
    problem.setBounds (b.removeFromTop (30));

    auto buttons = b.removeFromBottom (26);
    load.setBounds (buttons.removeFromLeft (80));
    buttons.removeFromLeft (6);
    clear.setBounds (buttons.removeFromLeft (64));
}

//==============================================================================
RigEditor::RigEditor (RigProcessor& p)
    : AudioProcessorEditor (p),
      rigProcessor (p),
      input      (p.apvts, ParamIDs::input,      "INPUT"),
      ampGain    (p.apvts, ParamIDs::ampGain,    "GAIN", true),
      ampLevel   (p.apvts, ParamIDs::ampLevel,   "LEVEL", true),
      loopSend   (p.apvts, ParamIDs::loopSend,   "SEND", true),
      loopReturn (p.apvts, ParamIDs::loopReturn, "RETURN", true),
      loopMix    (p.apvts, ParamIDs::loopMix,    "MIX", true),
      output     (p.apvts, ParamIDs::output,     "OUTPUT"),
      loopOnAttachment (p.apvts, ParamIDs::loopOn.getParamID(), loopOnButton),
      ampPanel   ("AMP",       "No capture loaded", p.apvts, ParamIDs::ampOn),
      powerPanel ("POWER AMP", "Empty (optional)",  p.apvts, ParamIDs::powerOn),
      cabPanel   ("CAB",       "No IR loaded",      p.apvts, ParamIDs::cabOn),
      lastFolder (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory))
{
    setLookAndFeel (&lookAndFeel);
    lookAndFeel.setColour (juce::ToggleButton::textColourId, ink);
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, ink);
    lookAndFeel.setColour (juce::ToggleButton::tickDisabledColourId, ink.withAlpha (0.4f));
    lookAndFeel.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2c2c2c));
    lookAndFeel.setColour (juce::TextButton::textColourOffId, ink);
    lookAndFeel.setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff2c2c2c));

    for (int i = 0; i < rig::slotsPerSection; ++i)
    {
        preSlots[static_cast<size_t> (i)]  = std::make_unique<SlotView> (p, rig::Section::pre, i);
        loopSlots[static_cast<size_t> (i)] = std::make_unique<SlotView> (p, rig::Section::loop, i);
        addAndMakeVisible (*preSlots[static_cast<size_t> (i)]);
        addAndMakeVisible (*loopSlots[static_cast<size_t> (i)]);
    }

    // Panels first: the amp's knobs sit on top of its panel
    for (auto* panelComponent : { &ampPanel, &powerPanel, &cabPanel })
        addAndMakeVisible (*panelComponent);

    for (auto* knob : { &input, &ampGain, &ampLevel, &loopSend, &loopReturn, &loopMix, &output })
    {
        knob->setPopupParent (this);
        addAndMakeVisible (*knob);
    }
    loopOnButton.setTooltip ("Switch the whole effects loop on or off");
    addAndMakeVisible (loopOnButton);

    ampPanel.onLoad    = [this] { chooseAmp (rig::AmpStage::amp); };
    ampPanel.onClear   = [this] { rigProcessor.clearAmp (rig::AmpStage::amp); refreshPanels(); };
    powerPanel.onLoad  = [this] { chooseAmp (rig::AmpStage::power); };
    powerPanel.onClear = [this] { rigProcessor.clearAmp (rig::AmpStage::power); refreshPanels(); };
    cabPanel.onLoad    = [this] { chooseCabinet(); };
    cabPanel.onClear   = [this] { rigProcessor.clearCabinet(); refreshPanels(); };
    ampPanel.setExtraWidth (130);


    const int width = margin * 2 + rig::slotsPerSection * (slotWidth + slotGap) + sideWidth;
    const int height = margin * 2 + 2 * (labelHeight + slotHeight) + ampRowHeight + 2 * margin + 30;
    setSize (width, height);

    refreshPanels();
    startTimerHz (10);
}

RigEditor::~RigEditor()
{
    setLookAndFeel (nullptr);
}

void RigEditor::timerCallback()
{
    for (auto& s : preSlots)  s->refresh();
    for (auto& s : loopSlots) s->refresh();
    refreshPanels();
}

void RigEditor::refreshPanels()
{
    ampPanel.show   (rigProcessor.getAmpFile (rig::AmpStage::amp),   rigProcessor.getAmpProblem (rig::AmpStage::amp));
    powerPanel.show (rigProcessor.getAmpFile (rig::AmpStage::power), rigProcessor.getAmpProblem (rig::AmpStage::power));
    cabPanel.show   (rigProcessor.getCabinetFile(),                  rigProcessor.getCabinetProblem());
}

void RigEditor::chooseAmp (rig::AmpStage stage)
{
    const auto prompt = stage == rig::AmpStage::amp ? "Choose an amp capture" : "Choose a power amp capture";
    chooser = std::make_unique<juce::FileChooser> (prompt, lastFolder, "*.nam");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, stage] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              lastFolder = file.getParentDirectory();

                              if (const auto error = rigProcessor.loadAmp (stage, file); error.isNotEmpty())
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                          "Couldn't load " + file.getFileName(), error);
                              refreshPanels();
                          });
}

void RigEditor::chooseCabinet()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a cabinet IR", lastFolder, "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              lastFolder = file.getParentDirectory();

                              if (const auto error = rigProcessor.loadCabinet (file); error.isNotEmpty())
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                          "Couldn't load " + file.getFileName(), error);
                              refreshPanels();
                          });
}

void RigEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Brand
    auto header = getLocalBounds().reduced (margin, 0).removeFromTop (margin + 30);
    g.setColour (ink);
    g.setFont (rs::ui::pedalFont (22.0f));
    g.drawText ("ROUGHSTOMP", header, juce::Justification::centredLeft);
    g.setColour (ink.withAlpha (0.5f));
    g.setFont (rs::ui::pedalFont (12.0f, false));
    g.drawText ("guitar -> pre-amp pedals -> amp -> FX loop -> power amp -> cab -> out",
                header, juce::Justification::centredRight);

    drawSectionLabel (g, preArea,  "PRE-AMP PEDALS");
    drawSectionLabel (g, loopArea, "FX LOOP");

    // Signal-flow hints between the rows
    g.setColour (ink.withAlpha (0.25f));
    g.drawLine (static_cast<float> (margin + 4), static_cast<float> (preArea.getBottom()),
                static_cast<float> (margin + 4), static_cast<float> (ampArea.getY() + 10), 2.0f);
    g.drawLine (static_cast<float> (margin + 4), static_cast<float> (ampArea.getBottom() - 10),
                static_cast<float> (margin + 4), static_cast<float> (loopArea.getY() + 4), 2.0f);
}

void RigEditor::resized()
{
    auto b = getLocalBounds().reduced (margin);
    b.removeFromTop (30);

    preArea = b.removeFromTop (labelHeight + slotHeight);
    b.removeFromTop (margin);
    ampArea = b.removeFromTop (ampRowHeight);
    b.removeFromTop (margin);
    loopArea = b.removeFromTop (labelHeight + slotHeight);

    auto layoutSlots = [] (juce::Rectangle<int> area, auto& views)
    {
        area.removeFromTop (labelHeight);
        for (auto& v : views)
        {
            v->setBounds (area.removeFromLeft (slotWidth));
            area.removeFromLeft (slotGap);
        }
        return area;
    };

    layoutSlots (preArea, preSlots);
    auto loopSide = layoutSlots (loopArea, loopSlots).reduced (4, 0);

    // Loop controls beside the loop pedals
    loopOnButton.setBounds (loopSide.removeFromTop (24));
    for (auto* knob : { &loopSend, &loopReturn, &loopMix })
        knob->setBounds (loopSide.removeFromTop (72));

    // Amp row: input, amp, power amp, cab, output
    auto row = ampArea.withTrimmedLeft (14);
    input.setBounds (row.removeFromLeft (84).withSizeKeepingCentre (84, 100));
    output.setBounds (row.removeFromRight (84).withSizeKeepingCentre (84, 100));
    row.removeFromRight (8);

    const int gap = 8;
    const int ampW = 360;
    const int rest = (row.getWidth() - ampW - 2 * gap) / 2;
    ampPanel.setBounds (row.removeFromLeft (ampW));
    row.removeFromLeft (gap);
    powerPanel.setBounds (row.removeFromLeft (rest));
    row.removeFromLeft (gap);
    cabPanel.setBounds (row);

    // The amp's knobs sit inside its panel
    auto extra = ampPanel.getExtraArea().translated (ampPanel.getX(), ampPanel.getY()).withTrimmedTop (22);
    ampGain.setBounds (extra.removeFromLeft (extra.getWidth() / 2).withSizeKeepingCentre (64, 80));
    ampLevel.setBounds (extra.withSizeKeepingCentre (64, 80));
}
