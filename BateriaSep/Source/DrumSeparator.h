#pragma once

// Núcleo de separação de bateria (sem dependência do JUCE).
//
// Reproduz em C++ o mesmo pipeline do DrumSep (MDX23C TFC-TDF) usado no Python:
//   áudio estéreo 44,1 kHz -> blocos de 130560 amostras (com sobreposição) -> STFT (2048/512)
//   -> rede neural (ONNX Runtime) -> STFT inversa -> soma ponderada dos blocos.
//
// A rede em si é o arquivo "drumsep_core.onnx" (gerado por tools/export_onnx.py).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <onnxruntime_cxx_api.h>   // (ORT_API_MANUAL_INIT é definido no build; veja OrtLoader.cpp)

namespace bateriasep
{

constexpr double kPi = 3.14159265358979323846;
constexpr int kModelRate = 44100;
constexpr int kNFft = 2048;
constexpr int kHop = 512;
constexpr int kDimF = 1024;                 // bins de frequência usados pela rede
constexpr int kDimT = 256;                  // quadros de tempo por bloco
constexpr int kChunk = kHop * (kDimT - 1);  // 130560 amostras
constexpr int kNumStems = 6;

inline const char* stemName (int i)
{
    static const char* names[kNumStems] = { "Kick", "Snare", "Toms", "Hi-Hat", "Ride", "Crash" };
    return names[i];
}

// ---------------------------------------------------------------------------------------------
// FFT complexa radix-2 (double) — pequena e sem dependências.
class Fft
{
public:
    explicit Fft (int size) : n (size), rev ((size_t) size), cosT ((size_t) size / 2), sinT ((size_t) size / 2)
    {
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * kPi * i / n;
            cosT[(size_t) i] = std::cos (a);
            sinT[(size_t) i] = std::sin (a);
        }
    }

    int size() const { return n; }

    void forward (double* re, double* im) const { run (re, im, false); }

    // inversa já normalizada por 1/N
    void inverse (double* re, double* im) const
    {
        run (re, im, true);
        const double s = 1.0 / n;
        for (int i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
    }

private:
    void run (double* re, double* im, bool inv) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const double wr = cosT[(size_t) (j * step)];
                    const double wi = inv ? -sinT[(size_t) (j * step)] : sinT[(size_t) (j * step)];
                    const int a = i + j, b = a + half;
                    const double tr = re[b] * wr - im[b] * wi;
                    const double ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
            }
        }
    }

    int n;
    std::vector<int> rev;
    std::vector<double> cosT, sinT;
};

// ---------------------------------------------------------------------------------------------
// STFT / ISTFT equivalentes a torch.stft / torch.istft (center=True, janela de Hann periódica).
// Layout do espectrograma de um bloco: [4][kDimF][kDimT] = {L_real, L_imag, R_real, R_imag}.
class Stft
{
public:
    Stft() : fft (kNFft), window ((size_t) kNFft)
    {
        for (int i = 0; i < kNFft; ++i)
            window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / kNFft);
    }

    // x: kChunk amostras de um canal. ch: 0 = esquerdo, 1 = direito.
    void forwardChannel (const float* x, int n, int ch, float* spec) const
    {
        const int T = 1 + n / kHop;
        std::vector<double> re ((size_t) kNFft), im ((size_t) kNFft);

        for (int t = 0; t < T; ++t)
        {
            for (int k = 0; k < kNFft; ++k)
            {
                long idx = (long) t * kHop - kNFft / 2 + k;
                if (idx < 0) idx = -idx;                        // reflexão nas bordas
                if (idx >= n) idx = 2L * (n - 1) - idx;
                re[(size_t) k] = (double) x[idx] * window[(size_t) k];
                im[(size_t) k] = 0.0;
            }
            fft.forward (re.data(), im.data());

            float* outRe = spec + (size_t) (ch * 2) * kDimF * T;
            float* outIm = spec + (size_t) (ch * 2 + 1) * kDimF * T;
            for (int f = 0; f < kDimF; ++f)
            {
                outRe[(size_t) f * T + t] = (float) re[(size_t) f];
                outIm[(size_t) f * T + t] = (float) im[(size_t) f];
            }
        }
    }

    // spec: [4][kDimF][T] de um stem. Escreve (T-1)*kHop amostras em outL / outR.
    void inverseStereo (const float* spec, int T, float* outL, float* outR) const
    {
        for (int ch = 0; ch < 2; ++ch)
            inverseChannel (spec + (size_t) (ch * 2) * kDimF * T,
                            spec + (size_t) (ch * 2 + 1) * kDimF * T, T, ch == 0 ? outL : outR);
    }

