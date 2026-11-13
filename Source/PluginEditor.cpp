#include "PluginEditor.h"

using namespace bateriasep;

namespace
{
float dbToPos (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); }   // -60..+6 dB -> 0..1

juce::String dbText (float db) { return db <= -59.9f ? juce::String (juce::CharPointer_UTF8 ("-\xE2\x88\x9E dB")) : juce::String (db, 1) + " dB"; }
}

// =================================================================================================
BsLookAndFeel::BsLookAndFeel()
{
    setColour (juce::TextButton::buttonColourId, ui::panel2);
    setColour (juce::TextButton::buttonOnColourId, ui::accent);
    setColour (juce::TextButton::textColourOffId, ui::textMain);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ToggleButton::textColourId, ui::textMain);
    setColour (juce::ToggleButton::tickColourId, ui::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, ui::textDim);
    setColour (juce::ComboBox::backgroundColourId, ui::panel2);
    setColour (juce::ComboBox::textColourId, ui::textMain);
    setColour (juce::ComboBox::outlineColourId, ui::panel2.brighter (0.2f));
    setColour (juce::PopupMenu::backgroundColourId, ui::panel2);
    setColour (juce::PopupMenu::textColourId, ui::textMain);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::ProgressBar::backgroundColourId, ui::bg);
    setColour (juce::ProgressBar::foregroundColourId, ui::accent);
    setColour (juce::Label::textColourId, ui::textMain);
}

void BsLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                                      juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
        return;
    }

    const float cx = (float) x + (float) w * 0.5f;
    g.setColour (juce::Colour (0xff0b0c0f));
    g.fillRoundedRectangle (cx - 3.0f, (float) y, 6.0f, (float) h, 3.0f);

    // marca de 0 dB
    const float zeroY = (float) y + (float) h - dbToPos (0.0f) * (float) h;
    g.setColour (ui::textDim.withAlpha (0.5f));
    g.fillRect (cx - 12.0f, zeroY - 0.5f, 24.0f, 1.0f);

    g.setColour (ui::accent.withAlpha (0.55f));
    g.fillRoundedRectangle (cx - 3.0f, sliderPos, 6.0f, (float) (y + h) - sliderPos, 3.0f);

    juce::Rectangle<float> t (cx - 19.0f, sliderPos - 10.0f, 38.0f, 20.0f);
    g.setColour (juce::Colour (0xffdfe3ea));
    g.fillRoundedRectangle (t, 4.0f);
    g.setColour (juce::Colour (0xff2a2e37));
    g.fillRect (t.getX() + 5.0f, t.getCentreY() - 1.0f, t.getWidth() - 10.0f, 2.0f);
}

// =================================================================================================
void DragHandle::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (active ? colour.withAlpha (0.18f) : ui::panel2.withAlpha (0.5f));
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour (active ? colour : ui::textDim.withAlpha (0.4f));
    g.drawRoundedRectangle (b, 5.0f, 1.0f);

    // "pegador" de pontinhos
    const float cy = b.getCentreY();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (b.getX() + 8.0f + (float) i * 4.0f, cy - 3.0f + (float) j * 5.0f, 2.2f, 2.2f);

    g.setColour (active ? ui::textMain : ui::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (text, getLocalBounds().withTrimmedLeft (26), juce::Justification::centredLeft, true);
}

void DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! active || dragging || e.getDistanceFromDragStart() < 6 || ! getFiles) return;
    const auto files = getFiles();
    if (files.isEmpty()) return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this, [this] { dragging = false; });
}

// =================================================================================================
double WaveformView::xToSeconds (float x) const
{
    const double dur = proc.getDurationSeconds();
    return juce::jlimit (0.0, dur, (double) x / (double) std::max (1, getWidth()) * dur);
}

