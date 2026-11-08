#include "RigEditor.h"

namespace
{
// The family palette (shared with RoughTrack)
namespace fam = rs::ui::family;
const juce::Colour background = fam::panel;
const juce::Colour ink        = fam::cream;
const juce::Colour warning    = fam::amber;

constexpr int margin = 12;
constexpr int slotWidth = 148, slotGap = 6, slotHeight = 300;
constexpr int sideWidth = 112;
constexpr int labelHeight = 20;
constexpr int ampRowHeight = 150;
constexpr int headerHeight = 48;     // wordmark, presets and Tuner, centred in it
constexpr int headerControlHeight = 26;
constexpr int boardOverhang = 6;     // a pedalboard reaches this far beyond its slots

constexpr int boxTitleHeight = 26;    // a box's title row
constexpr int boxButtonsHeight = 26;  // its bottom row (buttons, switches)

/** Every box in the middle row and the loop's: RoughTrack's console strip
    colour and corners, the title top left. */
void drawBox (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& title)
{
    g.setColour (fam::strip);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

    g.setColour (fam::cream);
    g.setFont (fam::font (15.0f));
    g.drawText (title, b.reduced (12.0f, 8.0f).removeFromTop (20.0f), juce::Justification::centredLeft);
}

/** A section heading, as RoughTrack's "Console". */
void drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    g.setColour (fam::dimText);
    g.setFont (fam::font (13.0f, true));
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

    name.setFont (fam::font (14.0f));
    name.setColour (juce::Label::textColourId, ink);
    name.setMinimumHorizontalScale (1.0f);   // long names end in "..." rather than squashing
    problem.setFont (fam::font (12.0f));
    problem.setColour (juce::Label::textColourId, warning);
    problem.setMinimumHorizontalScale (0.5f);

    for (auto* c : std::initializer_list<juce::Component*> { &load, &clear, &name, &problem, &on })
        addAndMakeVisible (c);
}

void FilePanel::show (const juce::File& file, const juce::String& problemText)
{
    const bool has = file != juce::File();
    name.setText (has ? file.getFileNameWithoutExtension() : emptyText, juce::dontSendNotification);
    name.setColour (juce::Label::textColourId, has ? ink : fam::dimText);
    name.setTooltip (has ? file.getFullPathName() : juce::String());
    problem.setText (problemText, juce::dontSendNotification);
    problem.setColour (juce::Label::textColourId, problemText.startsWith ("Loading") ? fam::dimText : warning);
    problem.setTooltip (problemText);
    clear.setEnabled (has);
}

void FilePanel::paint (juce::Graphics& g)
{
    drawBox (g, getLocalBounds().toFloat(), title);
}

void FilePanel::resized()
{
    // The title row spans the whole box (the On switch at its right end);
    // room for extra controls comes out of what's below it
    auto b = getLocalBounds().reduced (12, 8);
    auto top = b.removeFromTop (20);
    on.setBounds (top.removeFromRight (56));
    extraArea = b.removeFromRight (extraWidth);

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
      input      (p.apvts, ParamIDs::input,      "Level"),
      gateThreshold (p.apvts, ParamIDs::gateThreshold, "Threshold"),
      loopSend   (p.apvts, ParamIDs::loopSend,   "Send"),
      loopReturn (p.apvts, ParamIDs::loopReturn, "Return"),
      loopMix    (p.apvts, ParamIDs::loopMix,    "Mix"),
      output     (p.apvts, ParamIDs::output,     "Level"),
      ampGain    (p.apvts, ParamIDs::ampGain,    "Gain"),
      ampLevel   (p.apvts, ParamIDs::ampLevel,   "Level"),
      gateOnAttachment (p.apvts, ParamIDs::gateOn.getParamID(), gateOnButton),
      tunerAttachment (p.apvts, ParamIDs::tuner.getParamID(), tunerButton),
      presetBar (p.getPresets()),
      loopOnAttachment (p.apvts, ParamIDs::loopOn.getParamID(), loopOnButton),
      ampPanel   ("Amp",       "No capture loaded", p.apvts, ParamIDs::ampOn),
      powerPanel ("Power amp", "Empty (optional)",  p.apvts, ParamIDs::powerOn),
      cabPanel   ("Cab",       "No IR loaded",      p.apvts, ParamIDs::cabOn),
      lastFolder (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory))
{
    setLookAndFeel (&lookAndFeel);

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

    for (auto* knob : { &input, &gateThreshold, &loopSend, &loopReturn, &loopMix, &output, &ampGain, &ampLevel })
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
    tunerButton.setColour (juce::TextButton::buttonOnColourId, fam::green);   // as RoughTrack's
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
    ampPanel.setExtraWidth (120);   // the amp's Gain and Level


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
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double seconds = lastTick > 0.0 ? juce::jlimit (0.0, 0.5, (now - lastTick) / 1000.0) : 0.05;
    lastTick = now;
    if (tuner.isVisible())
        tuner.update (rigProcessor.readTuner(), seconds);
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
int RigEditor::designHeight()
{
    return headerHeight + ampRowHeight + margin + (labelHeight + slotHeight) + margin + boardOverhang
           + (labelHeight + slotHeight) + margin;
}

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
    auto header = board.getLocalBounds().reduced (margin, 0).removeFromTop (headerHeight);
    g.setColour (ink);
    g.setFont (fam::font (20.0f, true));
    g.drawText ("RoughStomp", header, juce::Justification::centredLeft);

    // Each row of pedals sits on a board with two rails
    for (auto area : { preArea, loopArea })
    {
        auto boardArea = area.withTrimmedTop (labelHeight + 24).toFloat().expanded (4.0f, static_cast<float> (boardOverhang))
                             .withRight (static_cast<float> (preSlots.back()->getRight() + 6));
        g.setGradientFill (juce::ColourGradient (fam::strip.darker (0.15f), boardArea.getTopLeft(),
                                                 fam::recess.brighter (0.1f), boardArea.getBottomLeft(), false));
        g.fillRoundedRectangle (boardArea, 8.0f);
        for (float t : { 0.3f, 0.72f })
        {
            const auto rail = juce::Rectangle<float> (boardArea.getX() + 6.0f, boardArea.getY() + boardArea.getHeight() * t,
                                                      boardArea.getWidth() - 12.0f, 9.0f);
            g.setGradientFill (juce::ColourGradient (fam::outline, rail.getTopLeft(),
                                                     fam::widget.darker (0.3f), rail.getBottomLeft(), false));
            g.fillRoundedRectangle (rail, 2.0f);
        }
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (boardArea, 8.0f, 1.0f);
    }

    drawBox (g, inputBox.toFloat(), "Input");
    drawBox (g, outputBox.toFloat(), "Output");
    drawBox (g, loopBox.toFloat(), "Loop");

    drawSectionLabel (g, preArea,  "Pre-amp pedals");
    drawSectionLabel (g, loopArea, "FX loop");
}