private:
    void inverseChannel (const float* specRe, const float* specIm, int T, float* out) const
    {
        const int len = kNFft + kHop * (T - 1);
        std::vector<double> buf ((size_t) len, 0.0), env ((size_t) len, 0.0);
        std::vector<double> re ((size_t) kNFft), im ((size_t) kNFft);

        for (int t = 0; t < T; ++t)
        {
            std::fill (re.begin(), re.end(), 0.0);
            std::fill (im.begin(), im.end(), 0.0);

            for (int f = 0; f < kDimF; ++f)
            {
                const double r = specRe[(size_t) f * T + t];
                const double i = (f == 0) ? 0.0 : (double) specIm[(size_t) f * T + t];
                re[(size_t) f] = r;
                im[(size_t) f] = i;
                if (f > 0)
                {
                    re[(size_t) (kNFft - f)] = r;               // simetria hermitiana
                    im[(size_t) (kNFft - f)] = -i;
                }
            }
            // bin de Nyquist (kDimF) = 0 (a rede só enxerga os primeiros kDimF bins)

            fft.inverse (re.data(), im.data());

            const size_t off = (size_t) t * kHop;
            for (int k = 0; k < kNFft; ++k)
            {
                const double w = window[(size_t) k];
                buf[off + (size_t) k] += re[(size_t) k] * w;
                env[off + (size_t) k] += w * w;
            }
        }

        const int outLen = kHop * (T - 1);
        for (int i = 0; i < outLen; ++i)
        {
            const double e = env[(size_t) (i + kNFft / 2)];
            out[i] = (float) (e > 1e-11 ? buf[(size_t) (i + kNFft / 2)] / e : 0.0);
        }
    }

    Fft fft;
    std::vector<double> window;
};

// ---------------------------------------------------------------------------------------------
// Reamostrador sinc com janela Kaiser (qualidade de estúdio, ~90 dB de rejeição).
class Resampler
{
public:
    static std::vector<float> process (const float* in, size_t n, double inRate, double outRate)
    {
        if (std::abs (inRate - outRate) < 0.01)
            return std::vector<float> (in, in + n);

        const double scale = std::min (1.0, outRate / inRate);
        const int hw = (int) std::ceil (16.0 / scale);
        const int taps = 2 * hw;
        constexpr int phases = 512;
        constexpr double beta = 8.6;
        const double i0beta = besselI0 (beta);

        std::vector<float> table ((size_t) (phases + 1) * taps);
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = (double) p / phases;
            double sum = 0.0;
            std::vector<double> row ((size_t) taps);
            for (int j = 0; j < taps; ++j)
            {
                const double d = j - (hw - 1) - frac;
                const double u = d / hw;
                const double kaiser = std::abs (u) < 1.0 ? besselI0 (beta * std::sqrt (1.0 - u * u)) / i0beta : 0.0;
                const double x = kPi * scale * d;
                const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (x) / x;
                row[(size_t) j] = scale * sinc * kaiser;
                sum += row[(size_t) j];
            }
            for (int j = 0; j < taps; ++j)
                table[(size_t) p * taps + j] = (float) (row[(size_t) j] / sum);   // ganho DC = 1
        }

