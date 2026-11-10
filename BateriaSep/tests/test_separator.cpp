// Testes do núcleo de separação. Compara com números gerados pelo Python (PyTorch original).
//
//   test_separator <pasta_dos_bins> <drumsep_core.onnx> <libonnxruntime> [full|load]
//
// Sem "full": testa STFT, ISTFT e reamostrador (rápido).
// Com "full": roda a separação inteira e compara com as faixas de referência (~2-3 min).

#include "DrumSeparator.h"
#include "OrtLoader.h"

#include <chrono>
#include <cstdio>
#include <fstream>

using namespace bateriasep;

static std::vector<float> readBin (const std::string& path)
{
    std::ifstream f (path, std::ios::binary | std::ios::ate);
    if (! f) { std::printf ("não consegui abrir %s\n", path.c_str()); std::exit (2); }
    const size_t bytes = (size_t) f.tellg();
    f.seekg (0);
    std::vector<float> v (bytes / sizeof (float));
    f.read ((char*) v.data(), (std::streamsize) bytes);
    return v;
}

static double maxAbsDiff (const float* a, const float* b, size_t n)
{
    double m = 0;
    for (size_t i = 0; i < n; ++i) m = std::max (m, (double) std::abs (a[i] - b[i]));
    return m;
}

int main (int argc, char** argv)
{
    if (argc < 4) { std::printf ("uso: %s <bins> <modelo.onnx> <libonnxruntime> [full|load]\n", argv[0]); return 2; }
    const std::string dir = std::string (argv[1]) + "/";
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("  [%s] %s\n", ok ? "OK" : "FALHA", what); if (! ok) ++failures; };

    Stft stft;

    // --- 1. STFT contra o torch.stft ---------------------------------------------------------
    {
        auto wav = readBin (dir + "wav_in.bin");   // [2][kChunk]
        auto ref = readBin (dir + "spec_in.bin");  // [4][kDimF][kDimT]
        std::vector<float> spec (ref.size());
        stft.forwardChannel (wav.data(), kChunk, 0, spec.data());
        stft.forwardChannel (wav.data() + kChunk, kChunk, 1, spec.data());
        double peak = 0; for (float v : ref) peak = std::max (peak, (double) std::abs (v));
        const double d = maxAbsDiff (spec.data(), ref.data(), ref.size());
        std::printf ("STFT: maior diferença %.3g (pico do espectro %.1f)\n", d, peak);
        check (d < 1e-3 * peak, "STFT igual ao torch.stft");
    }

    // --- 2. ISTFT contra o torch.istft (espectro de saída da rede -> áudio) ---------------------
    {
        auto spec = readBin (dir + "spec_out.bin");   // [6][4][kDimF][kDimT]
        auto ref  = readBin (dir + "wav_ref.bin");    // [6][2][kChunk]
        const size_t specSize = (size_t) 4 * kDimF * kDimT;
        std::vector<float> L ((size_t) kChunk), R ((size_t) kChunk);
        double worst = 0;
        for (int s = 0; s < kNumStems; ++s)
        {
            stft.inverseStereo (spec.data() + (size_t) s * specSize, kDimT, L.data(), R.data());
            worst = std::max (worst, maxAbsDiff (L.data(), ref.data() + ((size_t) s * 2) * kChunk, kChunk));
            worst = std::max (worst, maxAbsDiff (R.data(), ref.data() + ((size_t) s * 2 + 1) * kChunk, kChunk));
        }
        std::printf ("ISTFT: maior diferença %.3g\n", worst);
        check (worst < 2e-3, "ISTFT igual ao torch.istft");
    }

    // --- 3. Reamostrador: seno 1 kHz 48k -> 44,1k -> 48k --------------------------------------
    {
        const size_t n = 48000;
        std::vector<float> x (n);
        for (size_t i = 0; i < n; ++i) x[i] = (float) (0.5 * std::sin (2 * kPi * 1000.0 * (double) i / 48000.0)
                                                      + 0.2 * std::sin (2 * kPi * 15000.0 * (double) i / 48000.0));
        auto a = Resampler::process (x.data(), x.size(), 48000.0, 44100.0);
        auto b = Resampler::process (a.data(), a.size(), 44100.0, 48000.0);
        double err = 0;
        const size_t from = 2000, to = std::min (b.size(), n) - 2000;
        for (size_t i = from; i < to; ++i) err = std::max (err, (double) std::abs (b[i] - x[i]));
        std::printf ("Reamostrador: comprimento %zu -> %zu -> %zu, erro máx. ida e volta %.2g\n", n, a.size(), b.size(), err);
        check (a.size() == 44100 && b.size() == 48000, "comprimentos corretos");
        check (err < 1e-3, "erro de ida e volta < -60 dB");
    }

    const std::string mode = argc >= 5 ? argv[4] : "";
    if (mode == "full" || mode == "load")
    {
        std::string error;
        check (initOnnxRuntime (argv[3], error), "carregar a biblioteca do ONNX Runtime pelo caminho");
        if (! error.empty()) { std::printf ("  %s\n", error.c_str()); return 1; }
        DrumSeparator sep;
        check (sep.load (argv[2], error), "carregar modelo ONNX");
        if (! sep.isLoaded()) { std::printf ("  %s\n", error.c_str()); return 1; }

        auto inL = readBin (dir + "full_in_L.bin");
        auto inR = readBin (dir + "full_in_R.bin");
        auto ref = readBin (dir + "full_ref.bin");   // [6][2][n]
        size_t n = inL.size();
        if (mode == "load") n = std::min<size_t> (n, 100000);   // só 1 bloco: confere carga do modelo e execução

        StemSet out;
        const auto t0 = std::chrono::steady_clock::now();
        int lastPct = -1;
        const bool ok = sep.separate (inL.data(), inR.data(), n, 4, out,
                                      [&] (float p) { const int pct = (int) (p * 100); if (pct / 10 != lastPct / 10) { std::printf ("  %d%%\n", pct); std::fflush (stdout); } lastPct = pct; return true; },
                                      error);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        check (ok, "separação completa");
        if (! ok) { std::printf ("  %s\n", error.c_str()); return 1; }
        std::printf ("  tempo: %.1f s para %.1f s de áudio\n", secs, (double) n / kModelRate);

        if (mode == "load")
        {
            double peak = 0;
            for (auto& st : out) for (float v : st[0]) peak = std::max (peak, (double) std::abs (v));
            std::printf ("  pico das saídas: %.3f\n", peak);
            check (peak > 0.01, "o modelo produziu áudio");
            std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d FALHA(S)\n", failures);
            return failures == 0 ? 0 : 1;
        }

        double refPow = 0, errPow = 0;
        for (int s = 0; s < kNumStems; ++s)
        {
            double sp = 0, se = 0;
            for (int c = 0; c < 2; ++c)
            {
                const float* r = ref.data() + ((size_t) s * 2 + (size_t) c) * inL.size();
                const auto& o = out[(size_t) s][(size_t) c];
                for (size_t i = 0; i < n; ++i) { sp += (double) r[i] * r[i]; se += (double) (o[i] - r[i]) * (o[i] - r[i]); }
            }
            std::printf ("  %-7s erro relativo: %.1f dB abaixo do sinal\n", stemName (s), 10 * std::log10 ((sp + 1e-20) / (se + 1e-20)));
            refPow += sp; errPow += se;
        }
        const double snr = 10 * std::log10 (refPow / (errPow + 1e-20));
        std::printf ("  total: %.1f dB\n", snr);
        check (snr > 50.0, "stems iguais aos do Python (> 50 dB)");
    }

    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d FALHA(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
