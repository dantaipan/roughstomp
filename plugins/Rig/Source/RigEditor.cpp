#include "RigEditor.h"

namespace
{
const juce::Colour background { 0xff141414 };
const juce::Colour panel      { 0xff1e1e1e };
const juce::Colour ink        { 0xfff3e6cf };
const juce::Colour warning    { 0xffff8a65 };

constexpr int margin = 12;
constexpr int slotWidth = 148, slotGap = 6, slotHeight = 300;
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
FilePanel::FilePanel (Style s, const juce::String& t, const juce::String& empty,
                      juce::AudioProcessorValueTreeState& apvts, const juce::ParameterID& onParam)
    : style (s), title (t), emptyText (empty), onAttachment (apvts, onParam.getParamID(), on)
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
    const bool plate = style == Style::amp && has;   // dark lettering on the amp's name plate
    name.setColour (juce::Label::textColourId, plate ? juce::Colour (0xff2a2018) : (has ? ink : ink.withAlpha (0.4f)));
    name.setJustificationType (plate ? juce::Justification::centred : juce::Justification::centredLeft);
    if (has != hasFile)
    {
        hasFile = has;
        repaint();
    }
    name.setTooltip (has ? file.getFullPathName() : juce::String());
    problem.setText (problemText, juce::dontSendNotification);
    problem.setTooltip (problemText);
    clear.setEnabled (has);
}

namespace
{
/** Fine diagonal lines in both directions: tolex grain or grille weave. */
void crosshatch (juce::Graphics& g, juce::Rectangle<float> area, float spacing, juce::Colour colour, float thickness)
{
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    g.setColour (colour);
    const float h = area.getHeight();
    for (float x = area.getX() - h; x < area.getRight(); x += spacing)
    {
        g.drawLine (x, area.getBottom(), x + h, area.getY(), thickness);
        g.drawLine (x, area.getY(), x + h, area.getBottom(), thickness);
    }
    g.restoreState();
}

const juce::Colour gold { 0xffc8a96a };
} // namespace

void FilePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::Colour titleColour = ink;

    switch (style)
    {
        case Style::amp:
        {
            g.setColour (juce::Colour (0xff181818));
            g.fillRoundedRectangle (b, 10.0f);
            crosshatch (g, b.reduced (3.0f), 3.0f, juce::Colours::white.withAlpha (0.035f), 1.0f);
            g.setColour (gold.withAlpha (0.8f));
            g.drawRoundedRectangle (b.reduced (4.5f), 7.0f, 1.5f);
            titleColour = gold;

            // The capture's name on a cream plate
            if (hasFile)
            {
                const auto plate = name.getBounds().toFloat().expanded (2.0f, 1.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff1e4c4), plate.getTopLeft(),
                                                         juce::Colour (0xffd8c8a2), plate.getBottomLeft(), false));
                g.fillRoundedRectangle (plate, 3.0f);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawRoundedRectangle (plate, 3.0f, 1.0f);
            }
            break;
        }
        case Style::power:
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343434), b.getTopLeft(),
                                                     juce::Colour (0xff212121), b.getBottomLeft(), false));
            g.fillRoundedRectangle (b, 10.0f);
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            for (float y = b.getY() + 3.0f; y < b.getBottom() - 3.0f; y += 2.0f)
                g.drawHorizontalLine (juce::roundToInt (y), b.getX() + 6.0f, b.getRight() - 6.0f);
            break;
        }
        case Style::cab:
        {
            g.setColour (juce::Colour (0xff181818));
            g.fillRoundedRectangle (b, 10.0f);
            crosshatch (g, b.reduced (3.0f), 3.0f, juce::Colours::white.withAlpha (0.035f), 1.0f);

            // Grille cloth with a hint of the speaker behind it
            auto grille = extraArea.toFloat().expanded (0.0f, 4.0f);
            g.setColour (juce::Colour (0xff2b251d));
            g.fillRoundedRectangle (grille, 5.0f);
            crosshatch (g, grille, 4.0f, juce::Colour (0xff8a7556).withAlpha (0.45f), 1.2f);
            const float r = juce::jmin (grille.getWidth(), grille.getHeight()) * 0.38f;
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (grille.getCentre()));
            g.setColour (gold.withAlpha (0.6f));
            g.drawRoundedRectangle (grille, 5.0f, 1.5f);
            break;
        }
    }

    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (titleColour);
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
    load.setBounds (buttons.removeFromLeft (68));
    buttons.removeFromLeft (6);
    clear.setBounds (buttons.removeFromLeft (54));
}

