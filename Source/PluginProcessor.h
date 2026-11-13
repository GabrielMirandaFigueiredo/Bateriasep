#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <map>
#include <mutex>

#include "MidiExtractor.h"
#include "Project.h"
#include "TriggerEngine.h"

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
    static juce::String trigModeId (int stem) { return "tm" + juce::String (stem); }    // 0 Original, 1 Somar, 2 Substituir
    static juce::String trigLevelId (int stem) { return "tl" + juce::String (stem); }   // nível do sample (dB)

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
    // Se "Exportar com triggers" estiver ligado, as peças em modo Somar/Substituir saem com o sample aplicado.
    bool exportStems (const juce::File& folder, int format, juce::String& message);
    juce::StringArray getStemPaths();   // arquivos prontos para arrastar

    // --- Triggers (samples tocados nos golpes detectados) ---------------------------------------------
    bool setTriggerSamples (int piece, const juce::StringArray& files, juce::String& message);   // samples do usuário (1 arquivo = 1 intensidade, do suave ao forte)
    void useDefaultTriggerKit (int piece);
    juce::String getTriggerSourceLabel (int piece) const;
    void rebuildTriggerPlan();
    bool isTriggerExportOn() const { return trigExportParam != nullptr && trigExportParam->load() > 0.5f; }
    bool anyTriggerOn() const;              // alguma peça em Somar/Substituir
    bool triggerPlanReady() const;          // o plano de triggers já está pronto para tocar

    // --- MIDI (notas e velocity extraídas de cada peça) ---------------------------------------------
    void setMidiTempo (double bpm) { midiBpm = bpm; }
    bool refreshMidi();                      // (re)gera os arquivos .mid se algo mudou; true se há arquivos
    juce::StringArray getMidiPaths();        // um .mid por peça (6)
    juce::String getMidiCombinedPath();      // um único .mid com todas as peças
    juce::String getMidiSummary() const;     // "Kick 47 | Snare 65 ..." (do último MIDI gerado)
    bool exportMidi (const juce::File& folder, juce::String& message);

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
    std::atomic<float>* midiSensParam = nullptr;
    std::array<std::atomic<float>*, 6> trigModeParams {}, trigLevelParams {};
    std::atomic<float>* trigExportParam = nullptr;

    // cache do MIDI gerado
    struct MidiCache
    {
        std::weak_ptr<const bateriasep::StemData> stems;
        float sens = -1.0f;
        double bpm = -1.0;
        std::array<int, 6> counts {};
        juce::StringArray stemFiles;
        juce::String combinedFile;
        std::shared_ptr<const std::array<std::vector<bateriasep::MidiHit>, 6>> hits;
        bool valid = false;
    };
    mutable std::mutex midiMutex;
    MidiCache midiCache;
    std::atomic<double> midiBpm { 0.0 };

    // triggers
    struct TrigSrcVoice { std::vector<std::vector<bateriasep::TrigSamplePtr>> layers; };
    struct TrigPieceSource
    {
        std::map<int, TrigSrcVoice> byNote;   // nota MIDI -> samples (-1 = qualquer nota)
        double rate = 44100.0;
        juce::String label;
        juce::StringArray files;              // vazio = kit padrão
    uint64_t serial = 0;
    };
    std::shared_ptr<const bateriasep::TrigPiece> getTrigPiece (int piece, double rate);
    std::shared_ptr<bateriasep::TrigPlan> makeTrigPlan (double rate, const std::shared_ptr<const bateriasep::StemData>& sd);
    void installPlan();
    bool refreshMidiImpl (bool& recomputed);
    std::shared_ptr<const TrigPieceSource> getSourceFor (int piece);   // personalizado ou kit padrão
    // stems com os triggers aplicados (só as peças em Somar/Substituir); changed = algum foi alterado
    std::vector<bateriasep::AudioPtr> effectiveStems (const std::shared_ptr<const bateriasep::StemData>& sd, bool& changed);
    mutable std::mutex trigMutex;
    std::array<std::shared_ptr<const TrigPieceSource>, 6> trigSources;
    std::map<std::pair<const TrigPieceSource*, long>, std::shared_ptr<const bateriasep::TrigPiece>> trigPieceCache;
    std::shared_ptr<const bateriasep::TrigPlan> playPlan;   // protegido por 'lock'
    bateriasep::TrigEngine trigEngine;                      // só a thread de áudio mexe (dentro de 'lock')
    std::mutex trigBuildMutex;
    std::array<std::shared_ptr<const TrigPieceSource>, 6> defaultSources;
    std::weak_ptr<const bateriasep::StemData> planStems;    // para quais stems o plano vale (protegido por 'lock')
    juce::String trigExportSig;
    std::atomic<uint64_t> sourceCounter { 0 };
    uint64_t enginePlanId = 0;
    std::atomic<uint64_t> planCounter { 0 };

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
