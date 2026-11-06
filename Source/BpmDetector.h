#pragma once

// Detector de andamento (BPM) para bateria. Sem dependências.
//
// 1. Curva de "ataques" (onset): energia em quadros curtos -> log -> diferença positiva.
// 2. Autocorrelação da curva, com reforço dos múltiplos do período (compasso) e preferência
//    suave por andamentos "naturais" (resolve a confusão entre 60 / 120 / 240 BPM).
// 3. Refino com interpolação parabólica e busca da fase da grade de batidas.

#include <algorithm>
#include <cmath>
#include <vector>

namespace bateriasep
{

struct BpmResult
{
    bool valid = false;
    double bpm = 0.0;          // andamento mais provável
    double halfBpm = 0.0;      // alternativa na metade (ex.: 60 se bpm = 120)
    double doubleBpm = 0.0;    // alternativa no dobro
    double confidence = 0.0;   // 0..1 (quão destacado é o pico)
    double firstBeatSec = 0.0; // posição da primeira batida da grade
};

class BpmDetector
{
public:
    static constexpr double kMinBpm = 50.0;
    static constexpr double kMaxBpm = 220.0;

    static BpmResult estimate (const float* x, size_t n, double sampleRate)
    {
        BpmResult res;
        const int hop = 256, win = 512;
        if (n < (size_t) (sampleRate * 4.0)) return res;           // precisa de pelo menos ~4 s

        const double fps = sampleRate / hop;                        // quadros por segundo
        const size_t frames = (n - (size_t) win) / (size_t) hop;

        // --- 1. curva de ataques ---------------------------------------------------------
        std::vector<double> energy (frames);
        for (size_t t = 0; t < frames; ++t)
        {
            double e = 0.0;
            const float* p = x + t * (size_t) hop;
            for (int i = 0; i < win; ++i) e += (double) p[i] * p[i];
            energy[t] = std::log (1.0 + 1.0e4 * e / win);
        }

        std::vector<double> onset (frames, 0.0);
        for (size_t t = 1; t < frames; ++t)
            onset[t] = std::max (0.0, energy[t] - energy[t - 1]);

        // remove a média local (≈ 0,5 s) para destacar só os picos
        {
            const int w = (int) (0.25 * fps);
            std::vector<double> cs (frames + 1, 0.0);
            for (size_t t = 0; t < frames; ++t) cs[t + 1] = cs[t] + onset[t];
            std::vector<double> out (frames);
            for (size_t t = 0; t < frames; ++t)
            {
                const size_t a = t > (size_t) w ? t - (size_t) w : 0;
                const size_t b = std::min (frames, t + (size_t) w + 1);
                const double mean = (cs[b] - cs[a]) / (double) (b - a);
                out[t] = std::max (0.0, onset[t] - mean);
            }
            onset.swap (out);
        }

        double total = 0.0;
        for (double v : onset) total += v;
        if (total < 1e-6) return res;                                // silêncio

        // --- 2. autocorrelação ----------------------------------------------------------
        const int minLag = (int) std::floor (fps * 60.0 / kMaxBpm);
        const int maxLag = (int) std::ceil (fps * 60.0 / kMinBpm);
        const int needed = maxLag * 4 + 2;                           // para os múltiplos
        std::vector<double> r ((size_t) needed, 0.0);
        for (int lag = minLag / 2; lag < needed && (size_t) lag < frames / 2; ++lag)
        {
            double s = 0.0;
            for (size_t t = 0; t + (size_t) lag < frames; ++t) s += onset[t] * onset[t + (size_t) lag];
            r[(size_t) lag] = s / (double) (frames - (size_t) lag);
        }

        auto at = [&] (double lag) -> double {                       // r com interpolação linear
            if (lag < 0 || lag >= (double) needed - 1) return 0.0;
            const int i = (int) lag; const double f = lag - i;
            return r[(size_t) i] * (1 - f) + r[(size_t) i + 1] * f;
        };

        std::vector<double> score ((size_t) maxLag + 1, 0.0);
        double bestScore = -1.0, sumScore = 0.0; int bestLag = 0, cnt = 0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double bpm = 60.0 * fps / lag;
            const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 115.0) / 0.9, 2.0));
            const double s = (at (lag) + 0.5 * at (2.0 * lag) + 0.25 * at (4.0 * lag)) * prior;
            score[(size_t) lag] = s;
            sumScore += s; ++cnt;
            if (s > bestScore) { bestScore = s; bestLag = lag; }
        }
        if (bestLag == 0 || bestScore <= 0) return res;

        // --- 3. refino parabólico no pico da autocorrelação ---------------------------------
        double lagF = bestLag;
        {
            const double a = at (bestLag - 1), b = at (bestLag), c = at (bestLag + 1);
            const double den = a - 2 * b + c;
            if (std::abs (den) > 1e-12) lagF += std::max (-1.0, std::min (1.0, 0.5 * (a - c) / den));
        }

        res.bpm = 60.0 * fps / lagF;
        res.halfBpm = res.bpm * 0.5;
        res.doubleBpm = res.bpm * 2.0;
        res.confidence = std::max (0.0, std::min (1.0, 1.0 - (sumScore / cnt) / bestScore));

        // --- 4. fase da grade de batidas ---------------------------------------------------
        double bestPhase = 0.0, bestPhaseScore = -1.0;
        const int steps = (int) std::ceil (lagF);
        for (int ph = 0; ph < steps; ++ph)
        {
            double s = 0.0;
            for (double pos = ph; pos < (double) frames - 1; pos += lagF)
            {
                const size_t i = (size_t) pos;
                s += onset[i] + (i + 1 < frames ? onset[i + 1] : 0.0) + (i > 0 ? onset[i - 1] : 0.0);
            }
            if (s > bestPhaseScore) { bestPhaseScore = s; bestPhase = ph; }
        }
        res.firstBeatSec = (bestPhase * hop + win * 0.5) / sampleRate;
        res.valid = true;
        return res;
    }
};

} // namespace bateriasep
