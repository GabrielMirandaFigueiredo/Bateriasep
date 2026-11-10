#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

namespace ui
{
const juce::Colour bg       (0xff14161b);
const juce::Colour panel    (0xff1e2128);
const juce::Colour panel2   (0xff272b35);
const juce::Colour textMain (0xffe8eaee);
const juce::Colour textDim  (0xff8d95a3);
const juce::Colour accent   (0xff4fd1c5);
const juce::Colour amber    (0xfff2b84b);
const juce::Colour red      (0xffe5645a);
const juce::Colour stemColours[6] = { juce::Colour (0xffe5645a), juce::Colour (0xfff2b84b), juce::Colour (0xff9b7be8),
                                      juce::Colour (0xff4fd1c5), juce::Colour (0xff6aa8f5), juce::Colour (0xff7fd36b) };
}

// -------------------------------------------------------------------------------------------------
class BsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BsLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
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

    void refresh (const std::shared_ptr<const bateriasep::StemData>& stems, bool hasFiles);
    void paint (juce::Graphics&) override;
    void resized() override;
    DragHandle& getDragHandle() { return drag; }

private:
    BateriaSepProcessor& proc;
    const int index;
    juce::Slider fader;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" };
    juce::Label dbLabel;
    DragHandle drag;
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

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void chooseAudioFile();
    void chooseModelFile();
    void chooseExportFolder();
    void flash (const juce::String& text);
    juce::String formatTime (double seconds) const;

    BateriaSepProcessor& proc;
    BsLookAndFeel laf;

    juce::TextButton loadBtn { "Carregar bateria..." }, cancelBtn { "Cancelar" }, modelBtn { "Localizar modelo..." };
    juce::ComboBox qualityBox;
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

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt, origAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt;

    juce::Rectangle<int> playerArea, mixerArea;
    std::shared_ptr<const bateriasep::SourceData> shownSource;
    std::shared_ptr<const bateriasep::StemData> shownStems;
    double bpmFactor = 1.0;
    juce::String flashText;
    double flashUntil = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BateriaSepEditor)
};
