#include "PluginProcessor.h"

#include "KitData.h"
#include "OrtLoader.h"
#include "PluginEditor.h"

using namespace bateriasep;

namespace
{
constexpr double kMaxSeconds = 8.0 * 60.0;   // limite de duração do arquivo (memória)
constexpr const char* kModelFileName = "drumsep_core.onnx";
}

// =================================================================================================
// Thread que decodifica, separa e prepara tudo em segundo plano.
struct BateriaSepProcessor::Loader : public juce::Thread
{
    Loader (BateriaSepProcessor& o, juce::File f, juce::File restore, int overlapValue)
        : juce::Thread ("BateriaSep loader"), owner (o), file (std::move (f)),
          restoreFolder (std::move (restore)), overlap (overlapValue) {}

    void run() override
    {
        auto& p = owner;
        p.progress = 0.0f;
        p.setStatus ("Lendo o arquivo...");

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        int channels = 2;
        auto orig = readAudio (file, fm, channels, kMaxSeconds);
        if (orig == nullptr)
        {
            fail ("Não consegui abrir o arquivo (use WAV, AIFF, FLAC, OGG ou MP3, com até 8 minutos)."_u8);
            return;
        }
        if (threadShouldExit()) { fail ("Cancelado."); return; }

        auto src = std::make_shared<SourceData>();
        src->name = safeName (file.getFileNameWithoutExtension());
        src->file = file;
        src->fileChannels = channels;
        src->original = orig;
        src->playRate = p.hostRate.load();
        src->playOriginal = resampled (orig, src->playRate);
        src->peaks = makePeaks (*orig, 1600);
        p.publish (src, nullptr, true);   // já dá para ouvir o original enquanto separa

        if (restoreFolder.isDirectory())
        {
            auto sd = loadStemsFromFolder (*src, fm);
            if (sd != nullptr) { p.publish (finishSource (src), sd, false); if (p.anyTriggerOn()) p.rebuildTriggerPlan(); p.progress = 1.0f; p.setStatus ("Pronto. Stems carregados da pasta do projeto."); return; }
            p.setStatus ("Os stems salvos não foram encontrados. Separando novamente..."_u8);
        }

        std::string err;
        if (! p.ensureEngine (err))
        {
            fail (juce::String::fromUTF8 (err.c_str()));
            return;
        }

        // --- separação -------------------------------------------------------------------------
        p.progress = 0.02f;
        auto in44 = resampled (orig, kModelRate);
        StemSet out;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();

        const bool ok = p.engine.separate (in44->ch[0].data(), in44->ch[1].data(), (size_t) in44->size(), overlap, out,
            [&] (float frac)
            {
                p.progress = 0.02f + 0.93f * frac;
                const double elapsed = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                const int eta = frac > 0.02f ? (int) (elapsed / frac * (1.0 - frac)) : 0;
                p.setStatus ("Separando a bateria...  " + juce::String ((int) (frac * 100)) + "%"
                             + (eta > 0 ? "   (faltam ~" + juce::String (eta / 60) + ":" + juce::String (eta % 60).paddedLeft ('0', 2) + ")" : juce::String()));
                return ! threadShouldExit();
            }, err);

        if (! ok)
        {
            fail (err == "Cancelado." ? juce::String ("Separação cancelada."_u8) : juce::String::fromUTF8 (err.c_str()));
            return;
        }

        // --- monta os stems na taxa do arquivo -----------------------------------------------------
        p.setStatus ("Finalizando...");
        auto sd = std::make_shared<StemData>();
        const int64_t n = orig->size();
        for (int s = 0; s < kNumStems; ++s)
        {
            auto a = std::make_shared<StereoAudio>();
            a->sampleRate = kModelRate;
            a->ch[0] = std::move (out[(size_t) s][0]);
            a->ch[1] = std::move (out[(size_t) s][1]);
            auto r = std::make_shared<StereoAudio> (*resampled (a, orig->sampleRate));   // cópia editável
            r->ch[0].resize ((size_t) n, 0.0f);   // mesmo comprimento do original
            r->ch[1].resize ((size_t) n, 0.0f);
            sd->stems.push_back (r);
            sd->peaks.push_back (makePeaks (*r, 240));
        }
        computeBpm (*sd, orig->sampleRate);

        // --- grava os arquivos (para poder arrastar para a DAW) ---------------------------------------
        p.progress = 0.96f;
        p.setStatus ("Gravando os arquivos dos stems...");
        auto base = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("BateriaSep");
        auto folder = base.getChildFile (src->name);
        for (int k = 2; folder.exists(); ++k) folder = base.getChildFile (src->name + " (" + juce::String (k) + ")");
        folder.createDirectory();
        bool wrote = folder.isDirectory();
        for (int s = 0; s < kNumStems && wrote; ++s)
            wrote = writeStemWav (folder.getChildFile (stemFileName (src->name, s)), *sd->stems[(size_t) s], 32, 1.0f);
        sd->folder = wrote ? folder : juce::File();

        auto finalSrc = finishSource (src);
        p.publish (finalSrc, finishStems (*sd), false);
        if (p.anyTriggerOn()) p.rebuildTriggerPlan();
        p.progress = 1.0f;
        p.setStatus (wrote ? "Pronto! Os arquivos estão em: "_u8 + folder.getFullPathName()
                           : "Pronto, mas não consegui gravar os arquivos (use \"Exportar todos\")."_u8);
    }

private:
    void fail (const juce::String& msg)
    {
        owner.progress = 0.0f;
        owner.setStatus (msg, true);
    }

    // refaz as cópias de reprodução caso a taxa do host tenha mudado durante o processamento
    std::shared_ptr<const SourceData> finishSource (const std::shared_ptr<SourceData>& s)
    {
        auto copy = std::make_shared<SourceData> (*s);
        copy->playRate = owner.hostRate.load();
        copy->playOriginal = resampled (copy->original, copy->playRate);
        return copy;
    }