void WaveformView::mouseDown (const juce::MouseEvent& e) { downX = e.x; selecting = false; }

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (proc.getSource() == nullptr) return;
    if (std::abs (e.x - downX) > 3) selecting = true;
    if (selecting) proc.setSelection (xToSeconds ((float) downX), xToSeconds ((float) e.x));
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (! selecting && proc.getSource() != nullptr) proc.seekSeconds (xToSeconds ((float) e.x));
    selecting = false;
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0f1115));
    g.fillRoundedRectangle (b, 6.0f);

    auto src = proc.getSource();
    if (src == nullptr)
    {
        g.setColour (ui::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Clique em \"Carregar bateria...\" ou arraste um arquivo de áudio para cá"_u8, getLocalBounds(), juce::Justification::centred);
        return;
    }

    const double dur = src->original->seconds();
    const float w = b.getWidth(), h = b.getHeight();
    auto secToX = [&] (double s) { return (float) (s / dur * (double) w); };

    // seleção
    if (proc.hasSelection())
    {
        const float x0 = secToX (proc.getSelectionStart()), x1 = secToX (proc.getSelectionEnd());
        g.setColour (ui::accent.withAlpha (0.30f));
        g.fillRect (x0, 0.0f, x1 - x0, h);
        g.setColour (ui::accent.withAlpha (0.9f));
        g.drawVerticalLine ((int) x0, 0.0f, h);
        g.drawVerticalLine ((int) x1, 0.0f, h);
    }

    // grade de batidas
    if (getBeatPeriod)
    {
        const double per = getBeatPeriod();
        if (per > 0.05)
        {
            const double first = getFirstBeat ? getFirstBeat() : 0.0;
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            for (double t = std::fmod (first, per); t < dur; t += per)
                g.drawVerticalLine ((int) secToX (t), 0.0f, h);
        }
    }

    // forma de onda
    const auto& pk = src->peaks;
    const int nb = (int) pk.size() / 2;
    float maxPk = 0.05f;
    for (float v : pk) maxPk = std::max (maxPk, std::abs (v));
    const float mid = h * 0.5f, scale = mid * 0.9f / maxPk;
    g.setColour (ui::accent.withAlpha (0.85f));
    const int wi = std::max (1, (int) w);
    for (int x = 0; x < wi; ++x)
    {
        const int i0 = x * nb / wi, i1 = std::max (i0 + 1, (x + 1) * nb / wi);
        float mn = 0.0f, mx = 0.0f;
        for (int i = i0; i < i1 && i < nb; ++i) { mn = std::min (mn, pk[(size_t) i * 2]); mx = std::max (mx, pk[(size_t) i * 2 + 1]); }
        g.drawVerticalLine (x, mid - mx * scale - 0.5f, mid - mn * scale + 0.5f);
    }

    // régua de tempo
    static const double steps[] = { 1, 2, 5, 10, 15, 30, 60, 120 };
    double step = steps[7];
    for (double s : steps) if (secToX (s) >= 80.0f) { step = s; break; }
    g.setFont (juce::FontOptions (11.0f));
    for (double t = 0.0; t < dur; t += step)
    {
        const float x = secToX (t);
        g.setColour (ui::textDim.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, 0.0f, 6.0f);
        g.drawText (juce::String ((int) t / 60) + ":" + juce::String ((int) t % 60).paddedLeft ('0', 2),
                    juce::Rectangle<float> (x + 3.0f, 0.0f, 50.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    // cabeça de reprodução
    const float px = secToX (proc.getPositionSeconds());
    g.setColour (juce::Colours::white);
    g.drawVerticalLine ((int) px, 0.0f, h);
    juce::Path tri;
    tri.addTriangle (px - 5.0f, 0.0f, px + 5.0f, 0.0f, px, 7.0f);
    g.fillPath (tri);
}

// =================================================================================================
StemStrip::StemStrip (BateriaSepProcessor& p, int stemIndex) : proc (p), index (stemIndex)
{
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (fader);
    faderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BateriaSepProcessor::gainId (index), fader);

    dbLabel.setJustificationType (juce::Justification::centred);
    dbLabel.setFont (juce::FontOptions (12.0f));
    dbLabel.setColour (juce::Label::textColourId, ui::textDim);
    addAndMakeVisible (dbLabel);

    if (index < kNumStems)
    {
        muteBtn.setClickingTogglesState (true);
        soloBtn.setClickingTogglesState (true);
        muteBtn.setColour (juce::TextButton::buttonOnColourId, ui::red);
        soloBtn.setColour (juce::TextButton::buttonOnColourId, ui::amber);
        addAndMakeVisible (muteBtn);
        addAndMakeVisible (soloBtn);
        muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, BateriaSepProcessor::muteId (index), muteBtn);
        soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, BateriaSepProcessor::soloId (index), soloBtn);

        drag.text = "Arrastar p/ DAW";
        drag.colour = ui::stemColours[index];
        drag.getFiles = [this]
        {
            juce::StringArray f;
            auto src = proc.getSource();
            auto sd = proc.getStems();
            if (src != nullptr && sd != nullptr)
            {
                const auto file = sd->folder.getChildFile (stemFileName (src->name, index));
                if (file.existsAsFile()) f.add (file.getFullPathName());
            }
            return f;
        };
        addAndMakeVisible (drag);

        trigMode.addItemList ({ "Original", "Somar sample", "Substituir" }, 1);
        trigMode.setTooltip ("Original: só o som gravado. Somar: o sample toca junto com o som gravado. Substituir: só o sample toca (o som gravado é silenciado).");
        addAndMakeVisible (trigMode);
        trigModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, BateriaSepProcessor::trigModeId (index), trigMode);

        trigSamplesBtn.setTooltip ("Escolher o kit padrão ou importar seus próprios samples");
        trigSamplesBtn.onClick = [this] { showSamplesMenu(); };
        addAndMakeVisible (trigSamplesBtn);

        trigLevel.setSliderStyle (juce::Slider::LinearHorizontal);
        trigLevel.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
        trigLevel.setTextValueSuffix (" dB");
        trigLevel.setDoubleClickReturnValue (true, 0.0);
        trigLevel.setTooltip ("Volume do sample (a intensidade de cada golpe continua sendo respeitada)");
        addAndMakeVisible (trigLevel);
        trigLevelAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BateriaSepProcessor::trigLevelId (index), trigLevel);
    }
    else
    {
        trigExportBtn.setButtonText ("Exportar com triggers");
        trigExportBtn.setTooltip ("Ligado: \"Exportar\" e \"Arrastar\" usam o som dos triggers nas peças em Somar/Substituir");
        addAndMakeVisible (trigExportBtn);
        trigExportAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "trigExport", trigExportBtn);
    }
}