        const size_t nOut = (size_t) std::llround ((double) n * outRate / inRate);
        std::vector<float> out (nOut);
        const double step = inRate / outRate;
        std::vector<float> coef ((size_t) taps);

        for (size_t m = 0; m < nOut; ++m)
        {
            const double t = (double) m * step;
            const long base = (long) std::floor (t);
            const double ph = (t - (double) base) * phases;
            const int p0 = (int) ph;
            const float a = (float) (ph - p0);
            const float* r0 = &table[(size_t) p0 * taps];
            const float* r1 = r0 + taps;

            const long first = base - hw + 1;
            float acc = 0.0f;
            if (first >= 0 && first + taps <= (long) n)
            {
                const float* x = in + first;
                for (int j = 0; j < taps; ++j)
                    acc += x[j] * (r0[j] + a * (r1[j] - r0[j]));
            }
            else
            {
                for (int j = 0; j < taps; ++j)
                {
                    const long idx = first + j;
                    if (idx >= 0 && idx < (long) n)
                        acc += in[idx] * (r0[j] + a * (r1[j] - r0[j]));
                }
            }
            out[m] = acc;
        }
        return out;
    }

private:
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x / 4.0;
        for (int k = 1; k < 60; ++k)
        {
            term *= q / ((double) k * k);
            sum += term;
            if (term < 1e-14 * sum) break;
        }
        return sum;
    }
};

// ---------------------------------------------------------------------------------------------
// Resultado: stems[stem][canal] em 44,1 kHz.
using StemSet = std::vector<std::array<std::vector<float>, 2>>;
using ProgressFn = std::function<bool (float)>;   // devolve false para cancelar