    std::shared_ptr<const StemData> finishStems (StemData& sd)
    {
        auto copy = std::make_shared<StemData> (sd);
        copy->playStems.clear();
        for (auto& st : copy->stems) copy->playStems.push_back (resampled (st, owner.hostRate.load()));
        return copy;
    }

    static void computeBpm (StemData& sd, double rate)
    {
        // Só bumbo + caixa + tons: o chimbal (colcheias) confunde a oitava do andamento.
        const size_t n = (size_t) sd.stems[0]->size();
        std::vector<float> mono (n, 0.0f);
        for (int s = 0; s < 3; ++s)
            for (size_t i = 0; i < n; ++i) mono[i] += sd.stems[(size_t) s]->ch[0][i];
        sd.bpm = BpmDetector::estimate (mono.data(), n, rate);
    }

    std::shared_ptr<const StemData> loadStemsFromFolder (const SourceData& src, juce::AudioFormatManager& fm)
    {
        auto sd = std::make_shared<StemData>();
        for (int s = 0; s < kNumStems; ++s)
        {
            int ch = 2;
            auto a = readAudio (restoreFolder.getChildFile (stemFileName (src.name, s)), fm, ch, kMaxSeconds);
            if (a == nullptr) return nullptr;
            auto r = std::make_shared<StereoAudio> (*a);
            r->ch[0].resize ((size_t) src.original->size(), 0.0f);
            r->ch[1].resize ((size_t) src.original->size(), 0.0f);
            sd->stems.push_back (r);
            sd->peaks.push_back (makePeaks (*r, 240));
        }
        sd->folder = restoreFolder;
        computeBpm (*sd, src.original->sampleRate);
        return finishStems (*sd);
    }

    BateriaSepProcessor& owner;
    juce::File file, restoreFolder;
    int overlap;
};

// =================================================================================================
BateriaSepProcessor::BateriaSepProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Entrada", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Saida",   juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETROS", createLayout())
{
    for (auto& m : meters) m = 0.0f;
    for (int i = 0; i < 7; ++i) gainParams[(size_t) i] = apvts.getRawParameterValue (gainId (i));
    for (int i = 0; i < kNumStems; ++i)
    {
        muteParams[(size_t) i] = apvts.getRawParameterValue (muteId (i));
        soloParams[(size_t) i] = apvts.getRawParameterValue (soloId (i));
    }
    origParam = apvts.getRawParameterValue ("orig");
    loopParam = apvts.getRawParameterValue ("loop");
    qualityParam = apvts.getRawParameterValue ("quality");
    midiSensParam = apvts.getRawParameterValue ("midiSens");
    for (int i = 0; i < kNumStems; ++i)
    {
        trigModeParams[(size_t) i] = apvts.getRawParameterValue (trigModeId (i));
        trigLevelParams[(size_t) i] = apvts.getRawParameterValue (trigLevelId (i));
    }
    trigExportParam = apvts.getRawParameterValue ("trigExport");

    setStatus ("Clique em \"Carregar bateria...\" para começar."_u8);
}

BateriaSepProcessor::~BateriaSepProcessor()
{
    if (loader != nullptr)
    {
        loader->signalThreadShouldExit();
        loader->stopThread (180000);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout BateriaSepProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto faderRange = [] { NormalisableRange<float> r (-60.0f, 6.0f, 0.1f); r.setSkewForCentre (-12.0f); return r; };

    for (int i = 0; i < kNumStems; ++i)
    {
        params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { gainId (i), 1 }, String (stemName (i)) + " - volume",
                                                                 faderRange(), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
        params.push_back (std::make_unique<AudioParameterBool> (ParameterID { muteId (i), 1 }, String (stemName (i)) + " - mute", false));
        params.push_back (std::make_unique<AudioParameterBool> (ParameterID { soloId (i), 1 }, String (stemName (i)) + " - solo", false));
    }
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { gainId (6), 1 }, "Master - volume",
                                                             faderRange(), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "orig", 1 }, "Ouvir original", false));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "loop", 1 }, "Repetir seleção"_u8, true));
    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "quality", 1 }, "Qualidade da separação"_u8,
                                                              StringArray { "Rápida"_u8, "Padrão"_u8 }, 1));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "midiSens", 1 }, "Sensibilidade do MIDI",
                                                             NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    for (int i = 0; i < kNumStems; ++i)
    {
        params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { trigModeId (i), 1 }, String (stemName (i)) + " - trigger",
                                                                  StringArray { "Original", "Somar", "Substituir" }, 0));
        params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { trigLevelId (i), 1 }, String (stemName (i)) + " - nível do trigger"_u8,
                                                                 NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    }
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "trigExport", 1 }, "Exportar com triggers", false));
    return { params.begin(), params.end() };
}

bool BateriaSepProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet() == out;
}

juce::AudioProcessorEditor* BateriaSepProcessor::createEditor() { return new BateriaSepEditor (*this); }

// -------------------------------------------------------------------------------------------------
// Estado: faders, mute/solo, e onde estão o arquivo e os stems (para reabrir o projeto da DAW).
void BateriaSepProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto s = getSource()) state.setProperty ("sourcePath", s->file.getFullPathName(), nullptr);
    if (auto d = getStems()) state.setProperty ("stemsFolder", d->folder.getFullPathName(), nullptr);
    {
        const std::lock_guard<std::mutex> g (trigMutex);
        for (int i = 0; i < kNumStems; ++i)
            if (trigSources[(size_t) i] != nullptr)
                state.setProperty ("trigFiles" + juce::String (i), trigSources[(size_t) i]->files.joinIntoString ("|"), nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void BateriaSepProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);

    for (int i = 0; i < kNumStems; ++i)
    {
        const auto t = tree.getProperty ("trigFiles" + juce::String (i)).toString();
        if (t.isEmpty()) continue;
        auto files = juce::StringArray::fromTokens (t, "|", "");
        bool all = true;
        for (auto& f : files) all = all && juce::File (f).existsAsFile();
        juce::String msg;
        if (all) setTriggerSamples (i, files, msg);
    }

    const juce::File sourceFile (tree.getProperty ("sourcePath").toString());
    const juce::File folder (tree.getProperty ("stemsFolder").toString());
    if (sourceFile.existsAsFile() && folder.isDirectory() && getSource() == nullptr && ! isBusy())
    {
        loader = std::make_unique<Loader> (*this, sourceFile, folder, qualityParam->load() > 0.5f ? 4 : 2);
        loader->startThread();
    }
}