//==============================================================================
RigEditor::RigEditor (RigProcessor& p)
    : AudioProcessorEditor (p),
      rigProcessor (p),
      input      (p.apvts, ParamIDs::input,      "INPUT"),
      gateThreshold (p.apvts, ParamIDs::gateThreshold, "GATE", true),
      ampGain    (p.apvts, ParamIDs::ampGain,    "GAIN", true),
      ampLevel   (p.apvts, ParamIDs::ampLevel,   "LEVEL", true),
      loopSend   (p.apvts, ParamIDs::loopSend,   "SEND", true),
      loopReturn (p.apvts, ParamIDs::loopReturn, "RETURN", true),
      loopMix    (p.apvts, ParamIDs::loopMix,    "MIX", true),
      output     (p.apvts, ParamIDs::output,     "OUTPUT"),
      gateOnAttachment (p.apvts, ParamIDs::gateOn.getParamID(), gateOnButton),
      tunerAttachment (p.apvts, ParamIDs::tuner.getParamID(), tunerButton),
      presetBar (p.getPresets()),
      loopOnAttachment (p.apvts, ParamIDs::loopOn.getParamID(), loopOnButton),
      ampPanel   (FilePanel::Style::amp,   "AMP",       "No capture loaded", p.apvts, ParamIDs::ampOn),
      powerPanel (FilePanel::Style::power, "POWER AMP", "Empty (optional)",  p.apvts, ParamIDs::powerOn),
      cabPanel   (FilePanel::Style::cab,   "CAB",       "No IR loaded",      p.apvts, ParamIDs::cabOn),
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
        board.addAndMakeVisible (*preSlots[static_cast<size_t> (i)]);
        board.addAndMakeVisible (*loopSlots[static_cast<size_t> (i)]);
    }

    // Panels first: the amp's knobs sit on top of its panel
    for (auto* panelComponent : { &ampPanel, &powerPanel, &cabPanel })
        board.addAndMakeVisible (*panelComponent);

    for (auto* knob : { &input, &gateThreshold, &ampGain, &ampLevel, &loopSend, &loopReturn, &loopMix, &output })
    {
        knob->setPopupParent (&board);
        board.addAndMakeVisible (*knob);
    }
    loopOnButton.setTooltip ("Switch the whole effects loop on or off");
    board.addAndMakeVisible (loopOnButton);

    gateOnButton.setTooltip ("Noise gate: silences hiss and hum between notes. GATE sets how quiet counts as silence.");
    board.addAndMakeVisible (gateOnButton);

    tunerButton.setClickingTogglesState (true);
    tunerButton.setTooltip ("Tuner (mutes the output while it's on)");
    tunerButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3f8f3a));
    tunerButton.onStateChange = [this] { tuner.setVisible (tunerButton.getToggleState()); };
    board.addAndMakeVisible (tunerButton);
    board.addAndMakeVisible (presetBar);
    board.addChildComponent (tuner);
    tuner.setVisible (tunerButton.getToggleState());

    ampPanel.onLoad    = [this] { chooseAmp (rig::AmpStage::amp); };
    ampPanel.onClear   = [this] { rigProcessor.clearAmp (rig::AmpStage::amp); refreshPanels(); };
    powerPanel.onLoad  = [this] { chooseAmp (rig::AmpStage::power); };
    powerPanel.onClear = [this] { rigProcessor.clearAmp (rig::AmpStage::power); refreshPanels(); };
    cabPanel.onLoad    = [this] { chooseCabinet(); };
    cabPanel.onClear   = [this] { rigProcessor.clearCabinet(); refreshPanels(); };
    ampPanel.setExtraWidth (130);
    cabPanel.setExtraWidth (40);


    // Everything is laid out once at the design size on the board, which is
    // scaled to fit the window (aspect ratio fixed)
    board.onPaint = [this] (juce::Graphics& g) { paintBoard (g); };
    board.onResize = [this] { layoutBoard(); };
    board.setSize (designWidth(), designHeight());
    addAndMakeVisible (board);

    // (read the saved size first: setting the limits resizes, which saves)
    const double scale = juce::jlimit (minScale, maxScale, static_cast<double> (p.apvts.state.getProperty ("uiScale", 1.0)));
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (designWidth() * minScale), juce::roundToInt (designHeight() * minScale),
                     juce::roundToInt (designWidth() * maxScale), juce::roundToInt (designHeight() * maxScale));
    getConstrainer()->setFixedAspectRatio (static_cast<double> (designWidth()) / designHeight());
    setSize (juce::roundToInt (designWidth() * scale), juce::roundToInt (designHeight() * scale));

    refreshPanels();
    startTimerHz (20);
}

