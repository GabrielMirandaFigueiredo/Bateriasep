#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <mutex>

#include "Project.h"

class BateriaSepProcessor : public juce::AudioProcessor
{
public:
    BateriaSepProcessor();
    ~BateriaSepProcessor() override;

    // --- AudioProcessor ---------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BateriaSep"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Parâmetros (faders, mute, solo, etc.) ---------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    static juce::String gainId (int stem) { return "g" + juce::String (stem); }   // 0..5, 6 = master ("gm")
    static juce::String muteId (int stem) { return "m" + juce::String (stem); }
    static juce::String soloId (int stem) { return "s" + juce::String (stem); }

    // --- Player ------------------------------------------------------------------------------------
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return playing.load(); }
    void seekSeconds (double seconds);
    double getPositionSeconds() const;
    double getDurationSeconds() const;
    void setSelection (double startSec, double endSec);
    void clearSelection() { selStart = -1.0; selEnd = -1.0; }
    bool hasSelection() const { return selStart.load() >= 0.0 && selEnd.load() > selStart.load(); }
    double getSelectionStart() const { return selStart.load(); }
    double getSelectionEnd() const { return selEnd.load(); }

    float getMeter (int index) const { return meters[(size_t) index].load(); }   // 0..5 stems, 6 = master (linear)

    // --- Dados ---------------------------------------------------------------------------------------
    std::shared_ptr<const bateriasep::SourceData> getSource() const;
    std::shared_ptr<const bateriasep::StemData> getStems() const;

    // --- Carregar / separar -------------------------------------------------------------------------
    void loadFile (const juce::File& file);
    void cancelLoad();
    bool isBusy() const;
    float getProgress() const { return progress.load(); }
    juce::String getStatus (bool* isError = nullptr) const;

    // --- Exportar -----------------------------------------------------------------------------------
    // format: 0 = WAV 32 bits float (sem alterar), 1 = WAV 24 bits (mesmo ganho de segurança em todos)
    bool exportStems (const juce::File& folder, int format, juce::String& message) const;
    juce::StringArray getStemPaths() const;   // arquivos prontos para arrastar

    // --- Modelo -------------------------------------------------------------------------------------
    juce::File findModelFile() const;
    void setModelFile (const juce::File& f);
    static juce::File getUserDataDir();

private:
    struct Loader;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void publish (std::shared_ptr<const bateriasep::SourceData> s,
                  std::shared_ptr<const bateriasep::StemData> d, bool resetTransport);
    void rebuildPlaybackCopies (double rate);
    bool ensureEngine (std::string& error);
    juce::File findOrtLibrary() const;
    void setStatus (const juce::String& text, bool error = false);

    // dados compartilhados com a thread de áudio (protegidos por 'lock')
    mutable juce::SpinLock lock;
    std::shared_ptr<const bateriasep::SourceData> source;
    std::shared_ptr<const bateriasep::StemData> stems;

    // transporte
    std::atomic<bool> playing { false };
    std::atomic<int64_t> playPos { 0 };        // em amostras da taxa do host
    std::atomic<int64_t> pendingSeek { -1 };
    std::atomic<double> selStart { -1.0 }, selEnd { -1.0 };
    std::atomic<double> hostRate { 44100.0 };

    std::array<std::atomic<float>, 7> meters;
    std::array<juce::SmoothedValue<float>, 7> smoothGain;

    std::array<std::atomic<float>*, 7> gainParams {};
    std::array<std::atomic<float>*, 6> muteParams {}, soloParams {};
    std::atomic<float>* origParam = nullptr;
    std::atomic<float>* loopParam = nullptr;
    std::atomic<float>* qualityParam = nullptr;

    // separação
    std::mutex engineMutex;
    bateriasep::DrumSeparator engine;
    std::unique_ptr<Loader> loader;
    std::atomic<float> progress { 0.0f };
    mutable juce::CriticalSection statusLock;
    juce::String statusText;
    bool statusIsError = false;
    juce::String pendingRestoreSource, pendingRestoreFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BateriaSepProcessor)
};