// -------------------------------------------------------------------------------------------------
void BateriaSepProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate;

    std::array<float, 7> targets {};
    for (int i = 0; i < 7; ++i)
    {
        const float db = gainParams[(size_t) i]->load();
        targets[(size_t) i] = db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
        smoothGain[(size_t) i].reset (sampleRate, 0.03);
        smoothGain[(size_t) i].setCurrentAndTargetValue (targets[(size_t) i]);
    }

    if (auto s = getSource())
        if (std::abs (s->playRate - sampleRate) > 0.5)
            rebuildPlaybackCopies (sampleRate);
}

void BateriaSepProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    buffer.clear();

    const double rate = hostRate.load();
    const float decay = (float) std::pow (0.1, (double) n / (rate * 0.4));   // -20 dB a cada 0,4 s
    std::array<float, 7> peak {};

    {
        const juce::SpinLock::ScopedTryLockType tl (lock);

        if (tl.isLocked() && source != nullptr && playing.load())
        {
            const auto& orig = *source->playOriginal;
            const StemData* sd = stems.get();
            const bool useOriginal = origParam->load() > 0.5f || sd == nullptr || sd->playStems.size() < (size_t) kNumStems;

            const int64_t total = orig.size();
            const double ss = selStart.load(), se = selEnd.load();
            const bool hasSel = ss >= 0.0 && se > ss;
            const int64_t s0 = hasSel ? (int64_t) (ss * rate) : 0;
            const int64_t regionEnd = hasSel ? std::min<int64_t> ((int64_t) (se * rate), total) : total;
            const bool loopOn = hasSel && loopParam->load() > 0.5f;

            int64_t pos = playPos.load();
            const int64_t ps = pendingSeek.exchange (-1);
            if (ps >= 0) pos = ps;

            // triggers: só valem se o plano é deste projeto e desta taxa de amostragem
            const TrigPlan* plan = nullptr;
            std::array<int, 6> tmode {};
            std::array<float, 6> tlev {};
            if (! useOriginal && playPlan != nullptr && playPlan->rate == rate && ! planStems.expired()
                && ! planStems.owner_before (stems) && ! stems.owner_before (planStems))
            {
                plan = playPlan.get();
                if (plan->id != enginePlanId) { trigEngine.reset(); enginePlanId = plan->id; }
                for (int k = 0; k < kNumStems; ++k)
                {
                    tmode[(size_t) k] = (int) (trigModeParams[(size_t) k]->load() + 0.5f);
                    tlev[(size_t) k] = juce::Decibels::decibelsToGain (trigLevelParams[(size_t) k]->load());
                }
            }

            bool anySolo = false;
            for (int i = 0; i < kNumStems; ++i) anySolo = anySolo || soloParams[(size_t) i]->load() > 0.5f;
            for (int i = 0; i < 7; ++i)
            {
                const float db = gainParams[(size_t) i]->load();
                float g = db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
                if (i < kNumStems)
                {
                    const bool audible = anySolo ? soloParams[(size_t) i]->load() > 0.5f : muteParams[(size_t) i]->load() < 0.5f;
                    if (! audible) g = 0.0f;
                }
                smoothGain[(size_t) i].setTargetValue (g);
            }

            float* outL = buffer.getWritePointer (0);
            float* outR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

            for (int i = 0; i < n; ++i)
            {
                if (pos >= regionEnd)
                {
                    if (loopOn) pos = s0;
                    else { playing = false; pos = s0; break; }
                }

                float g[7];
                for (int k = 0; k < 7; ++k) g[k] = smoothGain[(size_t) k].getNextValue();

                float l = 0.0f, r = 0.0f;
                if (useOriginal)
                {
                    l = orig.ch[0][(size_t) pos];
                    r = orig.ch[1][(size_t) pos];
                }
                else
                {
                    for (int k = 0; k < kNumStems; ++k)
                    {
                        const auto& st = *sd->playStems[(size_t) k];
                        const int mode = plan != nullptr ? tmode[(size_t) k] : 0;
                        const float trig = mode > 0 ? trigEngine.tick (*plan, k, pos) * tlev[(size_t) k] : 0.0f;
                        const bool inRange = pos < st.size();
                        if (! inRange && mode == 0) continue;
                        const float oL = (mode == 2 || ! inRange) ? 0.0f : st.ch[0][(size_t) pos];
                        const float oR = (mode == 2 || ! inRange) ? 0.0f : st.ch[1][(size_t) pos];
                        const float sl = (oL + trig) * g[k];
                        const float sr = (oR + trig) * g[k];
                        peak[(size_t) k] = std::max (peak[(size_t) k], std::max (std::abs (sl), std::abs (sr)));
                        l += sl; r += sr;
                    }
                }

                l *= g[6]; r *= g[6];
                peak[6] = std::max (peak[6], std::max (std::abs (l), std::abs (r)));

                if (outR != nullptr) { outL[i] = l; outR[i] = r; }
                else outL[i] = 0.5f * (l + r);

                ++pos;
            }
            playPos = pos;
        }
    }

    for (size_t k = 0; k < 7; ++k)
        meters[k] = std::max (peak[k], meters[k].load() * decay);
}

// -------------------------------------------------------------------------------------------------
// Player
void BateriaSepProcessor::play()
{
    auto src = getSource();
    if (src == nullptr) return;

    const double rate = hostRate.load();
    const int64_t total = src->playOriginal->size();
    int64_t pos = playPos.load();
    if (hasSelection())
    {
        const int64_t s0 = (int64_t) (selStart.load() * rate), s1 = (int64_t) (selEnd.load() * rate);
        if (pos < s0 || pos >= s1) pos = s0;
    }
    else if (pos >= total)
    {
        pos = 0;
    }
    playPos = pos;
    pendingSeek = pos;
    playing = true;
}