RigEditor::~RigEditor()
{
    setLookAndFeel (nullptr);
}

void RigEditor::timerCallback()
{
    if (tuner.isVisible())
        tuner.update (rigProcessor.readTuner());
    if (++ticks % 10 == 0)
        presetBar.refresh();

    for (auto& s : preSlots)  s->refresh();
    for (auto& s : loopSlots) s->refresh();
    refreshPanels();
}

void RigEditor::refreshPanels()
{
    // While a capture loads in the background, show its name
    auto ampFile = [this] (rig::AmpStage s)
    {
        const auto loading = rigProcessor.getLoadingAmpFile (s);
        return loading != juce::File() ? loading : rigProcessor.getAmpFile (s);
    };
    ampPanel.show   (ampFile (rig::AmpStage::amp),   rigProcessor.getAmpProblem (rig::AmpStage::amp));
    powerPanel.show (ampFile (rig::AmpStage::power), rigProcessor.getAmpProblem (rig::AmpStage::power));
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

                              // In the background: big captures take a moment
                              juce::Component::SafePointer<RigEditor> safe (this);
                              rigProcessor.loadAmpAsync (stage, file, RigProcessor::AmpLoad::keepOld,
                                                         [safe, file] (const juce::String& error)
                              {
                                  if (error.isNotEmpty())
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                              "Couldn't load " + file.getFileName(), error);
                                  if (safe != nullptr)
                                      safe->refreshPanels();
                              });
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

int RigEditor::designWidth()  { return margin * 2 + rig::slotsPerSection * (slotWidth + slotGap) + sideWidth; }
int RigEditor::designHeight() { return margin * 2 + 2 * (labelHeight + slotHeight) + ampRowHeight + 2 * margin + 30; }

void RigEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void RigEditor::resized()
{
    const double scale = static_cast<double> (getWidth()) / designWidth();
    board.setTransform (juce::AffineTransform::scale (static_cast<float> (scale)));
    rigProcessor.apvts.state.setProperty ("uiScale", scale, nullptr);   // sessions remember the size
}

void RigEditor::paintBoard (juce::Graphics& g)
{
    g.fillAll (background);

    // Brand
    auto header = board.getLocalBounds().reduced (margin, 0).removeFromTop (margin + 30);
    g.setColour (ink);
    g.setFont (rs::ui::pedalFont (22.0f));
    g.drawText ("ROUGHSTOMP", header, juce::Justification::centredLeft);

    // Each row of pedals sits on a board with two rails
    for (auto area : { preArea, loopArea })
    {
        auto boardArea = area.withTrimmedTop (labelHeight + 24).toFloat().expanded (4.0f, 6.0f)
                             .withRight (static_cast<float> (preSlots.back()->getRight() + 6));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222222), boardArea.getTopLeft(),
                                                 juce::Colour (0xff1a1a1a), boardArea.getBottomLeft(), false));
        g.fillRoundedRectangle (boardArea, 8.0f);
        for (float t : { 0.3f, 0.72f })
        {
            const auto rail = juce::Rectangle<float> (boardArea.getX() + 6.0f, boardArea.getY() + boardArea.getHeight() * t,
                                                      boardArea.getWidth() - 12.0f, 9.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), rail.getTopLeft(),
                                                     juce::Colour (0xff262626), rail.getBottomLeft(), false));
            g.fillRoundedRectangle (rail, 2.0f);
        }
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (boardArea, 8.0f, 1.0f);
    }

    drawSectionLabel (g, preArea,  "PRE-AMP PEDALS");
    drawSectionLabel (g, loopArea, "FX LOOP");

    // Signal-flow hints between the rows
    g.setColour (ink.withAlpha (0.25f));
    g.drawLine (static_cast<float> (margin + 4), static_cast<float> (preArea.getBottom()),
                static_cast<float> (margin + 4), static_cast<float> (ampArea.getY() + 10), 2.0f);
    g.drawLine (static_cast<float> (margin + 4), static_cast<float> (ampArea.getBottom() - 10),
                static_cast<float> (margin + 4), static_cast<float> (loopArea.getY() + 4), 2.0f);
}

