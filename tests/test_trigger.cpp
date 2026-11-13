// Teste do motor de triggers (sem JUCE).
#include <cmath>
#include <cstdio>
#include "../Source/TriggerEngine.h"

using namespace bateriasep;
static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("  [%s] %s\n", ok ? "OK" : "FALHOU", what); if (! ok) ++fails; }

constexpr double sr = 48000.0;

// sample = pulso de amplitude 'a' seguido de um decaimento curto; len amostras
static TrigSamplePtr smp (float a, size_t len, float marker = 0.0f)
{
    auto v = std::make_shared<std::vector<float>> (len, 0.0f);
    for (size_t i = 0; i < len; ++i) (*v)[i] = a * std::exp (-(float) i / (float) (len / 6)) + (i == 3 ? marker : 0.0f);
    return v;
}

static TrigVoice makeVoice (std::vector<float> amps, size_t len, int rr = 1)
{
    TrigVoice tv;
    float marker = 0.0f;
    for (float a : amps)
    {
        std::vector<TrigSamplePtr> v;
        for (int r = 0; r < rr; ++r) { v.push_back (smp (a, len, marker)); if (rr > 1) marker += 0.01f; }   // marcador = identifica a variação (só nas vozes com variações)
        tv.layers.push_back (v);
    }
    return tv;
}

static MidiHit hit (double t, int vel, int note) { MidiHit h; h.time = t; h.velocity = vel; h.note = note; return h; }