class DrumSeparator
{
public:
    bool load (const std::filesystem::path& modelPath, std::string& error, int threads = 0)
    {
        try
        {
            if (! std::filesystem::exists (modelPath)) { error = "Arquivo do modelo não encontrado."; return false; }

            if (threads <= 0)
            {
                const int hw = (int) std::thread::hardware_concurrency();
                threads = std::max (1, std::min (8, hw - 1));
            }

            env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "bateriasep");
            Ort::SessionOptions so;
            so.SetIntraOpNumThreads (threads);
            so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
            session = std::make_unique<Ort::Session> (*env, modelPath.c_str(), so);
            return true;
        }
        catch (const std::exception& e)
        {
            session.reset();
            error = e.what();
            return false;
        }
    }

    bool isLoaded() const { return session != nullptr; }

    // overlap: 4 = qualidade padrão (igual ao Python), 2 = ~2x mais rápido, qualidade um pouco menor.
    bool separate (const float* inL, const float* inR, size_t n, int overlap,
                   StemSet& result, ProgressFn progress, std::string& error)
    {
        if (! session) { error = "Modelo não carregado."; return false; }
        if (n < 64) { error = "Áudio muito curto."; return false; }
        overlap = std::max (1, std::min (8, overlap));

        try
        {
            const int step = kChunk / overlap;
            const int border = kChunk - step;
            const bool padded = (long) n > 2L * border;
            const size_t total = padded ? n + 2 * (size_t) border : n;

            std::array<std::vector<float>, 2> mix;
            const float* src[2] = { inL, inR };
            for (int c = 0; c < 2; ++c)
            {
                mix[(size_t) c].resize (total);
                if (padded)
                {
                    for (int i = 0; i < border; ++i)
                    {
                        mix[(size_t) c][(size_t) i] = src[c][border - i];
                        mix[(size_t) c][total - (size_t) border + (size_t) i] = src[c][n - 2 - (size_t) i];
                    }
                    std::copy (src[c], src[c] + n, mix[(size_t) c].begin() + border);
                }
                else
                {
                    std::copy (src[c], src[c] + n, mix[(size_t) c].begin());
                }
            }

            std::vector<std::array<std::vector<float>, 2>> acc ((size_t) kNumStems);
            for (auto& s : acc) { s[0].assign (total, 0.0f); s[1].assign (total, 0.0f); }
            std::vector<float> counter (total, 0.0f);

            const int fade = kChunk / 10;
            std::vector<float> window ((size_t) kChunk, 1.0f);
            for (int i = 0; i < fade; ++i)
            {
                const float v = (float) i / (float) (fade - 1);
                window[(size_t) i] = v;
                window[(size_t) (kChunk - fade + i)] = 1.0f - v;
            }

            const size_t numChunks = (total + (size_t) step - 1) / (size_t) step;
            const size_t specSize = (size_t) 4 * kDimF * kDimT;
            std::vector<float> spec (specSize);
            std::vector<float> partL ((size_t) kChunk), partR ((size_t) kChunk);
            std::vector<float> wavL ((size_t) kChunk), wavR ((size_t) kChunk);
            std::vector<float> weights ((size_t) kChunk);

            Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
            const int64_t inShape[4] = { 1, 4, kDimF, kDimT };
            const char* inNames[] = { "spec" };
            const char* outNames[] = { "stems" };

            size_t chunkIndex = 0;
            for (size_t position = 0; position < total; position += (size_t) step, ++chunkIndex)
            {
                const size_t length = std::min ((size_t) kChunk, total - position);

                for (int c = 0; c < 2; ++c)
                {
                    float* part = c == 0 ? partL.data() : partR.data();
                    const float* m = mix[(size_t) c].data() + position;
                    std::copy (m, m + length, part);
                    if (length < (size_t) kChunk)
                    {
                        if (length > (size_t) kChunk / 2)
                            for (size_t j = 0; j < (size_t) kChunk - length; ++j) part[length + j] = m[length - 2 - j];  // reflexão
                        else
                            std::fill (part + length, part + kChunk, 0.0f);
                    }
                }

                stft.forwardChannel (partL.data(), kChunk, 0, spec.data());
                stft.forwardChannel (partR.data(), kChunk, 1, spec.data());

                Ort::Value input = Ort::Value::CreateTensor<float> (mem, spec.data(), specSize, inShape, 4);
                auto outputs = session->Run (Ort::RunOptions { nullptr }, inNames, &input, 1, outNames, 1);
                const float* stems = outputs[0].GetTensorData<float>();

                weights = window;
                const bool isFirst = (position == 0);
                const bool isLast = (position + (size_t) step >= total);
                if (isFirst)
                    std::fill (weights.begin(), weights.begin() + fade, 1.0f);
                else if (isLast)
                    std::fill (weights.end() - fade, weights.end(), 1.0f);

                for (int s = 0; s < kNumStems; ++s)
                {
                    stft.inverseStereo (stems + (size_t) s * specSize, kDimT, wavL.data(), wavR.data());
                    float* aL = acc[(size_t) s][0].data() + position;
                    float* aR = acc[(size_t) s][1].data() + position;
                    for (size_t j = 0; j < length; ++j)
                    {
                        aL[j] += wavL[j] * weights[j];
                        aR[j] += wavR[j] * weights[j];
                    }
                }
                for (size_t j = 0; j < length; ++j)
                    counter[position + j] += weights[j];

                if (progress && ! progress ((float) (chunkIndex + 1) / (float) numChunks))
                {
                    error = "Cancelado.";
                    return false;
                }
            }

            result.assign ((size_t) kNumStems, {});
            const size_t offset = padded ? (size_t) border : 0;
            for (int s = 0; s < kNumStems; ++s)
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto& o = result[(size_t) s][(size_t) c];
                    o.resize (n);
                    const float* a = acc[(size_t) s][(size_t) c].data() + offset;
                    for (size_t i = 0; i < n; ++i)
                    {
                        const float cnt = counter[offset + i];
                        o[i] = cnt > 1e-8f ? a[i] / cnt : 0.0f;
                    }
                }
            }
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

private:
    Stft stft;
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
};

} // namespace bateriasep
