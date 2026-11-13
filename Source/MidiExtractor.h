#pragma once

// Transforma o áudio de cada peça separada (kick, snare...) em notas MIDI com velocity.
// Não depende do JUCE (só da biblioteca padrão), para poder ser testado isoladamente.
//
// Ideia: cada peça já está isolada, então basta achar os ataques (subidas bruscas do envelope de
// amplitude) e medir a força de cada um. A velocity vem do pico do ataque, em relação aos ataques
// mais fortes daquela mesma peça (curva em raiz quadrada, parecida com a de bateria eletrônica).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bateriasep
{

struct MidiHit
{
    double time = 0.0;        // segundos desde o início do arquivo
    int note = 36;            // número da nota (mapa General MIDI de bateria)
    int velocity = 100;       // 1..127
    double duration = 0.1;    // segundos
    float peak = 0.0f;        // pico linear do ataque (para diagnóstico)
};

// Notas do mapa General MIDI
namespace gm
{
constexpr int kick = 36, snare = 38, hatClosed = 42, hatOpen = 46, ride = 51, crash = 49;
constexpr int tomLowFloor = 43, tomLow = 45, tomMid = 47, tomHigh = 50;
}

namespace midi_detail
{
inline float percentile (std::vector<float> v, double p)
{
    if (v.empty()) return 0.0f;
    const size_t k = std::min (v.size() - 1, (size_t) std::floor (p * (double) (v.size() - 1) + 0.5));
    std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) k, v.end());
    return v[k];
}

// Altura aproximada (Hz) de um golpe de tom, por autocorrelação. 0 = não deu para medir.
inline double estimatePitch (const std::vector<float>& mono, size_t startSample, double sr)
{
    const size_t D = std::max<size_t> (1, (size_t) std::lround (sr / 11025.0));
    const double fsd = sr / (double) D;
    const size_t minLag = (size_t) std::max (8.0, fsd / 420.0);
    const size_t maxLag = (size_t) (fsd / 55.0);
    const size_t W = (size_t) (0.060 * fsd);
    const size_t need = (W + maxLag + 2) * D;
    if (startSample + need >= mono.size()) return 0.0;

    std::vector<float> x (W + maxLag + 2);
    for (size_t j = 0; j < x.size(); ++j)
    {
        float s = 0.0f;
        for (size_t d = 0; d < D; ++d) s += mono[startSample + j * D + d];
        x[j] = s / (float) D;
    }

    std::vector<double> r (maxLag + 2, 0.0);
    double e0 = 0.0;
    for (size_t j = 0; j < W; ++j) e0 += (double) x[j] * x[j];
    if (e0 < 1e-12) return 0.0;
    for (size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double c = 0.0, e = 0.0;
        for (size_t j = 0; j < W; ++j) { c += (double) x[j] * x[j + lag]; e += (double) x[j + lag] * x[j + lag]; }
        r[lag] = c / std::sqrt (e0 * e + 1e-20);
    }
    double best = 0.0;
    for (size_t lag = minLag; lag <= maxLag; ++lag) best = std::max (best, r[lag]);
    if (best < 0.35) return 0.0;
    for (size_t lag = minLag + 1; lag < maxLag; ++lag)
        if (r[lag] >= 0.85 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1])
        {
            const double a = r[lag - 1], b = r[lag], c = r[lag + 1];
            const double den = a - 2.0 * b + c;
            const double off = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
            return fsd / ((double) lag + off);
        }
    return 0.0;
}
} // namespace midi_detail

