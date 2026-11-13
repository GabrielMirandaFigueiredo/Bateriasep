// Teste do extrator de MIDI com baterias sintéticas de resposta conhecida.
#include <cmath>
#include <cstdio>
#include <random>
#include "../Source/MidiExtractor.h"

using namespace bateriasep;
static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("  [%s] %s\n", ok ? "OK" : "FALHOU", what); if (! ok) ++fails; }

constexpr double sr = 44100.0;
static std::mt19937 rng (7);
static std::normal_distribution<float> gauss (0.0f, 1.0f);

struct Ev { double t; float a; double f; double tau; };

// sintetiza: senoide (f>0) ou ruído (f==0) com decaimento exponencial, mais ruído de fundo
static std::vector<float> synth (double seconds, const std::vector<Ev>& evs, float floorAmp = 1e-4f)
{
    std::vector<float> x ((size_t) (seconds * sr));
    for (auto& v : x) v = floorAmp * gauss (rng);
    for (auto& e : evs)
        for (size_t i = (size_t) (e.t * sr); i < x.size(); ++i)
        {
            const double t = (double) i / sr - e.t;
            if (t > e.tau * 9) break;
            const float env = (float) std::exp (-t / e.tau);
            const float osc = e.f > 0 ? (float) std::sin (2 * M_PI * e.f * t) : 0.5f * gauss (rng);
            x[i] += e.a * env * osc;
        }
    return x;
}