void StemStrip::showSamplesMenu()
{
    juce::PopupMenu m;
    const bool custom = shownSourceLabel.isNotEmpty() && ! shownSourceLabel.startsWith ("Kit");
    m.addItem (1, "Usar o kit padrão (Big Rusty Drums)"_u8, true, ! custom);
    m.addItem (2, "Importar meus samples..."_u8);
    m.addSeparator();
    m.addItem (3, "Dica: 1 arquivo = 1 intensidade. Com vários, use nomes em ordem (do suave ao forte)."_u8, false);
    juce::Component::SafePointer<StemStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&trigSamplesBtn), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)
        {
            juce::Thread::launch ([safe]
            {
                if (safe == nullptr) return;
                auto& pr = safe->proc;
                pr.useDefaultTriggerKit (safe->index);
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onMessage) safe->onMessage ("Kit padrão restaurado."_u8); });
            });
        }
        else if (r == 2) safe->importSamples();
    });
}

void StemStrip::importSamples()
{
    sampleChooser = std::make_unique<juce::FileChooser> ("Escolha os samples (um ou vários)"_u8,
                                                         juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                         "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<StemStrip> safe (this);
    sampleChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
                                [safe] (const juce::FileChooser& fc)
                                {
                                    juce::StringArray files;
                                    for (auto& f : fc.getResults()) files.add (f.getFullPathName());
                                    if (safe == nullptr || files.isEmpty()) return;
                                    juce::Thread::launch ([safe, files]
                                    {
                                        if (safe == nullptr) return;
                                        juce::String msg;
                                        safe->proc.setTriggerSamples (safe->index, files, msg);
                                        juce::MessageManager::callAsync ([safe, msg] { if (safe != nullptr && safe->onMessage) safe->onMessage (msg); });
                                    });
                                });
}

void StemStrip::refresh (const std::shared_ptr<const StemData>& stems, bool hasFiles)
{
    if (stems != shown) { shown = stems; repaint(); }
    if (index < kNumStems)
    {
        drag.setActive (hasFiles);
        trigMode.setEnabled (stems != nullptr);
        trigSamplesBtn.setEnabled (stems != nullptr);
        const auto label = proc.getTriggerSourceLabel (index);
        if (label != shownSourceLabel)
        {
            shownSourceLabel = label;
            trigSamplesBtn.setButtonText (label.startsWith ("Kit") ? "Kit padrão  v"_u8 : label + "  v");
        }
    }
    else trigExportBtn.setEnabled (stems != nullptr);

    const float v = proc.getMeter (index);
    if (std::abs (v - meterShown) > 0.002f) { meterShown = v; repaint (meterArea); }

    const float db = (float) fader.getValue();
    const auto t = dbText (db);
    if (dbLabel.getText() != t) dbLabel.setText (t, juce::dontSendNotification);
}