// stem: 0 Kick, 1 Snare, 2 Toms, 3 Hi-Hat, 4 Ride, 5 Crash.  sens: 0..1 (maior = mais notas, inclusive as bem fracas)
// globalPeak: pico (linear) da bateria inteira. Serve de referência absoluta para descartar peças que só têm
// vazamento (ex.: o "ride" de uma gravação em que ninguém tocou ride). 0 = não usar.
inline std::vector<MidiHit> extractHits (const float* left, const float* right, size_t n, double sr, int stem, float sens,
                                         float globalPeak = 0.0f)
{
    using namespace midi_detail;
    std::vector<MidiHit> out;
    const size_t H = std::max<size_t> (1, (size_t) std::lround (sr * 0.001));   // blocos de 1 ms
    const size_t nb = n / H;
    if (nb < 40 || sr < 8000.0) return out;
    sens = std::min (1.0f, std::max (0.0f, sens));

    static const double refractoryMs[6] = { 40, 35, 40, 28, 35, 150 };
    static const double riseExtraDb[6]  = { 0, 0, 0, 1.5, 1.5, 4.0 };
    static const int baseNote[6] = { gm::kick, gm::snare, gm::tomMid, gm::hatClosed, gm::ride, gm::crash };

    // --- envelope de pico a cada 1 ms ------------------------------------------------------------
    std::vector<float> env (nb), mono (n);
    for (size_t i = 0; i < n; ++i) mono[i] = 0.5f * (left[i] + right[i]);
    for (size_t k = 0; k < nb; ++k)
    {
        float m = 0.0f;
        for (size_t i = k * H; i < (k + 1) * H; ++i) m = std::max (m, std::max (std::abs (left[i]), std::abs (right[i])));
        env[k] = m;
    }
    // Graves (kick, toms) oscilam a 50-100 Hz: o pico de 1 ms cai a zero a cada meio ciclo e geraria
    // "ataques" falsos. Por isso o envelope é o maior pico dos últimos W ms (a subida continua instantânea).
    static const size_t hold[6] = { 10, 4, 10, 2, 3, 3 };
    {
        std::vector<float> raw = env;
        for (size_t k2 = 0; k2 < nb; ++k2)
        {
            float m = raw[k2];
            for (size_t j = 1; j < hold[stem] && j <= k2; ++j) m = std::max (m, raw[k2 - j]);
            env[k2] = m;
        }
    }
    std::vector<float> db (nb);
    for (size_t k = 0; k < nb; ++k) db[k] = 20.0f * std::log10 (std::max (env[k], 1e-6f));

    const float refLevel = percentile (env, 0.995);
    if (refLevel < 1e-4f) return out;                                // peça praticamente silenciosa
    const double floorDb = 14.0 + 24.0 * sens;                       // ignora o que está mais que isso abaixo do forte
    const double riseDb = 11.0 - 6.0 * sens + riseExtraDb[stem];     // subida mínima para contar como ataque
    float floorLin = refLevel * (float) std::pow (10.0, -floorDb / 20.0);
    if (globalPeak > 0.0f)
        floorLin = std::max (floorLin, globalPeak * (float) std::pow (10.0, -(38.0 + 16.0 * sens) / 20.0));
    const size_t look = 6;
    const size_t refr = std::max<size_t> (1, (size_t) refractoryMs[stem]);

    struct Cand { size_t onsetBlock, peakBlock; float peak; };
    std::vector<Cand> cands;
    size_t k = look;
    while (k + 1 < nb)
    {
        float minPrev = db[k - 1];
        for (size_t j = k - look; j < k; ++j) minPrev = std::min (minPrev, db[j]);
        if ((double) (db[k] - minPrev) >= riseDb)
        {
            const size_t winEnd = std::min (nb - 1, k + 12);
            size_t pk = k;
            for (size_t j = k; j <= winEnd; ++j) if (env[j] > env[pk]) pk = j;
            if (env[pk] >= floorLin)
            {
                size_t i = k;                                         // volta ao início da subida
                while (i > 0 && k - i < look && db[i - 1] < db[i]) --i;
                cands.push_back ({ i, pk, env[pk] });
                k = pk + refr;
                continue;
            }
        }
        ++k;
    }
    if (cands.empty()) return out;

    // --- descarta "ataques" fracos logo depois de um golpe forte: são cauda/ressonância/vazamento -------
    {
        static const double tailMs[6]    = { 300, 60, 300, 0, 0, 0 };
        static const float  tailRatio[6] = { 0.35f, 0.30f, 0.35f, 0, 0, 0 };
        if (tailMs[stem] > 0.0)
        {
            std::vector<Cand> kept;
            for (auto& c : cands)
            {
                if (! kept.empty())
                {
                    const double dt = (double) (c.onsetBlock - kept.back().onsetBlock) * (double) H * 1000.0 / sr;
                    if (dt < tailMs[stem] && c.peak < tailRatio[stem] * kept.back().peak) continue;
                }
                kept.push_back (c);
            }
            cands.swap (kept);
        }
    }
    if (cands.empty()) return out;

    // --- velocity: relativa aos ataques mais fortes da própria peça ------------------------------
    std::vector<float> peaks;
    for (auto& c : cands) peaks.push_back (c.peak);
    const float refPeak = peaks.size() >= 12 ? percentile (peaks, 0.97) : *std::max_element (peaks.begin(), peaks.end());

    // --- altura dos toms (agrupa em até 3 alturas) ------------------------------------------------
    std::vector<int> tomNote (cands.size(), gm::tomMid);
    if (stem == 2)
    {
        std::vector<double> lf (cands.size(), 0.0);
        std::vector<size_t> valid;
        for (size_t c = 0; c < cands.size(); ++c)
        {
            const double f = estimatePitch (mono, cands[c].onsetBlock * H + (size_t) (0.006 * sr), sr);
            if (f > 0.0) { lf[c] = std::log2 (f); valid.push_back (c); }
        }
        if (valid.size() >= 2)
        {
            std::vector<double> vals;
            for (auto c : valid) vals.push_back (lf[c]);
            std::sort (vals.begin(), vals.end());
            std::vector<double> centers { vals.front(), vals[vals.size() / 2], vals.back() };
            std::vector<int> assign (valid.size(), 0);
            for (int iter = 0; iter < 25; ++iter)
            {
                for (size_t a = 0; a < valid.size(); ++a)
                {
                    int bi = 0;
                    for (int q = 1; q < 3; ++q)
                        if (std::abs (lf[valid[a]] - centers[(size_t) q]) < std::abs (lf[valid[a]] - centers[(size_t) bi])) bi = q;
                    assign[a] = bi;
                }
                for (int q = 0; q < 3; ++q)
                {
                    double s = 0; int cnt = 0;
                    for (size_t a = 0; a < valid.size(); ++a) if (assign[a] == q) { s += lf[valid[a]]; ++cnt; }
                    if (cnt > 0) centers[(size_t) q] = s / cnt;
                }
            }
            // junta alturas parecidas (menos de ~4 semitons) e descarta grupos vazios
            std::vector<int> used;
            for (int q = 0; q < 3; ++q)
            {
                bool any = false;
                for (auto a : assign) if (a == q) any = true;
                if (any) used.push_back (q);
            }
            std::sort (used.begin(), used.end(), [&] (int a, int b) { return centers[(size_t) a] < centers[(size_t) b]; });
            std::vector<int> groupOf (3, -1);
            std::vector<double> groupCenters;
            for (int q : used)
            {
                if (! groupCenters.empty() && centers[(size_t) q] - groupCenters.back() < 0.33)
                    groupOf[(size_t) q] = (int) groupCenters.size() - 1;
                else { groupOf[(size_t) q] = (int) groupCenters.size(); groupCenters.push_back (centers[(size_t) q]); }
            }
            const int ng = (int) groupCenters.size();
            static const int noteFor3[3] = { gm::tomLowFloor, gm::tomMid, gm::tomHigh };
            static const int noteFor2[2] = { gm::tomLow, gm::tomHigh };
            for (size_t a = 0; a < valid.size(); ++a)
            {
                const int g = groupOf[(size_t) assign[a]];
                tomNote[valid[a]] = ng >= 3 ? noteFor3[g] : (ng == 2 ? noteFor2[g] : gm::tomMid);
            }
        }
    }

    // --- monta as notas ---------------------------------------------------------------------------
    for (size_t c = 0; c < cands.size(); ++c)
    {
        MidiHit h;
        h.time = (double) (cands[c].onsetBlock * H) / sr;
        h.peak = cands[c].peak;
        h.velocity = std::min (127, std::max (1, (int) std::lround (127.0 * std::sqrt (std::min (1.0f, cands[c].peak / refPeak)))));
        h.note = baseNote[stem];
        const double nextT = c + 1 < cands.size() ? (double) (cands[c + 1].onsetBlock * H) / sr : 1e9;

        double dur = 0.10;
        if (stem == 2) h.note = tomNote[c];
        if (stem == 4) dur = 0.20;
        if (stem == 5) dur = 0.30;
        if (stem == 3)
        {
            // hi-hat aberto: o som continua forte bem depois do ataque
            const size_t a0 = cands[c].peakBlock + 60, a1 = std::min ({ cands[c].peakBlock + 160, nb, (size_t) (nextT * 1000.0) });
            if (a1 > a0 + 30)
            {
                double s = 0.0;
                for (size_t j = a0; j < a1; ++j) s += env[j];
                const double tail = s / (double) (a1 - a0);
                if (tail > 0.12 * (double) cands[c].peak)
                {
                    h.note = gm::hatOpen;
                    size_t j = a0;
                    while (j < nb && env[j] > 0.05f * cands[c].peak && j < cands[c].peakBlock + 600) ++j;
                    dur = std::max (0.15, (double) (j - cands[c].peakBlock) / 1000.0);
                }
            }
        }
        h.duration = std::max (0.02, std::min (dur, nextT - h.time - 0.002));
        out.push_back (h);
    }
    return out;
}

