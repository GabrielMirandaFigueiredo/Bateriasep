#!/usr/bin/env bash
# Instala o BateriaSep so para o seu usuario (nao precisa de sudo).
set -euo pipefail
AQUI="$(cd "$(dirname "$0")" && pwd)"
VST3="$HOME/.vst3"
PROG="$HOME/.local/share/BateriaSep"
DADOS="$HOME/.config/BateriaSep"      # o plugin procura o modelo aqui
echo "Instalando BateriaSep..."
mkdir -p "$VST3" "$PROG" "$DADOS" "$HOME/.local/share/applications"
rm -rf "$VST3/BateriaSep.vst3"
cp -a "$AQUI/BateriaSep.vst3" "$VST3/BateriaSep.vst3"
cp -a "$AQUI/standalone/." "$PROG/"
cp -f "$AQUI/drumsep_core.onnx" "$DADOS/drumsep_core.onnx"
cat > "$HOME/.local/share/applications/bateriasep.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=BateriaSep
Comment=Separador de bateria com IA
Exec=$PROG/BateriaSep
Terminal=false
Categories=AudioVideo;Audio;
DESK
echo
echo "Pronto!"
echo " - Plugin VST3: $VST3/BateriaSep.vst3  (reescaneie os plugins na sua DAW)"
echo " - Programa: procure 'BateriaSep' no menu de aplicativos"
