#!/usr/bin/env bash
# Gera BateriaSep-<versao>-linux-amd64.deb e BateriaSep-<versao>-linux.tar.gz
# uso: make_packages.sh <versao> <pasta BateriaSep.vst3> <pasta Standalone> <drumsep_core.onnx> <pasta de saida>
set -euo pipefail
VERSAO="$1"; VST="$(realpath "$2")"; STAND="$(realpath "$3")"; MODELO="$(realpath "$4")"; OUT="$(realpath -m "$5")"
AQUI="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$OUT"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# ---------------- .deb ----------------
D="$TMP/deb"
mkdir -p "$D/DEBIAN" "$D/usr/lib/vst3" "$D/opt/BateriaSep" "$D/usr/share/applications" "$D/usr/bin"
cp -a "$VST" "$D/usr/lib/vst3/BateriaSep.vst3"
cp -a "$STAND/BateriaSep" "$D/opt/BateriaSep/BateriaSep"
cp -a "$STAND"/libonnxruntime.so* "$D/opt/BateriaSep/"
cp "$MODELO" "$D/opt/BateriaSep/drumsep_core.onnx"
ln -s /opt/BateriaSep/BateriaSep "$D/usr/bin/bateriasep"
cat > "$D/usr/share/applications/bateriasep.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=BateriaSep
Comment=Separador de bateria com IA
Exec=/opt/BateriaSep/BateriaSep
Terminal=false
Categories=AudioVideo;Audio;
DESK
KB=$(du -sk "$D" | cut -f1)
cat > "$D/DEBIAN/control" <<CTRL
Package: bateriasep
Version: $VERSAO
Architecture: amd64
Maintainer: Gabriel Figueiredo <gabrielfigueiredo10@gmail.com>
Installed-Size: $KB
Depends: libasound2t64 | libasound2, libfreetype6, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1, libgl1
Section: sound
Priority: optional
Description: BateriaSep - separador de bateria com IA (VST3 e programa)
 Separa uma gravacao de bateria em kick, snare, toms, hi-hat, ride e crash.
CTRL
find "$D" -type d -exec chmod 755 {} +
dpkg-deb --root-owner-group -Zgzip -z1 --build "$D" "$OUT/BateriaSep-$VERSAO-linux-amd64.deb"

# ---------------- .tar.gz (qualquer distribuicao, sem sudo) ----------------
T="$TMP/BateriaSep-Linux"
mkdir -p "$T/standalone"
cp -a "$VST" "$T/BateriaSep.vst3"
cp -a "$STAND/BateriaSep" "$T/standalone/BateriaSep"
cp -a "$STAND"/libonnxruntime.so* "$T/standalone/"
cp "$MODELO" "$T/drumsep_core.onnx"
cp "$AQUI/install.sh" "$AQUI/LEIAME.txt" "$T/"
chmod +x "$T/install.sh"
tar -C "$TMP" -czf "$OUT/BateriaSep-$VERSAO-linux.tar.gz" BateriaSep-Linux
ls -la "$OUT"