// Todas as peças de uma vez. Além de rodar cada peça, descarta o vazamento: hi-hat/ride/crash "tocando" no
// mesmo instante de um kick ou snare MUITO mais forte (mais de ~32 dB acima) é só o som do kick/snare que
// escapou para a outra peça.
struct StemView { const float* left = nullptr; const float* right = nullptr; size_t n = 0; };

inline std::array<std::vector<MidiHit>, 6> extractAllHits (const std::array<StemView, 6>& stems, double sr, float sens)
{
    float globalPeak = 0.0f;
    for (auto& v : stems)
        for (size_t i = 0; i < v.n; ++i)
            globalPeak = std::max (globalPeak, std::max (std::abs (v.left[i]), std::abs (v.right[i])));

    std::array<std::vector<MidiHit>, 6> res;
    for (int s = 0; s < 6; ++s)
        res[(size_t) s] = extractHits (stems[(size_t) s].left, stems[(size_t) s].right, stems[(size_t) s].n, sr, s, sens, globalPeak);

    constexpr float kMaskRatio = 40.0f;   // ~32 dB
    for (int s = 3; s <= 5; ++s)
    {
        auto& v = res[(size_t) s];
        v.erase (std::remove_if (v.begin(), v.end(), [&] (const MidiHit& h)
        {
            for (int strong = 0; strong <= 1; ++strong)
                for (auto& o : res[(size_t) strong])
                    if (std::abs (o.time - h.time) < 0.020 && o.peak > kMaskRatio * h.peak) return true;
            return false;
        }), v.end());
    }
    return res;
}

} // namespace bateriasep