void StemStrip::resized()
{
    auto r = getLocalBounds().reduced (6);
    r.removeFromTop (24);   // nome
    if (index < kNumStems)
    {
        waveArea = r.removeFromTop (28);
        r.removeFromTop (4);
        drag.setBounds (r.removeFromTop (22));
        r.removeFromTop (6);
        trigMode.setBounds (r.removeFromTop (22));
        r.removeFromTop (3);
        trigSamplesBtn.setBounds (r.removeFromTop (22));
        r.removeFromTop (3);
        trigLevel.setBounds (r.removeFromTop (20));
        r.removeFromTop (6);
        auto ms = r.removeFromBottom (28);
        muteBtn.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0));
        soloBtn.setBounds (ms.reduced (2, 0));
        r.removeFromBottom (4);
    }
    else
    {
        r.removeFromBottom (32);
        // mesmo espaço dos controles de trigger, para os faders ficarem alinhados
        r.removeFromTop (28 + 4 + 22 + 6);
        trigExportBtn.setBounds (r.removeFromTop (22));
        r.removeFromTop (3 + 22 + 3 + 20 + 6);
    }
    dbLabel.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);

    auto faderArea = r.removeFromLeft (r.getWidth() * 62 / 100);
    fader.setBounds (faderArea);
    meterArea = r.withSizeKeepingCentre (10, r.getHeight() - 20);
}

void StemStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::panel2);
    g.fillRoundedRectangle (b, 8.0f);

    const auto colour = index < kNumStems ? ui::stemColours[index] : ui::accent;
    g.setColour (colour);
    g.fillRoundedRectangle (b.getX(), b.getY(), b.getWidth(), 4.0f, 2.0f);

    g.setColour (ui::textMain);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (index < kNumStems ? juce::String (stemName (index)) : juce::String ("Master"),
                getLocalBounds().withHeight (28).withTrimmedTop (6).reduced (8, 0), juce::Justification::centredLeft);

    if (index < kNumStems)
    {
        g.setColour (juce::Colour (0xff0f1115));
        g.fillRoundedRectangle (waveArea.toFloat(), 4.0f);
        if (shown != nullptr && (size_t) index < shown->peaks.size())
        {
            const auto& pk = shown->peaks[(size_t) index];
            const int nb = (int) pk.size() / 2;
            float maxPk = 0.02f;
            for (float v : pk) maxPk = std::max (maxPk, std::abs (v));
            const float mid = waveArea.getCentreY(), sc = waveArea.getHeight() * 0.45f / maxPk;
            g.setColour (colour.withAlpha (0.9f));
            for (int x = 0; x < waveArea.getWidth(); ++x)
            {
                const int i = x * nb / waveArea.getWidth();
                g.drawVerticalLine (waveArea.getX() + x, mid - pk[(size_t) i * 2 + 1] * sc - 0.5f, mid - pk[(size_t) i * 2] * sc + 0.5f);
            }
        }
        else
        {
            g.setColour (ui::textDim.withAlpha (0.4f));
            g.drawHorizontalLine (waveArea.getCentreY(), (float) waveArea.getX() + 4.0f, (float) waveArea.getRight() - 4.0f);
        }
    }

    // medidor
    g.setColour (juce::Colour (0xff0b0c0f));
    g.fillRoundedRectangle (meterArea.toFloat(), 3.0f);
    const float level = dbToPos (juce::Decibels::gainToDecibels (meterShown, -60.0f));
    const float fh = level * (float) meterArea.getHeight();
    g.setColour (meterShown > 1.0f ? ui::red : colour);
    g.fillRoundedRectangle ((float) meterArea.getX(), (float) meterArea.getBottom() - fh, (float) meterArea.getWidth(), fh, 3.0f);
}

