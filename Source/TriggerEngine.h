#pragma once

// Motor de triggers: recebe as notas (tempo + velocity) de cada peça e toca samples no lugar certo.
// Sem dependência do JUCE, para poder ser testado isoladamente.
//
//  - Cada peça tem samples organizados em "camadas" de intensidade (suave -> forte). A velocity escolhe a camada
//    (com mistura suave entre camadas vizinhas) e ainda ajusta o volume, para respeitar a dinâmica do baterista.
//  - Cada camada pode ter variações (round-robin); a escolha depende só do número do golpe, então tocar, procurar
//    (seek) ou exportar dá exatamente o mesmo resultado.
//  - Hi-hat fechado corta o aberto (como numa bateria de verdade).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "MidiExtractor.h"

namespace bateriasep
{

using TrigSamplePtr = std::shared_ptr<const std::vector<float>>;

struct TrigVoice
{
    std::vector<std::vector<TrigSamplePtr>> layers;   // layers[camada][variação], camadas do mais suave ao mais forte

    bool empty() const { return layers.empty(); }
    size_t maxLength() const
    {
        size_t m = 0;
        for (auto& l : layers) for (auto& s : l) if (s) m = std::max (m, s->size());
        return m;
    }
};

struct TrigPiece
{
    std::map<int, TrigVoice> byNote;   // nota MIDI -> samples; a chave -1 vale para qualquer nota

    const TrigVoice* pick (int note) const
    {
        auto it = byNote.find (note);
        if (it == byNote.end()) it = byNote.find (-1);
        if (it == byNote.end() && ! byNote.empty()) it = byNote.begin();
        return it != byNote.end() && ! it->second.empty() ? &it->second : nullptr;
    }
    size_t maxLength() const
    {
        size_t m = 0;
        for (auto& kv : byNote) m = std::max (m, kv.second.maxLength());
        return m;
    }
};

struct TrigKit
{
    std::array<TrigPiece, 6> piece;
};

// Tudo que o motor precisa, já na taxa de amostragem em que vai tocar. Imutável depois de pronto.
struct TrigPlan
{
    uint64_t id = 0;                      // muda a cada plano novo (o motor recomeça quando o id muda)
    double rate = 44100.0;
    std::shared_ptr<const TrigKit> kit;
    std::array<std::vector<MidiHit>, 6> hits;
    std::array<std::vector<int64_t>, 6> hitSample;   // posição de cada golpe, em amostras
    std::array<int64_t, 6> maxTail {};               // maior duração de sample de cada peça

    void prepare()
    {
        for (size_t p = 0; p < 6; ++p)
        {
            hitSample[p].resize (hits[p].size());
            for (size_t i = 0; i < hits[p].size(); ++i) hitSample[p][i] = (int64_t) std::llround (hits[p][i].time * rate);
            maxTail[p] = kit ? (int64_t) kit->piece[p].maxLength() : 0;
        }
    }
};

class TrigEngine
{
public:
    static constexpr int kMaxVoices = 32;

    void reset() { for (auto& s : st) { s.nv = 0; s.expect = -2; s.nextHit = 0; } }

    // Uma amostra da peça p na posição pos (em amostras). Chame com pos consecutivo; um salto (seek, loop) é
    // tratado sozinho, recomeçando e retomando os golpes que ainda estariam soando.
    float tick (const TrigPlan& plan, int p, int64_t pos)
    {
        auto& s = st[(size_t) p];
        if (pos != s.expect) reseek (plan, p, pos);
        const auto& hs = plan.hitSample[(size_t) p];
        while (s.nextHit < hs.size() && hs[s.nextHit] <= pos)
        {
            startHit (plan, p, s.nextHit, pos - hs[s.nextHit]);
            ++s.nextHit;
        }
        s.expect = pos + 1;

        float out = 0.0f;
        for (int i = 0; i < s.nv;)
        {
            Voice& v = s.v[i];
            float g = v.gain;
            if (v.chokeLeft > 0)
            {
                g *= (float) v.chokeLeft / (float) v.chokeLen;
                if (--v.chokeLeft == 0) v.pos = v.d->size();
            }
            if (v.pos < v.d->size()) out += (*v.d)[v.pos++] * g;
            if (v.pos >= v.d->size()) s.v[i] = s.v[--s.nv];   // acabou: tira da lista
            else ++i;
        }
        return out;
    }