void RigEditor::layoutBoard()
{
    auto b = board.getLocalBounds().reduced (margin);
    auto header = b.removeFromTop (30);
    tunerButton.setBounds (header.removeFromRight (90).reduced (0, 3));
    presetBar.setBounds (header.withSizeKeepingCentre (420, header.getHeight()).reduced (0, 3));

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
    {
        auto gateArea = row.removeFromLeft (72).withSizeKeepingCentre (72, 112);
        gateOnButton.setBounds (gateArea.removeFromTop (24).withSizeKeepingCentre (64, 24));
        gateThreshold.setBounds (gateArea.withSizeKeepingCentre (64, 80));
    }
    row.removeFromLeft (6);
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

    tuner.setBounds (ampArea);
}

//==============================================================================
void TunerDisplay::update (const RigProcessor::TunerReading& r)
{
    // Only confident readings move the display; a gap shows as "play a string"
    if (r.hz > 0.0f && r.confidence > 0.85f)
    {
        const auto n = fx::Note::fromHz (r.hz);
        const auto name = juce::String (std::string (n.name));
        if (name != note || n.octave != octave || framesSinceNote > 10)
            cents = n.cents;                   // a new note: jump straight there
        else
            cents += 0.35f * (n.cents - cents); // the same note: steady the needle
        note = name;
        octave = n.octave;
        framesSinceNote = 0;
    }
    else
        ++framesSinceNote;
    repaint();
}

void TunerDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff101010));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (ink.withAlpha (0.3f));
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    b.reduce (24.0f, 12.0f);
    g.setColour (ink.withAlpha (0.6f));
    g.setFont (rs::ui::pedalFont (13.0f));
    g.drawText ("TUNER  -  output muted", b.removeFromTop (18.0f), juce::Justification::centredLeft);

    const bool showing = framesSinceNote < 20;   // about a second after the note fades
    auto noteArea = b.removeFromLeft (170.0f);
    if (! showing)
    {
        g.setColour (ink.withAlpha (0.5f));
        g.setFont (rs::ui::pedalFont (16.0f, false));
        g.drawText ("Play a string", noteArea, juce::Justification::centred);
    }
    else
    {
        const bool inTune = std::abs (cents) < 3.0f;
        const auto accent = inTune ? juce::Colour (0xff57d15a) : juce::Colour (0xffff8a65);
        g.setColour (accent);
        g.setFont (rs::ui::pedalFont (64.0f));
        g.drawText (note, noteArea.withTrimmedBottom (18.0f), juce::Justification::centred);
        g.setColour (ink.withAlpha (0.7f));
        g.setFont (rs::ui::pedalFont (14.0f, false));
        g.drawText (juce::String (octave) + "   " + (cents >= 0.0f ? "+" : "") + juce::String (cents, 1) + " cents",
                    noteArea.removeFromBottom (20.0f), juce::Justification::centred);
    }

    // Meter: -50 .. +50 cents, a green zone in the middle, and the needle
    auto meter = b.reduced (20.0f, 8.0f);
    const float cy = meter.getCentreY();
    auto xFor = [&] (float c) { return meter.getX() + (c + 50.0f) / 100.0f * meter.getWidth(); };

    g.setColour (juce::Colour (0xff57d15a).withAlpha (0.25f));
    g.fillRect (juce::Rectangle<float> (xFor (-3.0f), cy - 26.0f, xFor (3.0f) - xFor (-3.0f), 52.0f));
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = xFor (static_cast<float> (c));
        const float h = c == 0 ? 26.0f : (c % 50 == 0 ? 20.0f : 12.0f);
        g.setColour (ink.withAlpha (c == 0 ? 0.9f : 0.45f));
        g.drawLine (x, cy - h, x, cy + h, c == 0 ? 2.0f : 1.0f);
    }
    g.setColour (ink.withAlpha (0.5f));
    g.setFont (rs::ui::pedalFont (11.0f, false));
    g.drawText ("-50", juce::Rectangle<float> (xFor (-50.0f) - 20.0f, cy + 28.0f, 40.0f, 14.0f), juce::Justification::centred);
    g.drawText ("+50", juce::Rectangle<float> (xFor (50.0f) - 20.0f, cy + 28.0f, 40.0f, 14.0f), juce::Justification::centred);

    if (showing)
    {
        const float x = xFor (juce::jlimit (-50.0f, 50.0f, cents));
        g.setColour (std::abs (cents) < 3.0f ? juce::Colour (0xff57d15a) : juce::Colour (0xffff8a65));
        g.fillRoundedRectangle (juce::Rectangle<float> (x - 3.0f, cy - 30.0f, 6.0f, 60.0f), 3.0f);
    }
}