void RigEditor::layoutBoard()
{
    auto b = board.getLocalBounds();
    {
        // Everything in the header shares its centre line
        auto header = b.removeFromTop (headerHeight).reduced (margin, 0);
        tunerButton.setBounds (header.removeFromRight (90).withSizeKeepingCentre (90, headerControlHeight));
        presetBar.setBounds (header.withSizeKeepingCentre (420, headerControlHeight));
    }
    b = b.reduced (margin, 0).withTrimmedBottom (margin);

    // The Input-to-Output row on top (where the tuner shows, as in RoughTrack),
    // then the two rows of pedals
    ampArea = b.removeFromTop (ampRowHeight);
    b.removeFromTop (margin);
    preArea = b.removeFromTop (labelHeight + slotHeight);
    b.removeFromTop (margin + boardOverhang);   // the same gap below the board as below the row above it
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

    // Loop controls in their box beside the loop pedals: title, On, knobs
    loopBox = loopSide.withBottom (loopSide.getBottom() + boardOverhang);   // down to the board's bottom edge
    {
        auto inner = loopBox.reduced (8, 8);
        auto title = inner.removeFromTop (boxTitleHeight).withTrimmedTop (-4);
        loopOnButton.setBounds (title.removeFromRight (52));
        inner.removeFromTop (4);
        for (auto* knob : { &loopSend, &loopReturn, &loopMix })
            knob->setBounds (inner.removeFromTop (72));
    }

    // Middle row, in signal order: Input, Amp, Power amp, Cab, Output. Every
    // box has a title row on top and a buttons row at the bottom, so knobs
    // sit at the same height in each.
    const int gap = 8;
    auto row = ampArea.withTrimmedLeft (-4);   // flush with the pedalboards' left edge
    inputBox = row.removeFromLeft (150);
    row.removeFromLeft (gap);
    outputBox = row.removeFromRight (96);
    row.removeFromRight (gap);

    auto knobArea = [] (juce::Rectangle<int> box)
    {
        return box.reduced (10, 8).withTrimmedTop (boxTitleHeight).withTrimmedBottom (boxButtonsHeight);
    };

    {
        auto k = knobArea (inputBox);
        input.setBounds (k.removeFromLeft (k.getWidth() / 2));
        gateThreshold.setBounds (k);
        gateOnButton.setBounds (inputBox.reduced (10, 8).removeFromBottom (boxButtonsHeight).withTrimmedLeft (input.getWidth()));
    }
    output.setBounds (knobArea (outputBox));

    const int ampW = 320;
    const int rest = (row.getWidth() - ampW - 2 * gap) / 2;
    ampPanel.setBounds (row.removeFromLeft (ampW));
    row.removeFromLeft (gap);
    powerPanel.setBounds (row.removeFromLeft (rest));
    row.removeFromLeft (gap);
    cabPanel.setBounds (row);

    // The amp's knobs sit inside its box, level with the others
    {
        auto extra = ampPanel.getExtraArea().translated (ampPanel.getX(), ampPanel.getY());
        auto k = extra.withY (knobArea (inputBox).getY()).withHeight (knobArea (inputBox).getHeight());
        ampGain.setBounds (k.removeFromLeft (k.getWidth() / 2));
        ampLevel.setBounds (k);
    }

    tuner.setBounds (ampArea.withTrimmedLeft (-4));   // the row's full width
}