    // Renderiza a peça inteira (para exportar). n = número de amostras.
    static std::vector<float> render (const TrigPlan& plan, int p, size_t n)
    {
        TrigEngine e;
        e.reset();
        std::vector<float> out (n, 0.0f);
        for (size_t i = 0; i < n; ++i) out[i] = e.tick (plan, p, (int64_t) i);
        return out;
    }

private:
    struct Voice
    {
        const std::vector<float>* d = nullptr;   // vive no TrigPlan, que só é trocado entre blocos de áudio (depois de reset())
        size_t pos = 0;
        float gain = 1.0f;
        int note = 0;
        int chokeLeft = 0, chokeLen = 1;
    };
    struct State
    {
        Voice v[kMaxVoices];
        int nv = 0;
        size_t nextHit = 0;
        int64_t expect = -2;
    };
    std::array<State, 6> st;

    void reseek (const TrigPlan& plan, int p, int64_t pos)
    {
        auto& s = st[(size_t) p];
        s.nv = 0;
        const auto& hs = plan.hitSample[(size_t) p];
        const int64_t from = pos - plan.maxTail[(size_t) p] - 1;
        s.nextHit = (size_t) (std::lower_bound (hs.begin(), hs.end(), from) - hs.begin());
    }

    void addVoice (State& s, const TrigSamplePtr& smp, size_t offset, float gain, int note)
    {
        if (! smp || offset >= smp->size() || gain <= 0.0f) return;
        if (s.nv >= kMaxVoices) s.v[0] = s.v[--s.nv];   // cheio: descarta a mais antiga
        Voice& v = s.v[s.nv++];
        v.d = smp.get(); v.pos = offset; v.gain = gain; v.note = note; v.chokeLeft = 0;
    }

    void startHit (const TrigPlan& plan, int p, size_t hitIndex, int64_t offset)
    {
        auto& s = st[(size_t) p];
        const MidiHit& h = plan.hits[(size_t) p][hitIndex];
        if (! plan.kit) return;
        const TrigVoice* tv = plan.kit->piece[(size_t) p].pick (h.note);
        if (tv == nullptr) return;

        if (p == 3 && h.note == gm::hatClosed)       // hi-hat fechado corta o aberto (~5 ms)
            for (int i = 0; i < s.nv; ++i)
                if (s.v[i].note == gm::hatOpen && s.v[i].chokeLeft == 0)
                {
                    s.v[i].chokeLen = std::max (1, (int) (plan.rate * 0.005));
                    s.v[i].chokeLeft = s.v[i].chokeLen;
                }

        const int L = (int) tv->layers.size();
        const double vel = (double) std::min (127, std::max (1, h.velocity));
        auto sample = [&] (int layer) -> TrigSamplePtr
        {
            const auto& rr = tv->layers[(size_t) layer];
            return rr.empty() ? nullptr : rr[hitIndex % rr.size()];
        };

        if (L == 1)
        {
            addVoice (s, sample (0), (size_t) offset, (float) (vel / 127.0), h.note);   // uma camada só: o volume segue a velocity
            return;
        }

        // posição entre os centros das camadas: 0 = centro da camada mais suave, L-1 = da mais forte
        const double x = vel * (double) L / 127.0 - 0.5;
        if (x <= 0.0)
        {
            const double c0 = 0.5 * 127.0 / (double) L;
            addVoice (s, sample (0), (size_t) offset, (float) (vel / c0), h.note);        // mais suave que a camada mais suave
        }
        else if (x >= (double) (L - 1))
        {
            const double cl = ((double) (L - 1) + 0.5) * 127.0 / (double) L;
            addVoice (s, sample (L - 1), (size_t) offset, (float) std::min (1.3, vel / cl), h.note);
        }
        else
        {
            const int lo = (int) std::floor (x);
            const float f = (float) (x - (double) lo);
            addVoice (s, sample (lo), (size_t) offset, 1.0f - f, h.note);
            addVoice (s, sample (lo + 1), (size_t) offset, f, h.note);
        }
    }
};

} // namespace bateriasep
