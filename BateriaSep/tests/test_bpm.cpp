// Teste do detector de BPM.
//   test_bpm                    -> sinais sintéticos de bateria em vários andamentos
//   test_bpm <full_ref.bin> <n> -> stems reais (kick+snare+toms), imprime o resultado
#include "BpmDetector.h"
#include <cstdio>
#include <fstream>
#include <random>

constexpr double kPi = 3.14159265358979323846;

using namespace bateriasep;

static std::vector<float> synth (double bpm, double seconds, double sr, unsigned seed, double swingJitter, bool hats)
{
    const size_t n = (size_t) (seconds * sr);
    std::vector<float> x (n, 0.0f);
    std::mt19937 rng (seed);
    std::normal_distribution<float> g (0.0f, 1.0f);
    std::normal_distribution<double> jit (0.0, swingJitter);
    const double beat = 60.0 / bpm;

    auto hit = [&] (double t, int kind) {
        const size_t s0 = (size_t) std::max (0.0, (t + jit (rng)) * sr);
        const int len = kind == 0 ? (int) (0.25 * sr) : kind == 1 ? (int) (0.18 * sr) : (int) (0.05 * sr);
        for (int i = 0; i < len && s0 + (size_t) i < n; ++i)
        {
            const double tt = i / sr;
            double v;
            if (kind == 0)      v = std::sin (2 * kPi * (50.0 + 80.0 * std::exp (-tt * 30)) * tt) * std::exp (-tt * 14) * 0.9;
            else if (kind == 1) v = (g (rng) * 0.5 + std::sin (2 * kPi * 190 * tt) * 0.4) * std::exp (-tt * 22) * 0.7;
            else                v = g (rng) * std::exp (-tt * 90) * 0.25;
            x[s0 + (size_t) i] += (float) v;
        }
    };

    for (int b = 0; b * beat < seconds - 0.3; ++b)
    {
        const double t = b * beat;
        if (b % 4 == 0 || b % 4 == 2) hit (t, 0);          // bumbo nos tempos 1 e 3
        if (b % 4 == 1 || b % 4 == 3) hit (t, 1);          // caixa nos tempos 2 e 4
        if (hats) { hit (t, 2); hit (t + beat / 2, 2); }   // chimbal em colcheias
    }
    return x;
}

int main (int argc, char** argv)
{
    const double sr = 44100.0;

    if (argc >= 3)
    {
        const size_t n = (size_t) std::stoul (argv[2]);
        std::ifstream f (argv[1], std::ios::binary);
        std::vector<float> all (6 * 2 * n);
        f.read ((char*) all.data(), (std::streamsize) (all.size() * sizeof (float)));
        std::vector<float> mix (n, 0.0f);
        for (int s = 0; s < 3; ++s)   // kick + snare + toms (sem chimbal)
            for (size_t i = 0; i < n; ++i) mix[i] += all[((size_t) s * 2) * n + i];
        auto r = BpmDetector::estimate (mix.data(), n, sr);
        std::printf ("REAL: valid=%d bpm=%.2f (metade %.1f, dobro %.1f) confiança=%.2f primeira batida=%.3f s\n",
                     (int) r.valid, r.bpm, r.halfBpm, r.doubleBpm, r.confidence, r.firstBeatSec);
        return 0;
    }

    int failures = 0;
    const double tests[] = { 70.0, 85.0, 97.0, 110.0, 123.4, 140.0, 150.0, 174.0 };
    for (double bpm : tests)
    {
        for (double jitter : { 0.0, 0.006 })           // 6 ms de "imperfeição humana"
        {
            auto x = synth (bpm, 24.0, sr, 7, jitter, false);   // como no plugin: sem chimbal
            auto r = BpmDetector::estimate (x.data(), x.size(), sr);
            const double err = std::abs (r.bpm - bpm);
            // Andamentos muito rápidos são ambíguos com "meio tempo" (87 x 174). Nesses casos o
            // plugin mostra as duas opções, então basta a alternativa (metade/dobro) estar certa.
            const bool octave = bpm >= 160.0 && r.valid && std::abs (r.doubleBpm - bpm) < 0.02 * bpm;
            const bool ok = r.valid && (err < 0.02 * bpm || octave);
            std::printf ("[%s] real %.1f jitter %.0f ms -> detectado %.2f (conf %.2f)%s\n", ok ? "OK" : "FALHA", bpm, jitter * 1000, r.bpm, r.confidence,
                         (octave && err >= 0.02 * bpm) ? "  [oitava: dobro correto]" : "");
            if (! ok) ++failures;
        }
    }

    std::vector<float> silence ((size_t) (10 * sr), 0.0f);
    const auto rs = BpmDetector::estimate (silence.data(), silence.size(), sr);
    std::printf ("[%s] silêncio -> sem resultado\n", rs.valid ? "FALHA" : "OK");
    if (rs.valid) ++failures;

    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d FALHA(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