void BateriaSepProcessor::pause() { playing = false; }

void BateriaSepProcessor::stop()
{
    playing = false;
    pendingSeek = -1;
    playPos = hasSelection() ? (int64_t) (selStart.load() * hostRate.load()) : 0;
}

void BateriaSepProcessor::seekSeconds (double seconds)
{
    auto src = getSource();
    if (src == nullptr) return;
    const int64_t pos = std::max<int64_t> (0, std::min<int64_t> ((int64_t) (seconds * hostRate.load()), src->playOriginal->size() - 1));
    playPos = pos;
    if (playing.load()) pendingSeek = pos;
}

double BateriaSepProcessor::getPositionSeconds() const { return (double) playPos.load() / hostRate.load(); }

double BateriaSepProcessor::getDurationSeconds() const
{
    auto s = getSource();
    return s != nullptr ? s->original->seconds() : 0.0;
}

void BateriaSepProcessor::setSelection (double a, double b)
{
    const double dur = getDurationSeconds();
    a = std::max (0.0, std::min (a, dur));
    b = std::max (0.0, std::min (b, dur));
    if (b < a) std::swap (a, b);
    if (b - a < 0.02) { clearSelection(); return; }
    selStart = a;
    selEnd = b;
}

// -------------------------------------------------------------------------------------------------
// Dados
std::shared_ptr<const SourceData> BateriaSepProcessor::getSource() const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return source;
}

std::shared_ptr<const StemData> BateriaSepProcessor::getStems() const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return stems;
}

void BateriaSepProcessor::publish (std::shared_ptr<const SourceData> s, std::shared_ptr<const StemData> d, bool resetTransport)
{
    std::shared_ptr<const SourceData> oldS;
    std::shared_ptr<const StemData> oldD;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        oldS = std::move (source);
        oldD = std::move (stems);
        source = std::move (s);
        stems = std::move (d);
        if (resetTransport)
        {
            playing = false;
            playPos = 0;
            pendingSeek = -1;
            selStart = -1.0;
            selEnd = -1.0;
        }
    }
    // os dados antigos são liberados aqui, fora da trava (nunca na thread de áudio)
}

void BateriaSepProcessor::rebuildPlaybackCopies (double rate)
{
    auto s = getSource();
    auto d = getStems();
    if (s == nullptr) return;

    auto ns = std::make_shared<SourceData> (*s);
    ns->playRate = rate;
    ns->playOriginal = resampled (ns->original, rate);

    std::shared_ptr<StemData> nd;
    if (d != nullptr)
    {
        nd = std::make_shared<StemData> (*d);
        nd->playStems.clear();
        for (auto& st : nd->stems) nd->playStems.push_back (resampled (st, rate));
    }

    const double oldRate = s->playRate;
    const bool wasPlaying = playing.exchange (false);
    std::shared_ptr<const SourceData> oldS;
    std::shared_ptr<const StemData> oldD;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        if (source == s)   // senão outro projeto foi carregado enquanto isso: não mexe
        {
            oldS = std::move (source);
            oldD = std::move (stems);
            source = std::move (ns);
            stems = std::move (nd);
            playPos = (int64_t) ((double) playPos.load() * rate / oldRate);
        }
    }
    playing = wasPlaying;
}

// -------------------------------------------------------------------------------------------------
// Carregar / separar
void BateriaSepProcessor::loadFile (const juce::File& file)
{
    if (isBusy()) return;
    loader = std::make_unique<Loader> (*this, file, juce::File(), qualityParam->load() > 0.5f ? 4 : 2);
    loader->startThread();
}

void BateriaSepProcessor::cancelLoad()
{
    if (loader != nullptr) loader->signalThreadShouldExit();
}

bool BateriaSepProcessor::isBusy() const { return loader != nullptr && loader->isThreadRunning(); }

void BateriaSepProcessor::setStatus (const juce::String& text, bool error)
{
    const juce::ScopedLock sl (statusLock);
    statusText = text;
    statusIsError = error;
}

juce::String BateriaSepProcessor::getStatus (bool* isError) const
{
    const juce::ScopedLock sl (statusLock);
    if (isError != nullptr) *isError = statusIsError;
    return statusText;
}

// -------------------------------------------------------------------------------------------------
// Modelo e ONNX Runtime
juce::File BateriaSepProcessor::getUserDataDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("BateriaSep");
}

static juce::Array<juce::File> searchDirs()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::Array<juce::File> dirs;
    dirs.add (exe.getParentDirectory());
    dirs.add (exe.getParentDirectory().getParentDirectory().getChildFile ("Resources"));
    dirs.add (BateriaSepProcessor::getUserDataDir());
    // pasta onde o instalador coloca o modelo (C:\Program Files\BateriaSep)
    dirs.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("BateriaSep"));
   #if JUCE_MAC
    dirs.add (juce::File ("/Library/Application Support/BateriaSep"));   // instalador .pkg
   #elif JUCE_LINUX
    dirs.add (juce::File ("/opt/BateriaSep"));                           // instalador .deb
   #endif
    return dirs;
}

juce::File BateriaSepProcessor::findModelFile() const
{
    const auto saved = getUserDataDir().getChildFile ("modelpath.txt");
    if (saved.existsAsFile())
    {
        const juce::File f (saved.loadFileAsString().trim());
        if (f.existsAsFile()) return f;
    }
    for (auto& d : searchDirs())
        if (d.getChildFile (kModelFileName).existsAsFile()) return d.getChildFile (kModelFileName);
    return {};
}

void BateriaSepProcessor::setModelFile (const juce::File& f)
{
    getUserDataDir().createDirectory();
    getUserDataDir().getChildFile ("modelpath.txt").replaceWithText (f.getFullPathName());
}