// =================================================================================================
BateriaSepEditor::BateriaSepEditor (BateriaSepProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), wave (p)
{
    setLookAndFeel (&laf);
    loopBtn.setButtonText ("Repetir seleção"_u8);
    origBtn.setButtonText ("Ouvir original");
    gridBtn.setButtonText ("Grade de batidas");
    halfBtn.setButtonText ("÷2"_u8);
    doubleBtn.setButtonText ("×2"_u8);
    clearSelBtn.setButtonText ("Limpar seleção"_u8);

    for (auto* b : { &loadBtn, &cancelBtn, &modelBtn, &playBtn, &pauseBtn, &stopBtn, &halfBtn, &doubleBtn, &clearSelBtn, &exportBtn, &openFolderBtn })
        addAndMakeVisible (b);
    for (auto* b : { &loopBtn, &origBtn, &gridBtn })
        addAndMakeVisible (b);
    for (auto* l : { &timeLabel, &infoLabel, &bpmLabel, &statusLabel, &hintLabel })
        addAndMakeVisible (l);
    addAndMakeVisible (qualityBox);
    addAndMakeVisible (formatBox);
    addAndMakeVisible (progressBar);
    addAndMakeVisible (wave);
    addAndMakeVisible (dragAll);
    for (auto* b : { &exportMidiBtn }) addAndMakeVisible (b);
    addAndMakeVisible (midiSensSlider);
    addAndMakeVisible (midiSensLabel);
    addAndMakeVisible (midiInfoLabel);
    addAndMakeVisible (dragMidi);
    addAndMakeVisible (dragMidiOne);

    for (int i = 0; i < 7; ++i)
    {
        strips.add (new StemStrip (proc, i));
        strips.getLast()->onMessage = [this] (const juce::String& t) { flash (t); };
        addAndMakeVisible (strips.getLast());
    }

    qualityBox.addItemList ({ "Qualidade: Rápida"_u8, "Qualidade: Padrão"_u8 }, 1);
    qualityAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "quality", qualityBox);

    formatBox.addItemList ({ "WAV 32 bits float (sem alterar)", "WAV 24 bits (ganho de segurança)"_u8 }, 1);
    formatBox.setSelectedItemIndex (0, juce::dontSendNotification);

    loopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "loop", loopBtn);
    origAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "orig", origBtn);
    gridBtn.setToggleState (true, juce::dontSendNotification);

    timeLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    timeLabel.setJustificationType (juce::Justification::centredLeft);
    infoLabel.setColour (juce::Label::textColourId, ui::textDim);
    infoLabel.setFont (juce::FontOptions (13.0f));
    bpmLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, ui::textDim);
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setColour (juce::Label::textColourId, ui::textDim);
    hintLabel.setText ("Clique na onda para ir a um ponto  -  arraste para selecionar um trecho  -  duplo clique limpa a seleção"_u8,
                       juce::dontSendNotification);
    progressBar.setPercentageDisplay (false);

    dragAll.text = "Arrastar todos os stems para a DAW";
    dragAll.getFiles = [this] { return proc.getStemPaths(); };

    // --- MIDI ---
    midiSensLabel.setText ("Sensibilidade do MIDI", juce::dontSendNotification);
    midiSensLabel.setFont (juce::FontOptions (12.0f));
    midiSensLabel.setColour (juce::Label::textColourId, ui::textDim);
    midiInfoLabel.setFont (juce::FontOptions (11.5f));
    midiInfoLabel.setMinimumHorizontalScale (1.0f);
    midiInfoLabel.setColour (juce::Label::textColourId, ui::textDim);
    midiInfoLabel.setJustificationType (juce::Justification::centredLeft);
    midiSensSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    midiSensSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    midiSensSlider.setColour (juce::Slider::trackColourId, ui::accent.withAlpha (0.6f));
    midiSensSlider.setColour (juce::Slider::thumbColourId, ui::textMain);
    midiSensSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff0b0c0f));
    midiSensSlider.setTooltip ("Mais à direita = mais notas (inclusive as bem fracas). Mais à esquerda = só os golpes fortes.");
    midiSensAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "midiSens", midiSensSlider);
    dragMidi.text = "Arrastar stems MIDI";
    dragMidi.colour = ui::amber;
    dragMidi.getFiles = [this] { return proc.getMidiPaths(); };
    dragMidiOne.text = "Arrastar MIDI único"_u8;
    dragMidiOne.colour = ui::amber;
    dragMidiOne.getFiles = [this]
    {
        juce::StringArray a;
        const auto p = proc.getMidiCombinedPath();
        if (p.isNotEmpty()) a.add (p);
        return a;
    };
    exportMidiBtn.onClick = [this] { chooseMidiFolder(); };

    wave.getBeatPeriod = [this]
    {
        if (! gridBtn.getToggleState() || shownStems == nullptr || ! shownStems->bpm.valid) return 0.0;
        return 60.0 / (shownStems->bpm.bpm * bpmFactor);
    };
    wave.getFirstBeat = [this] { return shownStems != nullptr ? shownStems->bpm.firstBeatSec : 0.0; };

    loadBtn.onClick = [this] { chooseAudioFile(); };
    cancelBtn.onClick = [this] { proc.cancelLoad(); };
    modelBtn.onClick = [this] { chooseModelFile(); };
    playBtn.onClick = [this] { proc.play(); };
    pauseBtn.onClick = [this] { proc.pause(); };
    stopBtn.onClick = [this] { proc.stop(); };
    clearSelBtn.onClick = [this] { proc.clearSelection(); };
    halfBtn.onClick = [this] { bpmFactor = std::max (0.25, bpmFactor * 0.5); };
    doubleBtn.onClick = [this] { bpmFactor = std::min (4.0, bpmFactor * 2.0); };
    exportBtn.onClick = [this] { chooseExportFolder(); };
    openFolderBtn.onClick = [this]
    {
        if (auto sd = proc.getStems()) if (sd->folder.isDirectory()) sd->folder.revealToUser();
    };

    setSize (1060, 880);   // por último: resized() precisa que todos os componentes já existam
    startTimerHz (30);
    timerCallback();
}