int main()
{
    auto kit = std::make_shared<TrigKit>();
    kit->piece[0].byNote[-1] = makeVoice ({ 0.1f, 0.3f, 0.6f, 0.9f }, 4800);                 // kick: 4 camadas
    kit->piece[1].byNote[-1] = makeVoice ({ 1.0f }, 4800);                                    // snare: 1 camada (volume pela velocity)
    kit->piece[3].byNote[gm::hatClosed] = makeVoice ({ 0.5f }, 2400);
    kit->piece[3].byNote[gm::hatOpen]   = makeVoice ({ 0.5f }, 48000);
    kit->piece[2].byNote[-1] = makeVoice ({ 0.5f }, 4800, 3);                                 // toms: 3 variações

    TrigPlan plan;
    plan.rate = sr;
    plan.kit = kit;
    plan.hits[0] = { hit (0.10, 20, 36), hit (0.30, 60, 36), hit (0.50, 100, 36), hit (0.70, 127, 36), hit (0.90, 127, 36) };
    plan.hits[1] = { hit (0.10, 127, 38), hit (0.30, 64, 38), hit (0.50, 32, 38) };
    plan.hits[3] = { hit (0.10, 100, gm::hatOpen), hit (0.30, 100, gm::hatClosed) };
    plan.hits[2] = { hit (0.1, 100, 47), hit (0.3, 100, 47), hit (0.5, 100, 47), hit (0.7, 100, 47) };
    plan.prepare();

    const size_t N = (size_t) (1.5 * sr);

    std::printf ("== Posição e dinâmica ==\n");
    auto kick = TrigEngine::render (plan, 0, N);
    bool atRight = true;
    double peaks[5];
    for (int i = 0; i < 5; ++i)
    {
        const size_t s0 = (size_t) std::llround (plan.hits[0][(size_t) i].time * sr);
        size_t pk = s0; float m = 0;
        for (size_t j = s0; j < s0 + 2000; ++j) if (std::abs (kick[j]) > m) { m = std::abs (kick[j]); pk = j; }
        atRight = atRight && pk == s0;
        peaks[i] = m;
    }
    check (atRight, "o pico de cada golpe cai exatamente na amostra do golpe");
    check (peaks[0] < peaks[1] && peaks[1] < peaks[2] && peaks[2] < peaks[3], "velocity maior = pico maior (kick, 4 camadas)");
    check (std::abs (peaks[3] - 0.9) < 0.15, "velocity 127 chega perto do volume da camada mais forte");
    check (std::abs (peaks[3] - peaks[4]) < 1e-6, "mesma velocity = mesmo som");
    auto snare = TrigEngine::render (plan, 1, N);
    const double a = snare[(size_t) std::llround (0.1 * sr)], b = snare[(size_t) std::llround (0.3 * sr)], c = snare[(size_t) std::llround (0.5 * sr)];
    check (std::abs (b / a - 64.0 / 127.0) < 0.02 && std::abs (c / a - 32.0 / 127.0) < 0.02, "uma camada só: volume proporcional à velocity");

    std::printf ("== Seek, loop e blocos ==\n");
    {
        // começar no meio (seek) deve dar o mesmo que a renderização inteira, inclusive a cauda dos golpes anteriores
        TrigEngine e;
        e.reset();
        double maxDiff = 0;
        for (size_t i = (size_t) (0.62 * sr); i < (size_t) (1.2 * sr); ++i) maxDiff = std::max (maxDiff, (double) std::abs (e.tick (plan, 0, (int64_t) i) - kick[i]));
        check (maxDiff < 1e-6, "tocar a partir do meio = mesmo som da renderização completa");

        // loop: depois de chegar no fim, volta ao começo da seleção
        TrigEngine l;
        l.reset();
        std::vector<float> loop;
        const int64_t s0 = (int64_t) (0.05 * sr), s1 = (int64_t) (0.45 * sr);
        int64_t pos = s0;
        for (int i = 0; i < (int) ((s1 - s0) * 3); ++i) { if (pos >= s1) pos = s0; loop.push_back (l.tick (plan, 0, pos)); ++pos; }
        bool same = true;
        for (size_t i = 0; i < (size_t) (s1 - s0); ++i) if (std::abs (loop[i] - loop[i + (size_t) (s1 - s0)]) > 1e-5f) same = false;
        check (same, "repetir a seleção (loop) soa igual a cada volta");
    }

    std::printf ("== Round-robin e hi-hat ==\n");
    auto toms = TrigEngine::render (plan, 2, N);
    float mk[4];
    for (int i = 0; i < 4; ++i) mk[i] = toms[(size_t) std::llround (plan.hits[2][(size_t) i].time * sr) + 3];
    check (std::abs (mk[0] - mk[3]) < 1e-6 && std::abs (mk[0] - mk[1]) > 1e-4 && std::abs (mk[1] - mk[2]) > 1e-4, "variações alternam 1,2,3,1 de forma determinística");
    auto hat = TrigEngine::render (plan, 3, N);
    TrigPlan onlyClosed = plan; onlyClosed.hits[3] = { hit (0.30, 100, gm::hatClosed) }; onlyClosed.prepare();
    auto hatC = TrigEngine::render (onlyClosed, 3, N);
    TrigPlan onlyOpen = plan; onlyOpen.hits[3] = { hit (0.10, 100, gm::hatOpen) }; onlyOpen.prepare();
    auto hatO = TrigEngine::render (onlyOpen, 3, N);
    double leftover = 0, openTail = 0;
    for (size_t i = (size_t) (0.31 * sr); i < (size_t) (0.45 * sr); ++i)
    {
        leftover = std::max (leftover, (double) std::abs (hat[i] - hatC[i]));   // o que sobra do aberto depois do fechado
        openTail = std::max (openTail, (double) std::abs (hatO[i]));            // o que o aberto faria sozinho
    }
    check (openTail > 0.05 && leftover < openTail * 0.05, "hi-hat fechado corta o aberto");

    std::printf ("== Casos de borda ==\n");
    TrigPlan empty; empty.rate = sr; empty.kit = kit; empty.prepare();
    check (TrigEngine::render (empty, 0, 1000)[500] == 0.0f, "sem golpes = silêncio");
    TrigPlan nokit; nokit.rate = sr; nokit.hits[0] = { hit (0.0, 100, 36) }; nokit.prepare();
    check (TrigEngine::render (nokit, 0, 1000)[10] == 0.0f, "sem samples = silêncio (sem travar)");
    TrigPlan many = plan; many.hits[2].clear();
    for (int i = 0; i < 200; ++i) many.hits[2].push_back (hit (0.001 * i, 100, 47));
    many.prepare();
    auto mm = TrigEngine::render (many, 2, N);
    check (std::isfinite (mm[1000]), "200 golpes seguidos (mais que o limite de vozes) não quebram");

    std::printf (fails ? "\nFALHAS: %d\n" : "\nTODOS OS TESTES PASSARAM\n", fails);
    return fails ? 1 : 0;
}