//==============================================================================
namespace
{
// As RoughTrack's tuner ribbon
constexpr size_t readingsSmoothed = 5;
constexpr double secondsNoteStays = 1.5;
constexpr double inTuneCents = 1.0;
constexpr double centsToBarsPerSecond = 0.25;
constexpr float barWidthPixels = 28.0f;
constexpr int noteWidth = 110, centsWidth = 110, statusWidth = 120;
} // namespace

void TunerDisplay::update (const RigProcessor::TunerReading& r, double secondsSinceLast)
{
    secondsSinceNote += secondsSinceLast;

    if (r.hz > 0.0f && r.confidence > 0.85f)
    {
        const auto n = fx::Note::fromHz (r.hz);
        if (n.midi != shownNote)
            recentCents.clear();
        shownNote = n.midi;
        secondsSinceNote = 0.0;

        // A plucked string starts sharp and wobbles: show the median of a few
        recentCents.push_back (n.cents);
        if (recentCents.size() > readingsSmoothed)
            recentCents.erase (recentCents.begin());
        auto sorted = recentCents;
        std::sort (sorted.begin(), sorted.end());
        shownCents = sorted[sorted.size() / 2];
    }

    if (shownNote >= 0 && secondsSinceNote > secondsNoteStays)
    {
        shownNote = -1;
        recentCents.clear();
    }

    if (shownNote >= 0)
        strobePhase += shownCents * centsToBarsPerSecond * secondsSinceLast;

    repaint();
}

void TunerDisplay::paintBand (juce::Graphics& g, juce::Rectangle<float> area, float barWidth, float offset, juce::Colour colour) const
{
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    g.setColour (colour);
    const auto start = std::fmod (offset, 2.0f * barWidth) - 2.0f * barWidth;
    for (auto x = area.getX() + start; x < area.getRight(); x += 2.0f * barWidth)
        g.fillRect (juce::Rectangle<float> (x, area.getY(), barWidth, area.getHeight()));
    g.restoreState();
}

void TunerDisplay::paint (juce::Graphics& g)
{
    g.fillAll (background);   // covers the amp row
    auto area = getLocalBounds().reduced (0, 18);

    g.setColour (fam::recess);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    area.reduce (14, 10);

    const bool hasNote = shownNote >= 0;
    const bool inTune = hasNote && std::abs (shownCents) < inTuneCents;

    auto noteArea = area.removeFromLeft (noteWidth);
    auto centsArea = area.removeFromLeft (centsWidth);
    auto statusArea = area.removeFromRight (statusWidth);

    juce::String noteName = juce::String::fromUTF8 ("\xe2\x80\x93");   // en dash
    if (hasNote)
    {
        const auto n = fx::Note::fromHz (440.0 * std::exp2 ((shownNote - 69) / 12.0));
        noteName = juce::String (std::string (n.name)) + juce::String (n.octave);
    }
    g.setColour (inTune ? fam::green : (hasNote ? fam::cream : fam::dimText));
    g.setFont (fam::font (static_cast<float> (noteArea.getHeight()) * 0.75f, true));
    g.drawFittedText (noteName, noteArea, juce::Justification::centredLeft, 1, 0.6f);

    g.setFont (fam::font (15.0f));
    g.setColour (inTune ? fam::green : fam::cream);
    if (hasNote)
        g.drawText (inTune ? juce::String ("In tune") : (shownCents > 0.0 ? "+" : "") + juce::String (shownCents, 1) + " cents",
                    centsArea, juce::Justification::centredLeft);

    g.setColour (fam::dimText);
    g.setFont (fam::font (13.0f));
    g.drawText ("Output muted", statusArea.withTrimmedLeft (10), juce::Justification::centredRight);

    // The strobe: two bands, the lower twice as fine and twice as fast
    const auto strobe = area.toFloat();
    const auto colour = hasNote ? (inTune ? fam::green : fam::amber) : fam::widget;
    g.setColour (juce::Colours::black);
    g.fillRect (strobe);
    const auto offset = static_cast<float> (strobePhase * 2.0 * barWidthPixels);
    paintBand (g, strobe.withHeight (strobe.getHeight() / 2.0f).reduced (0.0f, 2.0f), barWidthPixels, offset, colour);
    paintBand (g, strobe.withTrimmedTop (strobe.getHeight() / 2.0f).reduced (0.0f, 2.0f), barWidthPixels / 2.0f, offset * 2.0f, colour.withAlpha (0.7f));
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
