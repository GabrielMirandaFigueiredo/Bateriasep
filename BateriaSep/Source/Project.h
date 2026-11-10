#pragma once

// Dados do "projeto" carregado no plugin (arquivo original + stems) e utilitários de áudio/arquivo.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <vector>

#include "BpmDetector.h"
#include "DrumSeparator.h"

namespace bateriasep
{

// Texto com acentos: "..."_u8 interpreta o literal como UTF-8 (sem isso o JUCE mostra 'RÃ¡pida').
inline juce::String operator""_u8 (const char* text, std::size_t length)
{
    return juce::String::fromUTF8 (text, (int) length);
}

struct StereoAudio
{
    double sampleRate = 44100.0;
    std::array<std::vector<float>, 2> ch;

    int64_t size() const { return (int64_t) ch[0].size(); }
    double seconds() const { return sampleRate > 0 ? (double) size() / sampleRate : 0.0; }
};

using AudioPtr = std::shared_ptr<const StereoAudio>;

inline AudioPtr resampled (const AudioPtr& a, double newRate)
{
    if (std::abs (a->sampleRate - newRate) < 0.01) return a;
    auto r = std::make_shared<StereoAudio>();
    r->sampleRate = newRate;
    for (int c = 0; c < 2; ++c)
        r->ch[(size_t) c] = Resampler::process (a->ch[(size_t) c].data(), a->ch[(size_t) c].size(), a->sampleRate, newRate);
    return r;
}

// pares (mínimo, máximo) por coluna, para desenhar a forma de onda
inline std::vector<float> makePeaks (const StereoAudio& a, int buckets)
{
    std::vector<float> p ((size_t) buckets * 2, 0.0f);
    const int64_t n = a.size();
    if (n == 0) return p;
    for (int b = 0; b < buckets; ++b)
    {
        const int64_t s = n * b / buckets;
        const int64_t e = std::max (s + 1, n * (b + 1) / buckets);
        float mn = 0.0f, mx = 0.0f;
        for (int64_t i = s; i < e && i < n; ++i)
        {
            const float v = 0.5f * (a.ch[0][(size_t) i] + a.ch[1][(size_t) i]);
            mn = std::min (mn, v);
            mx = std::max (mx, v);
        }
        p[(size_t) b * 2] = mn;
        p[(size_t) b * 2 + 1] = mx;
    }
    return p;
}

struct SourceData
{
    juce::String name;
    juce::File file;
    int fileChannels = 2;
    AudioPtr original;       // na taxa do arquivo
    AudioPtr playOriginal;   // na taxa do host (pode ser o mesmo objeto)
    double playRate = 44100.0;
    std::vector<float> peaks;
};

struct StemData
{
    std::vector<AudioPtr> stems;       // kNumStems, na taxa do arquivo
    std::vector<AudioPtr> playStems;   // na taxa do host
    std::vector<std::vector<float>> peaks;
    BpmResult bpm;
    juce::File folder;                 // pasta com os WAVs dos stems
};

inline juce::String stemFileName (const juce::String& name, int i)
{
    return name + "_" + stemName (i) + ".wav";
}

inline juce::String safeName (const juce::String& s)
{
    auto r = juce::File::createLegalFileName (s.trim());
    return r.isEmpty() ? juce::String ("Bateria") : r;
}

inline std::filesystem::path toFsPath (const juce::File& f)
{
   #if JUCE_WINDOWS
    return std::filesystem::path (f.getFullPathName().toWideCharPointer());
   #else
    return std::filesystem::path (f.getFullPathName().toStdString());
   #endif
}

// Lê qualquer arquivo de áudio suportado como estéreo (mono vira estéreo duplicado).
inline AudioPtr readAudio (const juce::File& f, juce::AudioFormatManager& fm, int& fileChannels, double maxSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return nullptr;
    if ((double) r->lengthInSamples > r->sampleRate * maxSeconds) return nullptr;

    fileChannels = (int) r->numChannels;
    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> buf (2, n);
    buf.clear();
    r->read (&buf, 0, n, 0, true, true);
    if (r->numChannels == 1)
        buf.copyFrom (1, 0, buf, 0, 0, n);

    auto a = std::make_shared<StereoAudio>();
    a->sampleRate = r->sampleRate;
    for (int c = 0; c < 2; ++c)
        a->ch[(size_t) c].assign (buf.getReadPointer (c), buf.getReadPointer (c) + n);
    return a;
}

// bits = 24 ou 32 (32 = ponto flutuante, sem perda nem corte). gain é aplicado a todas as amostras.
inline bool writeStemWav (const juce::File& f, const StereoAudio& a, int bits, float gain)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;

    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (a.sampleRate)
                                                                         .withNumChannels (2)
                                                                         .withBitsPerSample (bits));
    if (w == nullptr) return false;   // (em caso de sucesso o writer passa a ser o dono do stream)

    const int64_t n = a.size();
    constexpr int block = 65536;
    juce::AudioBuffer<float> tmp (2, block);
    for (int64_t pos = 0; pos < n; pos += block)
    {
        const int m = (int) std::min<int64_t> (block, n - pos);
        for (int c = 0; c < 2; ++c)
        {
            float* d = tmp.getWritePointer (c);
            const float* s = a.ch[(size_t) c].data() + pos;
            for (int i = 0; i < m; ++i) d[i] = s[i] * gain;
        }
        if (! w->writeFromAudioSampleBuffer (tmp, 0, m)) return false;
    }
    return true;
}

} // namespace bateriasep