juce::File BateriaSepProcessor::findOrtLibrary() const
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("BATERIASEP_ORT", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile()) return juce::File (env);

    for (auto& d : searchDirs())
    {
       #if JUCE_WINDOWS
        const auto f = d.getChildFile ("bateriasep_onnxruntime.dll");
        if (f.existsAsFile()) return f;
       #else
        juce::Array<juce::File> found;
       #if JUCE_MAC
        d.findChildFiles (found, juce::File::findFiles, false, "libonnxruntime*.dylib");
       #else
        d.findChildFiles (found, juce::File::findFiles, false, "libonnxruntime.so*");
       #endif
        if (found.size() > 0) return found[0];
       #endif
    }
    return {};
}

bool BateriaSepProcessor::ensureEngine (std::string& error)
{
    const std::lock_guard<std::mutex> g (engineMutex);
    if (engine.isLoaded()) return true;

    const auto ort = findOrtLibrary();
    if (! ort.existsAsFile())
    {
        error = "Não encontrei o ONNX Runtime ao lado do plugin. Reinstale copiando a pasta .vst3 completa.";
        return false;
    }
    if (! initOnnxRuntime (toFsPath (ort), error)) return false;

    const auto model = findModelFile();
    if (! model.existsAsFile())
    {
        error = std::string ("Modelo de IA (") + kModelFileName + ") não encontrado. Use \"Localizar modelo...\" ou copie o arquivo para "
                + getUserDataDir().getFullPathName().toStdString();
        return false;
    }

    setStatus ("Carregando o modelo de IA (pode levar alguns segundos)...");
    return engine.load (toFsPath (model), error);
}

// -------------------------------------------------------------------------------------------------
// Exportar
bool BateriaSepProcessor::exportStems (const juce::File& folder, int format, juce::String& message)
{
    const bool wantTrig = isTriggerExportOn() && anyTriggerOn();
    if (wantTrig) refreshMidi();

    auto src = getSource();
    auto sd = getStems();
    if (src == nullptr || sd == nullptr || sd->stems.size() < (size_t) kNumStems)
    {
        message = "Ainda não há stems prontos para exportar."_u8;
        return false;
    }

    folder.createDirectory();
    if (! folder.isDirectory()) { message = "Não consegui criar a pasta escolhida."_u8; return false; }

    bool changed = false;
    std::vector<AudioPtr> eff = wantTrig ? effectiveStems (sd, changed) : sd->stems;

    int bits = 32;
    float gain = 1.0f;
    if (format == 1)
    {
        bits = 24;
        float pk = 0.0f;
        for (auto& st : eff)
            for (int c = 0; c < 2; ++c)
                for (float v : st->ch[(size_t) c]) pk = std::max (pk, std::abs (v));
        if (pk > 0.98f) gain = 0.98f / pk;   // o mesmo ganho em todos mantém o balanço entre as peças
    }

    for (int s = 0; s < kNumStems; ++s)
        if (! writeStemWav (folder.getChildFile (stemFileName (src->name, s)), *eff[(size_t) s], bits, gain))
        {
            message = "Falha ao gravar " + stemFileName (src->name, s);
            return false;
        }

    message = juce::String (kNumStems) + " arquivos gravados em " + folder.getFullPathName()
              + (changed ? " (com os triggers aplicados)"_u8 : juce::String())
              + (gain < 1.0f ? "  (ganho de " + juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB aplicado a todos para não estourar)"_u8 : juce::String());
    return true;
}

juce::StringArray BateriaSepProcessor::getStemPaths()
{
    juce::StringArray paths;
    const bool wantTrig = isTriggerExportOn() && anyTriggerOn();
    if (wantTrig) refreshMidi();
    auto src = getSource();
    auto sd = getStems();
    if (src == nullptr || sd == nullptr || ! sd->folder.isDirectory()) return paths;

    auto original = [&] (int s) { return sd->folder.getChildFile (stemFileName (src->name, s)); };

    if (wantTrig)
    {
        const auto dir = sd->folder.getChildFile ("Triggers");
        juce::String sig = juce::String ((juce::int64) (intptr_t) sd.get()) + "/" + juce::String (midiSensParam->load(), 3);
        for (int k = 0; k < kNumStems; ++k)
        {
            const int mode = (int) (trigModeParams[(size_t) k]->load() + 0.5f);
            const auto sk = getSourceFor (k);
            sig << "|" << mode << ":" << juce::String (trigLevelParams[(size_t) k]->load(), 1) << ":" << (sk != nullptr ? (juce::int64) sk->serial : 0);
        }

        bool filesOk = sig == trigExportSig;
        if (filesOk)
            for (int k = 0; k < kNumStems; ++k)
                if (trigModeParams[(size_t) k]->load() > 0.5f && ! dir.getChildFile (stemFileName (src->name, k)).existsAsFile()) filesOk = false;

        if (! filesOk)
        {
            bool changed = false;
            auto eff = effectiveStems (sd, changed);
            dir.createDirectory();
            bool wroteAll = true;
            for (int k = 0; k < kNumStems; ++k)
                if (eff[(size_t) k] != sd->stems[(size_t) k])
                    wroteAll = writeStemWav (dir.getChildFile (stemFileName (src->name, k)), *eff[(size_t) k], 32, 1.0f) && wroteAll;
            trigExportSig = wroteAll ? sig : juce::String();
        }

        for (int k = 0; k < kNumStems; ++k)
        {
            const auto t = dir.getChildFile (stemFileName (src->name, k));
            const bool useT = trigModeParams[(size_t) k]->load() > 0.5f && t.existsAsFile();
            const auto f = useT ? t : original (k);
            if (f.existsAsFile()) paths.add (f.getFullPathName());
        }
        return paths;
    }

    for (int s = 0; s < kNumStems; ++s)
        if (original (s).existsAsFile()) paths.add (original (s).getFullPathName());
    return paths;
}

