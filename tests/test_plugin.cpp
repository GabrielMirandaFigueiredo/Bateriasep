// Teste de integração do plugin (sem DAW): carrega um áudio de verdade, separa, toca, conferindo mute/solo,
// seleção/repetição, A/B original, master, exportação, reabertura do projeto e tira "fotos" da interface.
//
//   BateriaSepTest <audio.wav> <drumsep_core.onnx> <libonnxruntime.so> <pasta_de_saida>

#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdio>
#include <thread>

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

using namespace bateriasep;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "OK" : "FALHA", what.toRawUTF8());
    if (! ok) ++failures;
}

static void setParam (BateriaSepProcessor& p, const juce::String& id, float realValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void snapshot (BateriaSepProcessor& proc, juce::Component& c, const juce::File& png)
{
    pump (200);
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    png.deleteFile();
    juce::FileOutputStream os (png);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("  imagem: %s\n", png.getFullPathName().toRawUTF8());
    (void) proc;
}

// processa 'blocks' blocos e devolve a saída concatenada (canal 0 e 1)
static void run (BateriaSepProcessor& p, int blocks, int bs, std::vector<float>& l, std::vector<float>& r)
{
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        p.processBlock (buf, midi);
        l.insert (l.end(), buf.getReadPointer (0), buf.getReadPointer (0) + bs);
        r.insert (r.end(), buf.getReadPointer (1), buf.getReadPointer (1) + bs);
    }
}