//==============================================================================
PresetBar::PresetBar (PresetManager& p) : presets (p)
{
    previous.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    name.setTooltip ("Presets: browse, save as, delete");
    save.setTooltip ("Save the rig to this preset (or as a new one)");

    previous.onClick = [this] { presets.step (-1); refresh(); };
    next.onClick = [this] { presets.step (1); refresh(); };
    name.onClick = [this] { showMenu(); };
    save.onClick = [this]
    {
        // A user preset is saved over; anything else needs a name
        if (presets.getCurrentName().isNotEmpty() && ! presets.isCurrentFactory())
            report (presets.save (presets.getCurrentName()));
        else
            saveAs();
        refresh();
    };

    for (auto* b : { &previous, &name, &next, &save })
        addAndMakeVisible (*b);
    refresh();
}

void PresetBar::refresh()
{
    auto text = presets.getCurrentName();
    if (text.isEmpty())
        text = "No preset";
    if (presets.isModified())
        text += " *";
    name.setButtonText (text);
}

void PresetBar::resized()
{
    auto b = getLocalBounds();
    save.setBounds (b.removeFromRight (64));
    b.removeFromRight (6);
    previous.setBounds (b.removeFromLeft (28));
    next.setBounds (b.removeFromRight (28));
    name.setBounds (b.reduced (3, 0));
}

void PresetBar::showMenu()
{
    const auto entries = presets.list();
    const auto current = presets.getCurrentName();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Factory (keep your amp and cab)");
    int id = 1;
    bool anyUser = false;
    for (const auto& e : entries)
    {
        if (! e.factory && ! anyUser)
        {
            menu.addSectionHeader ("Yours");
            anyUser = true;
        }
        const bool ticked = e.name == current && e.factory == presets.isCurrentFactory();
        menu.addItem (id++, e.name, true, ticked);
    }
    if (! anyUser)
    {
        menu.addSectionHeader ("Yours");
        menu.addItem (-1, "Nothing saved yet", false);
    }

    constexpr int saveAsId = 10000, deleteId = 10001, folderId = 10002;
    menu.addSeparator();
    menu.addItem (saveAsId, "Save As...");
    menu.addItem (deleteId, "Delete \"" + current + "\"", current.isNotEmpty() && ! presets.isCurrentFactory());
    menu.addItem (folderId, "Show Presets Folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name),
                        [this, entries, current] (int result)
                        {
                            if (result >= 1 && result <= static_cast<int> (entries.size()))
                                report (presets.load (entries[static_cast<size_t> (result - 1)]));
                            else if (result == saveAsId)
                                saveAs();
                            else if (result == deleteId)
                            {
                                for (const auto& e : entries)
                                    if (! e.factory && e.name == current)
                                        presets.remove (e);
                            }
                            else if (result == folderId)
                            {
                                PresetManager::getFolder().createDirectory();
                                PresetManager::getFolder().revealToUser();
                            }
                            refresh();
                        });
}

void PresetBar::saveAs()
{
    dialog = std::make_unique<juce::AlertWindow> ("Save Preset", "Name for this rig:", juce::MessageBoxIconType::NoIcon, this);
    const auto suggestion = presets.isCurrentFactory() ? presets.getCurrentName() + " (mine)" : presets.getCurrentName();
    dialog->addTextEditor ("name", suggestion);
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && dialog != nullptr)
            report (presets.save (dialog->getTextEditorContents ("name")));
        dialog.reset();
        refresh();
    }), false);
}

void PresetBar::report (const juce::String& error)
{
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Presets", error);
}