int main()
{
    // ---- Kick: 10 batidas, forças variadas -------------------------------------------------------
    std::printf("== Kick ==\n");
    std::vector<Ev> kev;
    const float kamp[] = { 0.9f, 0.6f, 0.3f, 0.15f, 0.8f, 0.45f, 0.9f, 0.2f, 0.7f, 0.5f };
    for (int i = 0; i < 10; ++i) kev.push_back ({ 0.5 + 0.45 * i, kamp[i], 60.0, 0.08 });
    auto k = synth (6.0, kev);
    auto h = extractHits (k.data(), k.data(), k.size(), sr, 0, 0.5f);
    check (h.size() == 10, "achou exatamente 10 batidas");
    double maxErr = 0; bool mono = true; double maxVelErr = 0;
    for (size_t i = 0; i < h.size() && i < 10; ++i)
    {
        maxErr = std::max (maxErr, std::abs (h[i].time - kev[i].t));
        maxVelErr = std::max (maxVelErr, std::abs (h[i].velocity - 127.0 * std::sqrt (kamp[i] / 0.9)));
        std::printf ("   t=%.3f (esp. %.3f)  vel=%d  (amp %.2f)\n", h[i].time, kev[i].t, h[i].velocity, kamp[i]);
        for (size_t j = 0; j < h.size() && j < 10; ++j)
            if (kamp[i] > kamp[j] && h[i].velocity <= h[j].velocity) mono = false;
    }
    check (maxErr < 0.004, "tempo dos ataques com erro menor que 4 ms");
    check (mono, "mais forte no áudio = velocity maior");
    check (maxVelErr < 6.0, "velocity perto do esperado (raiz da amplitude)");
    check (h.size() == 10 && h[0].note == 36, "nota do kick = 36");

    // ---- Snare com ghost notes e vazamento do kick -----------------------------------------------
    std::printf("== Snare (com ghost notes e vazamento) ==\n");
    std::vector<Ev> sev;
    for (int i = 0; i < 8; ++i) sev.push_back ({ 0.6 + 0.5 * i, (i % 2) ? 0.12f : 0.8f, 0.0, 0.05 });   // fortes e fantasmas (-16 dB)
    for (int i = 0; i < 8; ++i) sev.push_back ({ 0.3 + 0.5 * i, 0.006f, 70.0, 0.08 });                  // vazamento do kick (-42 dB)
    auto s = synth (5.0, sev);
    h = extractHits (s.data(), s.data(), s.size(), sr, 1, 0.5f);
    check (h.size() == 8, "8 batidas de caixa, ignorando o vazamento do kick");
    bool ghostLow = true;
    for (size_t i = 0; i < h.size(); ++i) if ((i % 2) && h[i].velocity > 60) ghostLow = false;
    check (ghostLow, "ghost notes ficam com velocity baixa");
    auto hs = extractHits (s.data(), s.data(), s.size(), sr, 1, 1.0f);
    std::printf ("   sensibilidade máxima acha %zu notas (mais que no padrão)\n", hs.size());
    check (hs.size() >= h.size(), "mais sensibilidade nunca acha menos notas");

    // ---- Toms: três alturas -----------------------------------------------------------------------
    std::printf("== Toms (3 alturas) ==\n");
    std::vector<Ev> tev;
    const double tf[] = { 80, 120, 180, 120, 80, 180, 120, 180, 80 };
    for (int i = 0; i < 9; ++i) tev.push_back ({ 0.5 + 0.5 * i, 0.7f, tf[i], 0.15 });
    auto t = synth (6.0, tev);
    h = extractHits (t.data(), t.data(), t.size(), sr, 2, 0.5f);
    check (h.size() == 9, "9 batidas de tom");
    bool okPitch = h.size() == 9;
    for (size_t i = 0; okPitch && i < 9; ++i)
    {
        const int want = tf[i] < 100 ? gm::tomLowFloor : (tf[i] < 150 ? gm::tomMid : gm::tomHigh);
        if (h[i].note != want) okPitch = false;
        std::printf ("   %.0f Hz -> nota %d\n", tf[i], h[i].note);
    }
    check (okPitch, "alturas separadas em 3 notas de tom (grave, médio, agudo)");

    // ---- Hi-hat fechado e aberto ------------------------------------------------------------------
    std::printf("== Hi-hat ==\n");
    std::vector<Ev> hev;
    for (int i = 0; i < 8; ++i) hev.push_back ({ 0.4 + 0.25 * i, 0.4f, 0.0, 0.012 });     // fechados
    hev.push_back ({ 2.6, 0.5f, 0.0, 0.22 });                                             // aberto
    hev.push_back ({ 3.8, 0.4f, 0.0, 0.012 });
    auto hh = synth (5.0, hev);
    h = extractHits (hh.data(), hh.data(), hh.size(), sr, 3, 0.5f);
    check (h.size() == 10, "10 batidas de hi-hat");
    int open = 0, closed = 0;
    for (auto& x : h) (x.note == gm::hatOpen ? open : closed)++;
    check (open == 1 && closed == 9, "1 aberto (46) e 9 fechados (42)");

    // ---- Prato longo: a cauda de ruído não pode gerar notas falsas --------------------------------
    std::printf("== Crash (cauda longa de ruído) ==\n");
    std::vector<Ev> cev { { 0.5, 0.8f, 0.0, 0.9 }, { 3.0, 0.7f, 0.0, 0.9 }, { 5.5, 0.9f, 0.0, 0.9 } };
    auto cr = synth (9.0, cev);
    h = extractHits (cr.data(), cr.data(), cr.size(), sr, 5, 0.5f);
    check (h.size() == 3, "3 crashes, sem notas falsas na cauda");
    auto rd = synth (9.0, cev);
    h = extractHits (rd.data(), rd.data(), rd.size(), sr, 4, 0.5f);
    check (h.size() == 3, "3 batidas de ride, sem notas falsas na cauda");


    // ---- Vazamento: "ride" fraco coincidindo com kick forte deve ser descartado --------------------------
    std::printf("== Vazamento entre peças ==\n");
    {
        std::vector<Ev> kk, rr;
        for (int i = 0; i < 6; ++i) kk.push_back ({ 0.5 + 0.5 * i, 0.9f, 60.0, 0.08 });
        for (int i = 0; i < 6; ++i) rr.push_back ({ 0.5 + 0.5 * i, 0.004f, 0.0, 0.05 });      // vazamento do kick no ride
        rr.push_back ({ 1.25, 0.05f, 0.0, 0.3 });                                              // ride de verdade
        auto kx = synth (4.0, kk), rx = synth (4.0, rr), z0 = synth (4.0, {});
        std::array<StemView, 6> sv;
        const std::vector<float>* ps[6] = { &kx, &z0, &z0, &z0, &rx, &z0 };
        for (int i = 0; i < 6; ++i) sv[(size_t) i] = { ps[i]->data(), ps[i]->data(), ps[i]->size() };
        auto all = extractAllHits (sv, sr, 0.5f);
        check (all[0].size() == 6, "6 kicks");
        check (all[4].size() == 1, "só o ride verdadeiro sobrou (o vazamento do kick foi descartado)");
    }

    // ---- Silêncio e áudio muito curto ----------------------------------------------------------------
    std::vector<float> z ((size_t) sr, 0.0f);
    check (extractHits (z.data(), z.data(), z.size(), sr, 0, 0.5f).empty(), "silêncio não gera notas");
    check (extractHits (z.data(), z.data(), 10, sr, 0, 0.5f).empty(), "áudio curtíssimo não quebra");

    std::printf (fails ? "\nFALHAS: %d\n" : "\nTODOS OS TESTES PASSARAM\n", fails);
    return fails ? 1 : 0;
}
