#!/usr/bin/env python3
"""Monta o kit padrão dos triggers (assets/default_kit.zip) a partir do Big Rusty Drums (Karoryfer, CC0).

Uso:
  git clone --filter=blob:none --no-checkout https://github.com/sfzinstruments/karoryfer.big-rusty-drums.git brd
  python3 tools/make_kit.py brd assets/default_kit.zip

Cada peça fica com 4 camadas de intensidade (suave -> forte) e 2 variações (round-robin), em mono, 16 bits, 44,1 kHz,
recortadas logo antes do ataque e com fade no final (para o plugin ficar pequeno).
"""
import io, re, subprocess, sys, zipfile
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

# voz -> (pasta dentro de Samples/, duração máxima em segundos)
VOICES = {
    "kick":  ("kick_24/kick/kick", 0.9),
    "snare": ("snare_14/center/top", 0.7),
    "tomH":  ("tom_14/center/cl", 1.2),
    "tomM":  ("tom_18/center/cl", 1.4),
    "tomL":  ("tom_22/center/cl", 1.6),
    "hatC":  ("hihat_14/tc/cl", 0.35),
    "hatO":  ("hihat_14/open/cl", 1.6),
    "ride":  ("ride_22/rd/oh", 3.0),
    "crash": ("crash_17/cr/oh", 4.0),
}
LAYERS, RR, OUT_SR = 4, 2, 44100


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, text=True).stdout


def main(repo, out_zip):
    repo = Path(repo)
    listing = git(repo, "ls-tree", "-r", "--name-only", "HEAD").splitlines()
    chosen = {}                                    # voz -> {(camada, rr): caminho}
    for voice, (d, _) in VOICES.items():
        files = {}
        for p in listing:
            m = re.match(rf"Samples/{re.escape(d)}/.*_vl(\d+)_rr(\d+)\.flac$", p)
            if m:
                files[(int(m.group(1)), int(m.group(2)))] = p
        if not files:
            sys.exit(f"sem arquivos para {voice} em {d}")
        vls = sorted({k[0] for k in files})
        pick = sorted({vls[int(round(i * (len(vls) - 1) / (LAYERS - 1)))] for i in range(LAYERS)})
        chosen[voice] = {(li + 1, r): files[(vl, r)] for li, vl in enumerate(pick) for r in range(1, RR + 1) if (vl, r) in files}
        print(voice, "camadas originais", vls, "-> usando", pick)

    need = sorted({p for v in chosen.values() for p in v.values()})
    print("baixando", len(need), "arquivos...")
    subprocess.run(["git", "-C", str(repo), "checkout", "HEAD", "--", *need], check=True)

    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_STORED) as z:
        for voice, (d, maxlen) in VOICES.items():
            raw = {}
            for key, p in chosen[voice].items():
                x, sr = sf.read(repo / p, dtype="float32", always_2d=True)
                x = x.mean(axis=1)
                if sr != OUT_SR:
                    from math import gcd
                    g = gcd(sr, OUT_SR)
                    x = resample_poly(x, OUT_SR // g, sr // g).astype(np.float32)
                raw[key] = x
            peak = max(float(np.abs(x).max()) for x in raw.values())
            gain = 0.89 / peak                      # mesmo ganho em todas as camadas: preserva a dinâmica entre elas
            for (layer, rr), x in sorted(raw.items()):
                x = x * gain
                thr = 0.03 * float(np.abs(x).max())
                first = int(np.argmax(np.abs(x) > thr))
                x = x[max(0, first - int(0.0005 * OUT_SR)):]
                x = x[: int(maxlen * OUT_SR)].copy()
                nf = min(len(x), int(0.08 * OUT_SR))
                x[-nf:] *= np.linspace(1.0, 0.0, nf, dtype=np.float32)
                buf = io.BytesIO()
                sf.write(buf, x, OUT_SR, format="FLAC", subtype="PCM_16")
                z.writestr(f"{voice}_v{layer}_r{rr}.flac", buf.getvalue())
    print("ok:", out_zip, Path(out_zip).stat().st_size // 1024, "KB")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
