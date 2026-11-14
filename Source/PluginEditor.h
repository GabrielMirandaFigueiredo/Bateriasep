#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

namespace ui
{
// Visuais (skins): 0 = Moderno escuro (padrão), 1 = Mesa SSL 9000 (off-white + laranja, analógico), 2 = Estúdio claro.
// As cores abaixo mudam quando o usuário troca o visual (setSkin).
inline int skin = 0;
inline juce::Colour bg, panel, panel2, ink, textMain, textDim, accent, accentDeep, amber, red;
inline juce::Colour display, displayText, displayDim, led, tick, playhead, onAccent, capColour, wood1, wood2;
inline juce::Colour stemColours[6];

inline bool analog() { return skin == 1; }
inline bool light()  { return skin == 2; }
inline const char* skinName (int i)
{
    static const char* n[3] = { "Moderno escuro", "Mesa SSL 9000", "Est\xc3\xba" "dio claro" };
    return n[i < 0 ? 0 : (i > 2 ? 2 : i)];
}
void setSkin (int index);   // define todas as cores
}

// -------------------------------------------------------------------------------------------------
class BsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BsLookAndFeel();
    void updateColours();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    static void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius);
};

// -------------------------------------------------------------------------------------------------
// Área que o usuário arrasta para a DAW (arquivos reais em disco).
class DragHandle : public juce::Component
{
public:
    std::function<juce::StringArray()> getFiles;
    juce::String text;
    juce::Colour colour = ui::accent;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    void setActive (bool a) { if (a != active) { active = a; repaint(); } }

private:
    bool active = false, dragging = false;
};

// -------------------------------------------------------------------------------------------------
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (BateriaSepProcessor& p) : proc (p) {}

    std::function<double()> getBeatPeriod;   // 0 = sem grade
    std::function<double()> getFirstBeat;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { proc.clearSelection(); }

private:
    double xToSeconds (float x) const;
    BateriaSepProcessor& proc;
    int downX = 0;
    bool selecting = false;
};

// -------------------------------------------------------------------------------------------------
class StemStrip : public juce::Component
{
public:
    // index 0..5 = peças, 6 = master
    StemStrip (BateriaSepProcessor& p, int stemIndex);

    void refresh (const std::shared_ptr<const bateriasep::StemData>& stems, bool hasFiles,
                  const std::shared_ptr<const std::array<std::vector<bateriasep::MidiHit>, 6>>& hits = nullptr);
    void paint (juce::Graphics&) override;
    void resized() override;
    DragHandle& getDragHandle() { return drag; }
    void applySkin();
    std::function<void (const juce::String&)> onMessage;   // avisos para a barra de status

private:
    BateriaSepProcessor& proc;
    const int index;
    juce::Slider fader;
    // detecção (ajuste fino de quando cada golpe dispara)
    juce::Slider kThresh, kAttack, kHold, kReject;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" };
    juce::Label dbLabel;
    DragHandle drag;

    // triggers (só nas peças) e a opção de exportar com triggers (só no master)
    juce::ComboBox trigMode;
    juce::TextButton trigSamplesBtn;
    juce::Slider trigLevel;   // knob do nível do sample
    juce::ToggleButton trigExportBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> trigModeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trigLevelAtt, threshAtt, attackAtt, holdAtt, rejectAtt;
    juce::Rectangle<int> detArea, trigArea, knobLabelArea[5];
    int hitCount = -1;
    std::shared_ptr<const std::array<std::vector<bateriasep::MidiHit>, 6>> shownHits;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> trigExportAtt;
    std::unique_ptr<juce::FileChooser> sampleChooser;
    juce::String shownSourceLabel;
    void showSamplesMenu();
    void importSamples();

    std::shared_ptr<const bateriasep::StemData> shown;
    float meterShown = 0.0f;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt;

    juce::Rectangle<int> meterArea, waveArea;
};

// -------------------------------------------------------------------------------------------------
class BateriaSepEditor : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit BateriaSepEditor (BateriaSepProcessor&);
    ~BateriaSepEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setSkin (int index, bool save = true);   // troca o visual (0 escuro, 1 SSL, 2 claro)

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void chooseAudioFile();
    void applySkin();
    void chooseModelFile();
    void chooseExportFolder();
    void chooseMidiFolder();
    void flash (const juce::String& text);
    juce::String formatTime (double seconds) const;

    BateriaSepProcessor& proc;
    BsLookAndFeel laf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::TextButton loadBtn { "Carregar bateria..." }, cancelBtn { "Cancelar" }, modelBtn { "Localizar modelo..." };
    juce::ComboBox qualityBox, skinBox;
    juce::TextButton playBtn { "Play" }, pauseBtn { "Pause" }, stopBtn { "Stop" };
    juce::ToggleButton loopBtn, origBtn, gridBtn;   // (textos definidos no construtor, em UTF-8)
    juce::TextButton halfBtn, doubleBtn, clearSelBtn;
    juce::Label timeLabel, infoLabel, bpmLabel, statusLabel, hintLabel;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };

    WaveformView wave;
    juce::OwnedArray<StemStrip> strips;

    juce::ComboBox formatBox;
    juce::TextButton exportBtn { "Exportar todos os stems..." }, openFolderBtn { "Abrir pasta dos stems" };
    DragHandle dragAll;

    // MIDI
    juce::Slider midiSensSlider;
    juce::Label midiSensLabel, midiInfoLabel;
    juce::TextButton exportMidiBtn { "Exportar MIDI..." };
    DragHandle dragMidi, dragMidiOne;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> midiSensAtt;
    const void* midiKeyStems = nullptr;
    double midiKeyBpm = -1.0, midiRefreshAt = 0.0;
    uint64_t midiKeySig = 0;
    bool midiBusy = false;
    bool trigBusy = false;
    double trigRetryAt = 0.0;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt, origAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt;

    static constexpr float kCheek = 16.0f;
    juce::Rectangle<int> logoArea, displayArea, controlArea, mixerArea, footerArea;
    std::shared_ptr<const bateriasep::SourceData> shownSource;
    std::shared_ptr<const bateriasep::StemData> shownStems;
    double bpmFactor = 1.0;
    juce::String flashText;
    double flashUntil = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BateriaSepEditor)
};