BateriaSepEditor::~BateriaSepEditor() { setLookAndFeel (nullptr); }

void BateriaSepEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    g.setColour (ui::textMain);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("BateriaSep", 18, 12, 220, 32, juce::Justification::centredLeft);
    g.setColour (ui::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Separador de bateria com IA", 150, 20, 260, 20, juce::Justification::centredLeft);

    g.setColour (ui::panel);
    g.fillRoundedRectangle (playerArea.toFloat(), 10.0f);
    g.fillRoundedRectangle (mixerArea.toFloat(), 10.0f);
    g.setColour (ui::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("STEMS", mixerArea.getX() + 12, mixerArea.getY() + 6, 100, 16, juce::Justification::centredLeft);
}

void BateriaSepEditor::resized()
{
    if (strips.size() < 7) return;
    auto r = getLocalBounds().reduced (14);

    auto top = r.removeFromTop (40);
    modelBtn.setBounds (top.removeFromRight (150).withTrimmedTop (4).withTrimmedBottom (4));
    top.removeFromRight (8);
    qualityBox.setBounds (top.removeFromRight (170).withTrimmedTop (4).withTrimmedBottom (4));
    top.removeFromRight (8);
    cancelBtn.setBounds (top.removeFromRight (90).withTrimmedTop (4).withTrimmedBottom (4));
    top.removeFromRight (8);
    loadBtn.setBounds (top.removeFromRight (180).withTrimmedTop (4).withTrimmedBottom (4));
    r.removeFromTop (6);

    // --- player ---
    playerArea = r.removeFromTop (296);
    auto p = playerArea.reduced (12);
    infoLabel.setBounds (p.removeFromTop (20));
    p.removeFromTop (4);
    wave.setBounds (p.removeFromTop (118));
    p.removeFromTop (4);
    hintLabel.setBounds (p.removeFromTop (16));
    p.removeFromTop (4);

    auto statusRow = p.removeFromTop (22);
    progressBar.setBounds (statusRow.removeFromRight (230));
    statusRow.removeFromRight (10);
    statusLabel.setBounds (statusRow);
    p.removeFromTop (6);

    auto tr = p.removeFromTop (34);
    playBtn.setBounds (tr.removeFromLeft (80));  tr.removeFromLeft (6);
    pauseBtn.setBounds (tr.removeFromLeft (80)); tr.removeFromLeft (6);
    stopBtn.setBounds (tr.removeFromLeft (80));  tr.removeFromLeft (14);
    timeLabel.setBounds (tr.removeFromLeft (170));
    bpmLabel.setBounds (tr.removeFromRight (360));
    doubleBtn.setBounds (tr.removeFromRight (40).reduced (0, 3));  tr.removeFromRight (4);
    halfBtn.setBounds (tr.removeFromRight (40).reduced (0, 3));

    p.removeFromTop (6);
    auto opts = p.removeFromTop (26);
    loopBtn.setBounds (opts.removeFromLeft (150));
    origBtn.setBounds (opts.removeFromLeft (150));
    gridBtn.setBounds (opts.removeFromLeft (160));
    clearSelBtn.setBounds (opts.removeFromRight (130));

    r.removeFromTop (8);

    // --- rodapé (linha do MIDI, embaixo; depois a linha dos stems) ---
    auto footerMidi = r.removeFromBottom (44);
    exportMidiBtn.setBounds (footerMidi.removeFromLeft (150).reduced (0, 5));
    footerMidi.removeFromLeft (14);
    midiSensLabel.setBounds (footerMidi.removeFromLeft (128).reduced (0, 5));
    midiSensSlider.setBounds (footerMidi.removeFromLeft (110).reduced (0, 8));
    footerMidi.removeFromLeft (12);
    dragMidiOne.setBounds (footerMidi.removeFromRight (172).reduced (0, 5));
    footerMidi.removeFromRight (8);
    dragMidi.setBounds (footerMidi.removeFromRight (172).reduced (0, 5));
    footerMidi.removeFromRight (10);
    midiInfoLabel.setBounds (footerMidi.reduced (0, 1));
    r.removeFromBottom (2);

    auto footer = r.removeFromBottom (44);
    openFolderBtn.setBounds (footer.removeFromRight (170).reduced (0, 5));
    footer.removeFromRight (8);
    dragAll.setBounds (footer.removeFromRight (290).reduced (0, 5));
    footer.removeFromRight (8);
    exportBtn.setBounds (footer.removeFromLeft (210).reduced (0, 5));
    footer.removeFromLeft (8);
    formatBox.setBounds (footer.removeFromLeft (240).reduced (0, 5));
    r.removeFromBottom (6);

    // --- mixer ---
    mixerArea = r;
    auto m = mixerArea.reduced (10);
    m.removeFromTop (14);
    const int gap = 8;
    const int w = (m.getWidth() - gap * 6) / 7;
    for (int i = 0; i < 7; ++i)
    {
        strips[i]->setBounds (m.removeFromLeft (w));
        m.removeFromLeft (gap);
    }
}

juce::String BateriaSepEditor::formatTime (double s) const
{
    const int m = (int) s / 60;
    const double sec = s - m * 60;
    return juce::String (m) + ":" + juce::String (sec, 1).paddedLeft ('0', 4);
}

void BateriaSepEditor::flash (const juce::String& text)
{
    flashText = text;
    flashUntil = juce::Time::getMillisecondCounterHiRes() + 9000.0;
}

void BateriaSepEditor::timerCallback()
{
    auto src = proc.getSource();
    auto sd = proc.getStems();
    const bool busy = proc.isBusy();
    const bool hasStems = sd != nullptr;
    const bool hasFiles = hasStems && sd->folder.isDirectory();

    if (src != shownSource)
    {
        if (src == nullptr || shownSource == nullptr || src->file != shownSource->file) bpmFactor = 1.0;
        shownSource = src;
        wave.repaint();
    }
    shownStems = sd;

    // informações do arquivo
    if (src != nullptr)
    {
        infoLabel.setText (src->file.getFileName() + "   -   " + formatTime (src->original->seconds()) + "   -   "
                           + juce::String (src->original->sampleRate / 1000.0, 1) + " kHz   -   "
                           + (src->fileChannels == 1 ? "mono" : "estéreo"_u8), juce::dontSendNotification);
    }
    else
    {
        infoLabel.setText ("Nenhum arquivo carregado", juce::dontSendNotification);
    }

    timeLabel.setText (formatTime (proc.getPositionSeconds()) + " / " + formatTime (proc.getDurationSeconds()), juce::dontSendNotification);

    // BPM
    if (hasStems && sd->bpm.valid)
    {
        const double v = sd->bpm.bpm * bpmFactor;
        bpmLabel.setText ("BPM provável: "_u8 + juce::String (v, 1) + "   (metade " + juce::String ((int) std::lround (v * 0.5)) + " / dobro " + juce::String ((int) std::lround (v * 2.0))
                          + ")   confiança "_u8 + juce::String ((int) (sd->bpm.confidence * 100)) + "%", juce::dontSendNotification);
        bpmLabel.setColour (juce::Label::textColourId, ui::amber);
    }
    else
    {
        bpmLabel.setText (hasStems ? "BPM: não foi possível estimar"_u8 : "BPM: aparece depois da separação"_u8, juce::dontSendNotification);
        bpmLabel.setColour (juce::Label::textColourId, ui::textDim);
    }

    // status
    bool isError = false;
    auto status = proc.getStatus (&isError);
    if (juce::Time::getMillisecondCounterHiRes() < flashUntil) { status = flashText; isError = false; }
    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? ui::red : ui::textDim);
    progressValue = proc.getProgress();
    progressBar.setVisible (busy);
    cancelBtn.setVisible (busy);

    // habilita / desabilita
    loadBtn.setEnabled (! busy);
    for (auto* b : { &playBtn, &pauseBtn, &stopBtn, &clearSelBtn }) b->setEnabled (src != nullptr);
    origBtn.setEnabled (hasStems);
    exportBtn.setEnabled (hasStems);
    openFolderBtn.setEnabled (hasFiles);
    gridBtn.setEnabled (hasStems);
    halfBtn.setEnabled (hasStems);
    doubleBtn.setEnabled (hasStems);
    dragAll.setActive (hasFiles);

    // --- MIDI: informa o BPM exibido e regera os arquivos (com pequeno atraso) quando algo muda ---
    {
        const double bpmShown = (sd != nullptr && sd->bpm.valid) ? sd->bpm.bpm * bpmFactor : 0.0;
        proc.setMidiTempo (bpmShown);
        const float sens = (float) midiSensSlider.getValue();
        const void* key = sd.get();
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (key != midiKeyStems || std::abs (sens - midiKeySens) > 1e-4f || std::abs (bpmShown - midiKeyBpm) > 1e-3)
        {
            midiKeyStems = key; midiKeySens = sens; midiKeyBpm = bpmShown;
            midiRefreshAt = nowMs + 350.0;
        }
        if (hasStems && midiRefreshAt > 0.0 && nowMs >= midiRefreshAt && ! midiBusy)
        {
            midiRefreshAt = 0.0;
            midiBusy = true;
            juce::Component::SafePointer<BateriaSepEditor> safe (this);
            juce::Thread::launch ([safe, this]
            {
                proc.refreshMidi();
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->midiBusy = false; });
            });
        }
        // triggers: monta o plano (notas + samples) quando alguma peça usa trigger e ele ainda não está pronto
        if (hasStems && ! midiBusy && midiRefreshAt == 0.0 && ! trigBusy && nowMs >= trigRetryAt
            && proc.anyTriggerOn() && ! proc.triggerPlanReady())
        {
            trigBusy = true;
            trigRetryAt = nowMs + 3000.0;
            juce::Component::SafePointer<BateriaSepEditor> safe (this);
            juce::Thread::launch ([safe, this]
            {
                proc.rebuildTriggerPlan();
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->trigBusy = false; });
            });
        }
        midiInfoLabel.setText (hasStems ? (midiBusy || midiRefreshAt > 0.0 ? juce::String ("Gerando MIDI...") : proc.getMidiSummary())
                                        : juce::String(), juce::dontSendNotification);
        exportMidiBtn.setEnabled (hasStems);
        midiSensSlider.setEnabled (hasStems);
        dragMidi.setActive (hasStems);
        dragMidiOne.setActive (hasStems);
    }
    playBtn.setToggleState (proc.isPlaying(), juce::dontSendNotification);
    playBtn.setColour (juce::TextButton::buttonColourId, proc.isPlaying() ? ui::accent : ui::panel2);
    playBtn.setColour (juce::TextButton::textColourOffId, proc.isPlaying() ? juce::Colours::black : ui::textMain);

    for (auto* s : strips) s->refresh (sd, hasFiles);
    wave.repaint();
}

