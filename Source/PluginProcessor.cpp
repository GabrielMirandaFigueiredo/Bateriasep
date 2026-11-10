#include "PluginProcessor.h"

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
            if (sd != nullptr) { p.publish (finishSource (src), sd, false); p.progress = 1.0f; p.setStatus ("Pronto. Stems carregados da pasta do projeto."); return; }
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
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void BateriaSepProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);

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
                        if (pos >= st.size()) continue;
                        const float sl = st.ch[0][(size_t) pos] * g[k];
                        const float sr = st.ch[1][(size_t) pos] * g[k];
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
bool BateriaSepProcessor::exportStems (const juce::File& folder, int format, juce::String& message) const
{
    auto src = getSource();
    auto sd = getStems();
    if (src == nullptr || sd == nullptr || sd->stems.size() < (size_t) kNumStems)
    {
        message = "Ainda não há stems prontos para exportar."_u8;
        return false;
    }

    folder.createDirectory();
    if (! folder.isDirectory()) { message = "Não consegui criar a pasta escolhida."_u8; return false; }

    int bits = 32;
    float gain = 1.0f;
    if (format == 1)
    {
        bits = 24;
        float pk = 0.0f;
        for (auto& st : sd->stems)
            for (int c = 0; c < 2; ++c)
                for (float v : st->ch[(size_t) c]) pk = std::max (pk, std::abs (v));
        if (pk > 0.98f) gain = 0.98f / pk;   // o mesmo ganho em todos mantém o balanço entre as peças
    }

    for (int s = 0; s < kNumStems; ++s)
        if (! writeStemWav (folder.getChildFile (stemFileName (src->name, s)), *sd->stems[(size_t) s], bits, gain))
        {
            message = "Falha ao gravar " + stemFileName (src->name, s);
            return false;
        }

    message = juce::String (kNumStems) + " arquivos gravados em " + folder.getFullPathName()
              + (gain < 1.0f ? "  (ganho de " + juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB aplicado a todos para não estourar)"_u8 : juce::String());
    return true;
}

juce::StringArray BateriaSepProcessor::getStemPaths() const
{
    juce::StringArray paths;
    auto src = getSource();
    auto sd = getStems();
    if (src == nullptr || sd == nullptr || ! sd->folder.isDirectory()) return paths;
    for (int s = 0; s < kNumStems; ++s)
    {
        const auto f = sd->folder.getChildFile (stemFileName (src->name, s));
        if (f.existsAsFile()) paths.add (f.getFullPathName());
    }
    return paths;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BateriaSepProcessor(); }