// =================================================================================================
// MIDI
// =================================================================================================
static bool writeMidiFile (const juce::File& f, const std::vector<const std::vector<bateriasep::MidiHit>*>& lists,
                           const juce::String& trackName, double bpm)
{
    constexpr int kTicks = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    const double ticksPerSec = bpm / 60.0 * kTicks;
    for (auto* l : lists)
        for (auto& h : *l)
        {
            const double on = h.time * ticksPerSec;
            const double off = on + std::max (1.0, h.duration * ticksPerSec);
            seq.addEvent (juce::MidiMessage::noteOn (10, h.note, (juce::uint8) h.velocity), on);
            seq.addEvent (juce::MidiMessage::noteOff (10, h.note, (juce::uint8) 0), off);
        }
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (kTicks);
    mf.addTrack (seq);

    f.deleteFile();
    f.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    return os != nullptr && mf.writeTo (*os, 0);
}

bool BateriaSepProcessor::refreshMidi()
{
    bool recomputed = false;
    const bool ok = refreshMidiImpl (recomputed);
    if (recomputed) installPlan();   // as notas mudaram: o plano dos triggers precisa ser refeito
    return ok;
}

bool BateriaSepProcessor::refreshMidiImpl (bool& recomputed)
{
    recomputed = false;
    auto src = getSource();
    auto sd = getStems();
    if (src == nullptr || sd == nullptr || sd->stems.size() < (size_t) kNumStems) return false;

    const float sens = midiSensParam != nullptr ? midiSensParam->load() : 0.5f;
    double bpm = midiBpm.load();
    if (bpm < 20.0 || bpm > 400.0) bpm = sd->bpm.valid ? sd->bpm.bpm : 120.0;

    const std::lock_guard<std::mutex> g (midiMutex);
    if (midiCache.valid && midiCache.stems.lock() == sd && std::abs (midiCache.sens - sens) < 1e-4f
        && std::abs (midiCache.bpm - bpm) < 0.01)
        return true;

    // as notas só mudam se os stems ou a sensibilidade mudarem (o BPM só afeta os arquivos .mid)
    std::shared_ptr<const std::array<std::vector<bateriasep::MidiHit>, 6>> hitsPtr;
    if (midiCache.hits != nullptr && midiCache.stems.lock() == sd && std::abs (midiCache.sens - sens) < 1e-4f)
    {
        hitsPtr = midiCache.hits;
    }
    else
    {
        std::array<bateriasep::StemView, 6> views;
        for (int s = 0; s < kNumStems; ++s)
        {
            const auto& a = *sd->stems[(size_t) s];
            views[(size_t) s] = { a.ch[0].data(), a.ch[1].data(), a.ch[0].size() };
        }
        const double sr = sd->stems[0]->sampleRate;
        hitsPtr = std::make_shared<const std::array<std::vector<bateriasep::MidiHit>, 6>> (bateriasep::extractAllHits (views, sr, sens));
        recomputed = true;
    }
    const auto& hits = *hitsPtr;

    MidiCache c;
    c.stems = sd;
    c.sens = sens;
    c.bpm = bpm;
    c.hits = hitsPtr;
    for (int s = 0; s < kNumStems; ++s) c.counts[(size_t) s] = (int) hits[(size_t) s].size();

    juce::File base = sd->folder.isDirectory() ? sd->folder
                                               : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BateriaSep_stems");
    const auto dir = base.getChildFile ("MIDI");
    dir.createDirectory();
    if (! dir.isDirectory()) { midiCache = c; return false; }   // as notas ficam disponíveis para os triggers mesmo assim

    bool ok = true;
    std::vector<const std::vector<bateriasep::MidiHit>*> all;
    for (int s = 0; s < kNumStems; ++s)
    {
        const auto f = dir.getChildFile (src->name + "_" + stemName (s) + ".mid");
        ok = writeMidiFile (f, { &hits[(size_t) s] }, juce::String (stemName (s)), bpm) && ok;
        c.stemFiles.add (f.getFullPathName());
        all.push_back (&hits[(size_t) s]);
    }
    const auto full = dir.getChildFile (src->name + "_Bateria_completa.mid");
    ok = writeMidiFile (full, all, "Bateria", bpm) && ok;
    c.combinedFile = full.getFullPathName();
    c.valid = ok;
    midiCache = c;
    return ok;
}

juce::StringArray BateriaSepProcessor::getMidiPaths()
{
    if (! refreshMidi()) return {};
    const std::lock_guard<std::mutex> g (midiMutex);
    return midiCache.stemFiles;
}

juce::String BateriaSepProcessor::getMidiCombinedPath()
{
    if (! refreshMidi()) return {};
    const std::lock_guard<std::mutex> g (midiMutex);
    return midiCache.combinedFile;
}

juce::String BateriaSepProcessor::getMidiSummary() const
{
    const std::lock_guard<std::mutex> g (midiMutex);
    if (! midiCache.valid || midiCache.stems.expired()) return {};
    juce::String t;
    int total = 0;
    for (int s = 0; s < kNumStems; ++s)
    {
        if (s > 0) t << "  |  ";
        t << stemName (s) << " " << midiCache.counts[(size_t) s];
        total += midiCache.counts[(size_t) s];
    }
    return juce::String (total) + " notas:  " + t;
}

bool BateriaSepProcessor::exportMidi (const juce::File& folder, juce::String& message)
{
    if (! refreshMidi()) { message = "Ainda não há stems para gerar o MIDI."_u8; return false; }
    juce::StringArray files;
    {
        const std::lock_guard<std::mutex> g (midiMutex);
        files = midiCache.stemFiles;
        files.add (midiCache.combinedFile);
    }
    folder.createDirectory();
    if (! folder.isDirectory()) { message = "Não consegui criar a pasta escolhida."_u8; return false; }
    for (auto& p : files)
    {
        const juce::File src (p);
        if (! src.copyFileTo (folder.getChildFile (src.getFileName())))
        {
            message = "Falha ao gravar " + src.getFileName();
            return false;
        }
    }
    message = juce::String (files.size()) + " arquivos MIDI gravados em " + folder.getFullPathName();
    return true;
}