// -------------------------------------------------------------------------------------------------
void BateriaSepEditor::chooseAudioFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha o arquivo de bateria",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadFile (f);
                          });
}

void BateriaSepEditor::chooseModelFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Localize o arquivo drumsep_core.onnx", juce::File(), "*.onnx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                              {
                                  proc.setModelFile (f);
                                  flash ("Modelo definido: " + f.getFullPathName() + ". Agora clique em \"Carregar bateria...\".");
                              }
                          });
}

void BateriaSepEditor::chooseExportFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha a pasta onde gravar os stems",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder == juce::File()) return;

                              flash ("Gravando os arquivos...");
                              const int fmt = formatBox.getSelectedItemIndex();
                              juce::Component::SafePointer<BateriaSepEditor> safe (this);
                              juce::Thread::launch ([safe, folder, fmt, this]
                              {
                                  juce::String msg;
                                  proc.exportStems (folder, fmt, msg);
                                  juce::MessageManager::callAsync ([safe, msg] { if (safe != nullptr) safe->flash (msg); });
                              });
                          });
}

void BateriaSepEditor::chooseMidiFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha a pasta onde gravar os arquivos MIDI",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder == juce::File()) return;
                              juce::String msg;
                              proc.exportMidi (folder, msg);
                              flash (msg);
                          });
}

bool BateriaSepEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && ! proc.isBusy();
}

void BateriaSepEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() == 1 && ! proc.isBusy()) proc.loadFile (juce::File (files[0]));
}
