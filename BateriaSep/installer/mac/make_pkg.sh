#!/usr/bin/env bash
# Gera BateriaSep-<versao>-mac.pkg (instalador de um clique).
# uso: make_pkg.sh <versao> <BateriaSep.vst3> <BateriaSep.app> <drumsep_core.onnx> <pasta de saida>
set -euo pipefail
VERSAO="$1"; VST="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; APP="$(cd "$(dirname "$3")" && pwd)/$(basename "$3")"
MODELO="$(cd "$(dirname "$4")" && pwd)/$(basename "$4")"; mkdir -p "$5"; OUT="$(cd "$5" && pwd)"
AQUI="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
ROOT="$TMP/root"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Applications" "$ROOT/Library/Application Support/BateriaSep"
ditto "$VST" "$ROOT/Library/Audio/Plug-Ins/VST3/BateriaSep.vst3"
ditto "$APP" "$ROOT/Applications/BateriaSep.app"
cp "$MODELO" "$ROOT/Library/Application Support/BateriaSep/drumsep_core.onnx"

# nao deixa o instalador "mover" os pacotes para outro lugar se ja existir uma copia
pkgbuild --analyze --root "$ROOT" "$TMP/components.plist" >/dev/null
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$TMP/components.plist" >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$TMP/components.plist"
  i=$((i+1))
done
pkgbuild --root "$ROOT" --component-plist "$TMP/components.plist" --identifier com.gabriel.bateriasep \
         --version "$VERSAO" --install-location / --ownership recommended "$TMP/BateriaSep-component.pkg"

sed "s/@VERSAO@/$VERSAO/g" "$AQUI/distribution.xml" > "$TMP/distribution.xml"
productbuild --distribution "$TMP/distribution.xml" --resources "$AQUI/resources" \
             --package-path "$TMP" "$OUT/BateriaSep-$VERSAO-mac.pkg"
ls -la "$OUT"