int main (int argc, char** argv)
{
    if (argc < 5) { std::printf ("uso: %s <audio> <modelo.onnx> <libonnxruntime> <saida>\n", argv[0]); return 2; }
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File audio (argv[1]), model (argv[2]), outDir (argv[4]);
    outDir.createDirectory();
    const bool autoFind = juce::String (argv[2]) == "auto";   // testa a busca automática (instalador)
    if (! autoFind) setenv ("BATERIASEP_ORT", argv[3], 1);

    constexpr double sr = 48000.0;
    constexpr int bs = 512;

    BateriaSepProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, bs);
    proc.prepareToPlay (sr, bs);
    if (! autoFind) proc.setModelFile (model);
    setParam (proc, "quality", 0.0f);   // Rápida (mais veloz)

    std::printf ("== Carregando e separando (qualidade Rápida) ==\n");
    proc.loadFile (audio);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setVisible (false);
    bool shotDuring = false;
    int waited = 0;
    while (proc.isBusy())
    {
        pump (500);
        waited += 500;
        if (! shotDuring && proc.getProgress() > 0.25f && proc.getSource() != nullptr)
        {
            shotDuring = true;
            snapshot (proc, *editor, outDir.getChildFile ("ui_separando.png"));
        }
        if (waited % 20000 == 0) std::printf ("  ... %s\n", proc.getStatus().toRawUTF8());
    }
    pump (300);

    bool err = false;
    std::printf ("  status final: %s\n", proc.getStatus (&err).toRawUTF8());
    auto src = proc.getSource();
    auto sd = proc.getStems();
    check (src != nullptr && sd != nullptr && ! err, "separação concluída sem erro");
    if (src == nullptr || sd == nullptr) return 1;

    check (sd->stems.size() == 6 && sd->stems[0]->size() == src->original->size(), "6 stems com o mesmo comprimento do original");
    check (sd->folder.isDirectory() && proc.getStemPaths().size() == 6, "6 arquivos gravados em disco para arrastar");
    std::printf ("  pasta: %s\n", sd->folder.getFullPathName().toRawUTF8());
    check (sd->bpm.valid, "BPM estimado");
    std::printf ("  BPM = %.1f (metade %.1f / dobro %.1f) confiança %.2f, primeira batida %.3f s\n",
                 sd->bpm.bpm, sd->bpm.halfBpm, sd->bpm.doubleBpm, sd->bpm.confidence, sd->bpm.firstBeatSec);

    // ---- Player: tocar, pausar, parar, buscar ------------------------------------------------------
    std::printf ("== Player ==\n");
    proc.stop();
    proc.play();
    std::vector<float> l, r;
    run (proc, 40, bs, l, r);
    float pk = 0; for (float v : l) pk = std::max (pk, std::abs (v));
    check (proc.isPlaying() && proc.getPositionSeconds() > 0.3, "Play avança a posição");
    check (pk > 0.01f, "Play produz áudio");
    proc.pause();
    const double posPaused = proc.getPositionSeconds();
    l.clear(); r.clear(); run (proc, 10, bs, l, r);
    check (std::abs (proc.getPositionSeconds() - posPaused) < 1e-9, "Pause mantém a posição");
    proc.stop();
    check (proc.getPositionSeconds() == 0.0 && ! proc.isPlaying(), "Stop volta ao início");
    proc.seekSeconds (2.0);
    check (std::abs (proc.getPositionSeconds() - 2.0) < 0.01, "Seek vai ao ponto clicado");

    // ---- Mixer: solo, mute, master, A/B ---------------------------------------------------------------
    std::printf ("== Mixer ==\n");
    auto compare = [&] (const char* name, std::function<float (int64_t)> expectedL, int64_t startPos)
    {
        l.clear(); r.clear();
        run (proc, 30, bs, l, r);   // 15360 amostras (a suavização de ganho de 30 ms já terminou)
        double maxDiff = 0;
        for (size_t i = 6000; i < l.size(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (l[i] - expectedL (startPos + (int64_t) i)));
        std::printf ("  %-34s diferença máx. %.2e\n", name, maxDiff);
        check (maxDiff < 1e-4, name);
    };
    const auto& orig = *src->playOriginal;
    auto stemL = [&] (int k, int64_t pos) { return sd->playStems[(size_t) k]->ch[0][(size_t) pos]; };
    auto sumAll = [&] (int64_t pos) { float s = 0; for (int k = 0; k < 6; ++k) s += stemL (k, pos); return s; };

    proc.seekSeconds (2.0); proc.play();
    int64_t start = (int64_t) (2.0 * sr);
    compare ("soma dos 6 stems = original (aprox.)", [&] (int64_t p) { return sumAll (p); }, start);
    {   // a soma dos stems deve reproduzir o original
        double e = 0, o = 0;
        for (int64_t p = start; p < start + 20000; ++p) { e += std::pow (sumAll (p) - orig.ch[0][(size_t) p], 2.0); o += std::pow (orig.ch[0][(size_t) p], 2.0); }
        std::printf ("  erro de reconstrução: %.1f dB abaixo do original\n", 10 * std::log10 (o / (e + 1e-20)));
    }
    proc.pause();

    setParam (proc, "s0", 1.0f);   // solo no Kick
    proc.seekSeconds (3.0); proc.play(); start = (int64_t) (3.0 * sr);
    compare ("Solo Kick toca só o Kick", [&] (int64_t p) { return stemL (0, p); }, start);
    proc.pause(); setParam (proc, "s0", 0.0f);

    setParam (proc, "m0", 1.0f);   // mute no Kick
    proc.seekSeconds (3.0); proc.play();
    compare ("Mute Kick remove só o Kick", [&] (int64_t p) { return sumAll (p) - stemL (0, p); }, start);
    proc.pause(); setParam (proc, "m0", 0.0f);

    setParam (proc, "g1", -6.0f);   // Snare -6 dB
    proc.seekSeconds (3.0); proc.play();
    compare ("Fader Snare -6 dB", [&] (int64_t p) { return sumAll (p) - 0.4988f * stemL (1, p); }, start);
    proc.pause(); setParam (proc, "g1", 0.0f);

    setParam (proc, "g6", -6.0f);   // master
    proc.seekSeconds (3.0); proc.play();
    compare ("Master -6 dB", [&] (int64_t p) { return 0.50119f * sumAll (p); }, start);
    proc.pause(); setParam (proc, "g6", 0.0f);

    setParam (proc, "orig", 1.0f);  // ouvir original
    proc.seekSeconds (3.0); proc.play();
    compare ("Ouvir original (A/B)", [&] (int64_t p) { return orig.ch[0][(size_t) p]; }, start);
    proc.pause(); setParam (proc, "orig", 0.0f);

    // ---- Seleção e repetição ---------------------------------------------------------------------------
    std::printf ("== Seleção ==\n");
    proc.setSelection (1.0, 1.5);
    check (proc.hasSelection(), "seleção criada");
    setParam (proc, "loop", 1.0f);
    proc.stop();
    check (std::abs (proc.getPositionSeconds() - 1.0) < 0.01, "Stop vai ao início da seleção");
    proc.play();
    bool inside = true; int wraps = 0; double last = proc.getPositionSeconds();
    for (int b = 0; b < 400; ++b)
    {
        l.clear(); r.clear(); run (proc, 1, bs, l, r);
        const double t = proc.getPositionSeconds();
        if (t < 0.999 || t > 1.5 + bs / sr) inside = false;
        if (t < last) ++wraps;
        last = t;
    }
    check (inside && wraps >= 6 && proc.isPlaying(), "repetição fica dentro da seleção (" + juce::String (wraps) + " voltas em 4,3 s)");
    setParam (proc, "loop", 0.0f);
    for (int b = 0; b < 100 && proc.isPlaying(); ++b) { l.clear(); r.clear(); run (proc, 1, bs, l, r); }
    check (! proc.isPlaying(), "sem repetição, para no fim da seleção");
    setParam (proc, "loop", 1.0f);

    // ---- UI: foto com seleção e faders mexidos -------------------------------------------------------------
    setParam (proc, "s3", 1.0f);
    setParam (proc, "m4", 1.0f);
    setParam (proc, "g0", -3.5f);
    proc.setSelection (1.0, 4.0);
    proc.seekSeconds (2.0);
    proc.play();
    l.clear(); r.clear(); run (proc, 60, bs, l, r);
    editor->setVisible (true);
    snapshot (proc, *editor, outDir.getChildFile ("ui_pronto.png"));
    proc.pause();
    setParam (proc, "s3", 0.0f); setParam (proc, "m4", 0.0f); setParam (proc, "g0", 0.0f);
    proc.clearSelection();

    // ---- Exportar --------------------------------------------------------------------------------------------
    std::printf ("== Exportar ==\n");
    juce::String msg;
    auto exp32 = outDir.getChildFile ("export32"), exp24 = outDir.getChildFile ("export24");
    check (proc.exportStems (exp32, 0, msg) && exp32.findChildFiles (juce::File::findFiles, false, "*.wav").size() == 6, "exportar 6 WAVs 32 bits float");
    check (proc.exportStems (exp24, 1, msg), "exportar 6 WAVs 24 bits");
    std::printf ("  %s\n", msg.toRawUTF8());
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (exp32.getChildFile (stemFileName (src->name, 0))));
        check (rd != nullptr && rd->lengthInSamples == src->original->size() && std::abs (rd->sampleRate - src->original->sampleRate) < 1, "WAV exportado tem a duração e a taxa do original");
        juce::AudioBuffer<float> b (2, 4000);
        if (rd) rd->read (&b, 0, 4000, 100000, true, true);
        double dmax = 0; for (int i = 0; i < 4000; ++i) dmax = std::max (dmax, (double) std::abs (b.getSample (0, i) - sd->stems[0]->ch[0][(size_t) (100000 + i)]));
        check (dmax < 1e-6, "WAV 32 bits float é idêntico ao stem na memória");
    }

    // ---- Reabrir o projeto da DAW (estado) ---------------------------------------------------------------------
    // ---- MIDI -----------------------------------------------------------------------------------------
    std::printf ("== MIDI ==\n");
    {
        proc.setMidiTempo (sd->bpm.bpm);
        const auto midiPaths = proc.getMidiPaths();
        check (midiPaths.size() == 6, "6 arquivos .mid (um por peça)");
        const auto combined = proc.getMidiCombinedPath();
        check (juce::File (combined).existsAsFile(), "arquivo .mid único com todas as peças");
        std::printf ("  %s\n", proc.getMidiSummary().toRawUTF8());

        auto readMidi = [] (const juce::String& path, int& notes, int& minVel, int& maxVel, double& bpm, int& ppq)
        {
            notes = 0; minVel = 999; maxVel = 0; bpm = 0; ppq = 0;
            juce::MidiFile mf;
            juce::FileInputStream in { juce::File (path) };
            if (! in.openedOk() || ! mf.readFrom (in)) return false;
            ppq = mf.getTimeFormat();
            mf.convertTimestampTicksToSeconds();
            for (int t = 0; t < mf.getNumTracks(); ++t)
                for (auto* e : *mf.getTrack (t))
                {
                    if (e->message.isNoteOn()) { ++notes; minVel = std::min (minVel, (int) e->message.getVelocity()); maxVel = std::max (maxVel, (int) e->message.getVelocity()); }
                    if (e->message.isTempoMetaEvent()) bpm = 60.0 / e->message.getTempoSecondsPerQuarterNote();
                }
            return true;
        };

        int total = 0;
        bool allRead = true;
        for (auto& pth : midiPaths)
        {
            int n, lo, hi, ppq; double bpm;
            allRead = readMidi (pth, n, lo, hi, bpm, ppq) && allRead;
            total += n;
            std::printf ("  %-40s %3d notas, velocity %d..%d, %.1f BPM\n", juce::File (pth).getFileName().toRawUTF8(), n, n ? lo : 0, hi, bpm);
        }
        check (allRead, "todos os .mid abrem e são válidos");
        int nAll, lo, hi, ppq; double bpmAll;
        readMidi (combined, nAll, lo, hi, bpmAll, ppq);
        check (nAll == total && total > 20, "o MIDI único tem as mesmas notas das peças somadas");
        check (std::abs (bpmAll - sd->bpm.bpm) < 0.2, "tempo do MIDI = BPM detectado");
        check (hi >= 120 && lo >= 1 && lo < hi, "velocities variam de acordo com a dinâmica");

        // mais sensibilidade = mais (ou as mesmas) notas
        const int before = total;
        setParam (proc, "midiSens", 1.0f);
        int total2 = 0;
        for (auto& pth : proc.getMidiPaths()) { int n, l2, h2, q; double b; readMidi (pth, n, l2, h2, b, q); total2 += n; }
        std::printf ("  sensibilidade 0.5 -> %d notas; 1.0 -> %d notas\n", before, total2);
        check (total2 >= before, "sensibilidade máxima gera mais notas");
        setParam (proc, "midiSens", 0.5f);
        proc.getMidiPaths();

        juce::String msg;
        const auto midiOut = outDir.getChildFile ("export_midi");
        check (proc.exportMidi (midiOut, msg) && midiOut.getNumberOfChildFiles (juce::File::findFiles, "*.mid") == 7, "exportar 7 arquivos MIDI");
        snapshot (proc, *editor, outDir.getChildFile ("ui_midi.png"));
    }

    // ---- Triggers -------------------------------------------------------------------------------------
    std::printf ("== Triggers ==\n");
    {
        auto readWav = [] (const juce::File& f, std::vector<float>& out)
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) return false;
            juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
            r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            out.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
            return true;
        };
        auto hitTimes = [] (const juce::String& path, std::vector<std::pair<double, int>>& v)
        {
            juce::MidiFile mf;
            juce::FileInputStream in { juce::File (path) };
            if (! in.openedOk() || ! mf.readFrom (in)) return;
            mf.convertTimestampTicksToSeconds();
            for (auto* e : *mf.getTrack (0))
                if (e->message.isNoteOn()) v.push_back ({ e->message.getTimeStamp(), (int) e->message.getVelocity() });
        };

        proc.setMidiTempo (sd->bpm.bpm);
        const auto mids = proc.getMidiPaths();
        std::vector<std::pair<double, int>> snareHits;
        hitTimes (mids[1], snareHits);
        check (snareHits.size() > 10, "há golpes de caixa para os triggers");

        check (proc.getTriggerSourceLabel (1).startsWith ("Kit"), "kit padrão é o som inicial");
        check (! proc.anyTriggerOn(), "tudo em Original no começo");

        // --- Substituir a caixa (kit padrão), exportando com triggers
        setParam (proc, "tm1", 2.0f);
        check (proc.anyTriggerOn(), "caixa em Substituir");
        proc.rebuildTriggerPlan();
        check (proc.triggerPlanReady(), "plano de triggers pronto");

        setParam (proc, "trigExport", 1.0f);
        juce::String msg;
        const auto trigOut = outDir.getChildFile ("export_trig");
        trigOut.deleteRecursively();
        check (proc.exportStems (trigOut, 0, msg), "exportar com triggers");
        std::printf ("  %s\n", msg.toRawUTF8());
        check (msg.contains ("triggers"), "a mensagem informa que os triggers foram aplicados");

        std::vector<float> origSnare, newSnare, origKick, newKick;
        readWav (trigOut.getChildFile (stemFileName (src->name, 1)), newSnare);
        readWav (trigOut.getChildFile (stemFileName (src->name, 0)), newKick);
        const auto stemDir = sd->folder;
        readWav (stemDir.getChildFile (stemFileName (src->name, 1)), origSnare);
        readWav (stemDir.getChildFile (stemFileName (src->name, 0)), origKick);
        check (origKick == newKick, "peças em Original saem idênticas");
        check (! newSnare.empty() && newSnare.size() == origSnare.size(), "caixa trocada tem o mesmo comprimento");

        // alinhamento + dinâmica: o pico do sample em cada golpe acompanha o pico do original
        const double fs = sd->stems[0]->sampleRate;
        std::vector<double> po, pn;
        int aligned = 0;
        for (auto& h : snareHits)
        {
            const int64_t c = (int64_t) (h.first * fs);
            auto peakIn = [&] (const std::vector<float>& x, int64_t a, int64_t b)
            {
                float m = 0.0f;
                for (int64_t i = std::max<int64_t> (0, a); i < std::min<int64_t> ((int64_t) x.size(), b); ++i) m = std::max (m, std::abs (x[(size_t) i]));
                return m;
            };
            const int64_t w = (int64_t) (0.03 * fs);
            po.push_back (peakIn (origSnare, c - (int64_t) (0.005 * fs), c + w));
            pn.push_back (peakIn (newSnare, c - (int64_t) (0.005 * fs), c + w));
            // o ataque do sample cai a no máximo 3 ms do golpe
            const int64_t e = (int64_t) (0.003 * fs);
            if (peakIn (newSnare, c - e, c + e + (int64_t) (0.004 * fs)) > 0.5f * pn.back()) ++aligned;
        }
        double mo = 0, mn = 0;
        for (size_t i = 0; i < po.size(); ++i) { mo += po[i]; mn += pn[i]; }
        mo /= (double) po.size(); mn /= (double) po.size();
        double sxy = 0, sxx = 0, syy = 0;
        for (size_t i = 0; i < po.size(); ++i) { sxy += (po[i] - mo) * (pn[i] - mn); sxx += (po[i] - mo) * (po[i] - mo); syy += (pn[i] - mn) * (pn[i] - mn); }
        const double corr = sxy / std::sqrt (sxx * syy + 1e-12);
        std::printf ("  golpes alinhados: %d/%d, correlação de dinâmica original x trigger: %.2f\n", aligned, (int) snareHits.size(), corr);
        check (aligned >= (int) snareHits.size() * 9 / 10, "o ataque do sample cai em cima do golpe (<= 3 ms)");
        check (corr > 0.7, "a intensidade do sample acompanha a dinâmica do músico");

        // --- arrastar usa os arquivos com trigger
        auto dragPaths = proc.getStemPaths();
        check (dragPaths.size() == 6 && dragPaths[1].contains ("Triggers") && ! dragPaths[0].contains ("Triggers"),
               "arrastar: só as peças trocadas vêm da pasta Triggers");
        setParam (proc, "trigExport", 0.0f);
        check (! proc.getStemPaths()[1].contains ("Triggers"), "sem 'exportar com triggers', arrasta os originais");

        // --- Player: ouvir só a caixa, Original x Substituir x Somar
        setParam (proc, "s1", 1.0f);
        auto playEnergy = [&] (int mode)
        {
            setParam (proc, "tm1", (float) mode);
            proc.seekSeconds (2.0);
            proc.play();
            std::vector<float> l, r;
            run (proc, 400, bs, l, r);
            proc.pause();
            double e = 0; bool bad = false;
            for (float v : l) { e += (double) v * v; bad = bad || ! std::isfinite (v); }
            check (! bad, "saída sem NaN (modo " + juce::String (mode) + ")");
            return std::make_pair (e, l);
        };
        auto [eO, lO] = playEnergy (0);
        auto [eS, lS] = playEnergy (2);
        auto [eA, lA] = playEnergy (1);
        std::printf ("  energia da caixa: original %.3g | substituir %.3g | somar %.3g\n", eO, eS, eA);
        check (eO > 0 && eS > 0, "o player toca a caixa original e a trocada");
        double diff = 0; for (size_t i = 0; i < lO.size(); ++i) diff += std::abs (lO[i] - lS[i]);
        check (diff > 1.0, "Substituir soa diferente do original");
        check (eA > eO * 0.9, "Somar tem pelo menos a energia do original");
        setParam (proc, "tl1", -12.0f);
        auto [eS2, lS2] = playEnergy (2);
        check (eS2 < eS * 0.2, "o nível do trigger (-12 dB) abaixa o sample");
        setParam (proc, "tl1", 0.0f);
        setParam (proc, "s1", 0.0f);

        // --- samples do usuário: 1 arquivo = 1 intensidade
        auto makeTone = [&] (const juce::String& name, float amp)
        {
            const auto f = outDir.getChildFile (name);
            f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 44100.0, 1, 16, {}, 0));
            juce::AudioBuffer<float> b (1, 8000);
            for (int i = 0; i < 8000; ++i) b.setSample (0, i, amp * std::sin (2.0f * juce::MathConstants<float>::pi * 200.0f * (float) i / 44100.0f) * std::exp (-(float) i / 1500.0f));
            w->writeFromAudioSampleBuffer (b, 0, 8000);
            return f.getFullPathName();
        };
        juce::StringArray mine { makeTone ("meu_1_suave.wav", 0.2f), makeTone ("meu_2_forte.wav", 0.9f) };
        check (proc.setTriggerSamples (0, mine, msg), "importar 2 samples para o bumbo");
        check (proc.getTriggerSourceLabel (0).contains ("samples seus"), "o rótulo mostra samples do usuário");
        juce::String bad;
        check (! proc.setTriggerSamples (0, { outDir.getChildFile ("nao_existe.wav").getFullPathName() }, bad) && bad.isNotEmpty(),
               "arquivo inválido gera aviso e não altera o kit");
        check (proc.getTriggerSourceLabel (0).contains ("samples seus"), "kit anterior preservado após erro");

        setParam (proc, "tm0", 2.0f);
        setParam (proc, "trigExport", 1.0f);
        const auto trigOut2 = outDir.getChildFile ("export_trig2");
        trigOut2.deleteRecursively();
        check (proc.exportStems (trigOut2, 0, msg), "exportar com samples do usuário");
        readWav (trigOut2.getChildFile (stemFileName (src->name, 0)), newKick);
        float kPeak = 0; for (float v : newKick) kPeak = std::max (kPeak, std::abs (v));
        check (kPeak > 0.1f && kPeak <= 1.2f, "bumbo trocado pelo sample do usuário tem volume razoável");

        // o estado do projeto guarda os samples do usuário
        juce::MemoryBlock st;
        proc.getStateInformation (st);
        {
            BateriaSepProcessor p2;
            p2.setPlayConfigDetails (2, 2, sr, bs);
            p2.setStateInformation (st.getData(), (int) st.getSize());
            check (p2.getTriggerSourceLabel (0).contains ("samples seus"), "o projeto reabre com os samples do usuário");
            check (p2.getTriggerSourceLabel (1).startsWith ("Kit"), "e com o kit padrão nas outras peças");
        }

        proc.useDefaultTriggerKit (0);
        check (proc.getTriggerSourceLabel (0).startsWith ("Kit"), "voltar ao kit padrão");

        setParam (proc, "tm0", 2.0f); setParam (proc, "tm1", 1.0f); setParam (proc, "tm3", 2.0f);
        snapshot (proc, *editor, outDir.getChildFile ("ui_triggers.png"));
        setParam (proc, "trigExport", 0.0f);
        for (int k = 0; k < 6; ++k) setParam (proc, "tm" + juce::String (k), 0.0f);
    }

    std::printf ("== Reabrir projeto ==\n");
    setParam (proc, "g2", -9.0f);
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    {
        BateriaSepProcessor proc2;
        proc2.setPlayConfigDetails (2, 2, sr, bs);
        proc2.prepareToPlay (sr, bs);
        proc2.setStateInformation (state.getData(), (int) state.getSize());
        for (int i = 0; i < 100 && (proc2.isBusy() || proc2.getStems() == nullptr); ++i) pump (200);
        pump (200);
        auto s2 = proc2.getStems();
        check (s2 != nullptr && s2->stems.size() == 6, "stems recarregados da pasta sem separar de novo");
        check (std::abs (proc2.apvts.getRawParameterValue ("g2")->load() + 9.0f) < 0.05f, "faders restaurados");
        if (s2) std::printf ("  BPM restaurado = %.1f\n", s2->bpm.bpm);
    }

    editor.reset();
    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d FALHA(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