// =================================================================================================
// Triggers
// =================================================================================================
namespace
{
// lê um arquivo de áudio como mono (média dos canais)
bool decodeMono (juce::AudioFormatReader& r, std::vector<float>& out, double& rate)
{
    const int n = (int) r.lengthInSamples;
    if (n <= 0) return false;
    const int nc = (int) std::max<unsigned> (1u, r.numChannels);
    juce::AudioBuffer<float> buf (nc, n);
    buf.clear();
    r.read (&buf, 0, n, 0, true, true);
    out.assign ((size_t) n, 0.0f);
    for (int c = 0; c < nc; ++c)
    {
        const float* p = buf.getReadPointer (c);
        for (int i = 0; i < n; ++i) out[(size_t) i] += p[i] / (float) nc;
    }
    rate = r.sampleRate;
    return true;
}

// corta o silêncio antes do ataque (deixa 0,5 ms) e suaviza o final
void trimSample (std::vector<float>& x, double rate)
{
    float pk = 0.0f;
    for (float v : x) pk = std::max (pk, std::abs (v));
    if (pk <= 0.0f) return;
    size_t first = 0;
    while (first < x.size() && std::abs (x[first]) < 0.03f * pk) ++first;
    const size_t back = (size_t) (0.0005 * rate);
    first = first > back ? first - back : 0;
    x.erase (x.begin(), x.begin() + (std::ptrdiff_t) first);
    const size_t nf = std::min (x.size(), (size_t) (0.01 * rate));
    for (size_t i = 0; i < nf; ++i) x[x.size() - nf + i] *= 1.0f - (float) i / (float) nf;
}
}

bool BateriaSepProcessor::anyTriggerOn() const
{
    for (auto* p : trigModeParams)
        if (p != nullptr && p->load() > 0.5f) return true;
    return false;
}

bool BateriaSepProcessor::triggerPlanReady() const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return playPlan != nullptr && playPlan->rate == hostRate.load() && ! planStems.expired()
           && ! planStems.owner_before (stems) && ! stems.owner_before (planStems);
}

std::shared_ptr<const BateriaSepProcessor::TrigPieceSource> BateriaSepProcessor::getSourceFor (int piece)
{
    const std::lock_guard<std::mutex> g (trigMutex);
    if (trigSources[(size_t) piece] != nullptr) return trigSources[(size_t) piece];
    if (defaultSources[(size_t) piece] != nullptr) return defaultSources[(size_t) piece];

    // decodifica o kit padrão embutido (uma vez só)
    juce::MemoryInputStream mis (KitData::default_kit_zip, (size_t) KitData::default_kit_zipSize, false);
    juce::ZipFile zip (mis);
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::map<juce::String, std::vector<std::vector<TrigSamplePtr>>> voices;   // voz -> [camada][variação]
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        if (e == nullptr) continue;
        auto tok = juce::StringArray::fromTokens (e->filename.upToLastOccurrenceOf (".", false, false), "_", "");
        if (tok.size() != 3) continue;
        const int layer = tok[1].substring (1).getIntValue(), rr = tok[2].substring (1).getIntValue();
        if (layer < 1 || rr < 1) continue;
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        if (in == nullptr) continue;
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (std::move (in)));
        std::vector<float> data;
        double rate = 44100.0;
        if (r == nullptr || ! decodeMono (*r, data, rate)) continue;
        auto& v = voices[tok[0]];
        if ((int) v.size() < layer) v.resize ((size_t) layer);
        if ((int) v[(size_t) layer - 1].size() < rr) v[(size_t) layer - 1].resize ((size_t) rr);
        v[(size_t) layer - 1][(size_t) rr - 1] = std::make_shared<const std::vector<float>> (std::move (data));
    }

    auto make = [&] (std::initializer_list<std::pair<int, const char*>> map)
    {
        auto src = std::make_shared<TrigPieceSource>();
        src->rate = 44100.0;
        src->label = "Kit padrão (Big Rusty Drums)"_u8;
        src->serial = ++sourceCounter;
        for (auto& kv : map)
        {
            auto it = voices.find (kv.second);
            if (it != voices.end()) src->byNote[kv.first].layers = it->second;
        }
        return src;
    };
    using namespace bateriasep;
    defaultSources[0] = make ({ { -1, "kick" } });
    defaultSources[1] = make ({ { -1, "snare" } });
    defaultSources[2] = make ({ { gm::tomHigh, "tomH" }, { gm::tomMid, "tomM" }, { gm::tomLow, "tomL" }, { gm::tomLowFloor, "tomL" }, { -1, "tomM" } });
    defaultSources[3] = make ({ { gm::hatClosed, "hatC" }, { gm::hatOpen, "hatO" }, { -1, "hatC" } });
    defaultSources[4] = make ({ { -1, "ride" } });
    defaultSources[5] = make ({ { -1, "crash" } });
    return defaultSources[(size_t) piece];
}

std::shared_ptr<const bateriasep::TrigPiece> BateriaSepProcessor::getTrigPiece (int piece, double rate)
{
    auto src = getSourceFor (piece);
    if (src == nullptr) return nullptr;

    const std::lock_guard<std::mutex> g (trigMutex);
    const auto key = std::make_pair (src.get(), (long) std::lround (rate));
    auto it = trigPieceCache.find (key);
    if (it != trigPieceCache.end()) return it->second;

    auto tp = std::make_shared<bateriasep::TrigPiece>();
    for (auto& kv : src->byNote)
    {
        bateriasep::TrigVoice v;
        for (auto& layer : kv.second.layers)
        {
            std::vector<bateriasep::TrigSamplePtr> rr;
            for (auto& smp : layer)
            {
                if (smp == nullptr || std::abs (src->rate - rate) < 0.5) rr.push_back (smp);
                else rr.push_back (std::make_shared<const std::vector<float>> (Resampler::process (smp->data(), smp->size(), src->rate, rate)));
            }
            v.layers.push_back (std::move (rr));
        }
        tp->byNote[kv.first] = std::move (v);
    }
    trigPieceCache[key] = tp;
    return tp;
}

std::shared_ptr<bateriasep::TrigPlan> BateriaSepProcessor::makeTrigPlan (double rate, const std::shared_ptr<const StemData>& sd)
{
    if (sd == nullptr) return nullptr;
    std::shared_ptr<const std::array<std::vector<bateriasep::MidiHit>, 6>> hits;
    {
        const std::lock_guard<std::mutex> g (midiMutex);
        if (midiCache.hits != nullptr && midiCache.stems.lock() == sd) hits = midiCache.hits;
    }
    if (hits == nullptr) return nullptr;

    auto plan = std::make_shared<bateriasep::TrigPlan>();
    plan->id = ++planCounter;
    plan->rate = rate;
    plan->hits = *hits;
    auto kit = std::make_shared<bateriasep::TrigKit>();
    for (int p = 0; p < kNumStems; ++p)
        if (auto pc = getTrigPiece (p, rate)) kit->piece[(size_t) p] = *pc;
    plan->kit = kit;
    plan->prepare();
    return plan;
}

void BateriaSepProcessor::installPlan()
{
    const std::lock_guard<std::mutex> bg (trigBuildMutex);
    auto sd = getStems();
    std::shared_ptr<const bateriasep::TrigPlan> np;
    if (sd != nullptr) np = makeTrigPlan (hostRate.load(), sd);

    std::shared_ptr<const bateriasep::TrigPlan> old;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        old = std::move (playPlan);
        playPlan = np;
        planStems = np != nullptr ? std::weak_ptr<const StemData> (sd) : std::weak_ptr<const StemData>();
    }
    // o plano antigo é liberado aqui, fora da trava
}

void BateriaSepProcessor::rebuildTriggerPlan()
{
    bool recomputed = false;
    refreshMidiImpl (recomputed);
    installPlan();
}

std::vector<bateriasep::AudioPtr> BateriaSepProcessor::effectiveStems (const std::shared_ptr<const StemData>& sd, bool& changed)
{
    changed = false;
    std::vector<AudioPtr> out (sd->stems.begin(), sd->stems.end());
    if (! anyTriggerOn()) return out;

    std::shared_ptr<const bateriasep::TrigPlan> plan = makeTrigPlan (sd->stems[0]->sampleRate, sd);
    if (plan == nullptr) return out;

    for (int k = 0; k < kNumStems; ++k)
    {
        const int mode = (int) (trigModeParams[(size_t) k]->load() + 0.5f);
        if (mode == 0) continue;
        const auto& orig = *sd->stems[(size_t) k];
        const size_t n = orig.ch[0].size();
        const auto trig = bateriasep::TrigEngine::render (*plan, k, n);
        const float lev = juce::Decibels::decibelsToGain (trigLevelParams[(size_t) k]->load());

        auto a = std::make_shared<StereoAudio>();
        a->sampleRate = orig.sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            auto& dst = a->ch[(size_t) c];
            if (mode == 1) dst = orig.ch[(size_t) c]; else dst.assign (n, 0.0f);
            for (size_t i = 0; i < n; ++i) dst[i] += trig[i] * lev;
        }
        out[(size_t) k] = a;
        changed = true;
    }
    return out;
}

bool BateriaSepProcessor::setTriggerSamples (int piece, const juce::StringArray& filesIn, juce::String& message)
{
    if (piece < 0 || piece >= kNumStems) return false;
    if (filesIn.isEmpty()) { useDefaultTriggerKit (piece); message = "Kit padrão restaurado."_u8; return true; }

    juce::StringArray files (filesIn);
    files.sortNatural();   // a ordem pelo nome define a intensidade: do mais suave ao mais forte

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::vector<std::vector<float>> datas;
    double rate = 0.0;
    for (auto& f : files)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (f)));
        std::vector<float> d;
        double rt = 0.0;
        if (r == nullptr || r->lengthInSamples <= 0 || (double) r->lengthInSamples > r->sampleRate * 20.0 || ! decodeMono (*r, d, rt))
        {
            message = "Não consegui abrir \""_u8 + juce::File (f).getFileName() + "\" (use WAV, AIFF, FLAC, OGG ou MP3 com até 20 s)."_u8;
            return false;
        }
        if (rate <= 0.0) rate = rt;
        else if (std::abs (rt - rate) > 0.5) d = Resampler::process (d.data(), d.size(), rt, rate);
        datas.push_back (std::move (d));
    }

    float pk = 0.0f;
    for (auto& d : datas) for (float v : d) pk = std::max (pk, std::abs (v));
    const float g = pk > 0.0f ? 0.89f / pk : 1.0f;   // mesmo ganho em todos: mantém a diferença de intensidade entre eles

    auto src = std::make_shared<TrigPieceSource>();
    src->rate = rate;
    src->files = files;
    src->serial = ++sourceCounter;
    src->label = juce::String (files.size()) + (files.size() == 1 ? " sample seu" : " samples seus");
    TrigSrcVoice v;
    for (auto& d : datas)
    {
        for (auto& x : d) x *= g;
        trimSample (d, rate);
        v.layers.push_back ({ std::make_shared<const std::vector<float>> (std::move (d)) });
    }
    src->byNote[-1] = std::move (v);

    {
        const std::lock_guard<std::mutex> gl (trigMutex);
        trigSources[(size_t) piece] = src;
        trigPieceCache.clear();
    }
    rebuildTriggerPlan();
    message = files.size() == 1 ? "1 sample carregado: o volume dele acompanha a intensidade de cada golpe."_u8
                                : juce::String (files.size()) + " samples carregados (do mais suave ao mais forte, em ordem de nome)."_u8;
    return true;
}

void BateriaSepProcessor::useDefaultTriggerKit (int piece)
{
    if (piece < 0 || piece >= kNumStems) return;
    {
        const std::lock_guard<std::mutex> g (trigMutex);
        trigSources[(size_t) piece] = nullptr;
        trigPieceCache.clear();
    }
    rebuildTriggerPlan();
}

juce::String BateriaSepProcessor::getTriggerSourceLabel (int piece) const
{
    const std::lock_guard<std::mutex> g (trigMutex);
    if (piece >= 0 && piece < kNumStems && trigSources[(size_t) piece] != nullptr) return trigSources[(size_t) piece]->label;
    return "Kit padrão (Big Rusty Drums)"_u8;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BateriaSepProcessor(); }
