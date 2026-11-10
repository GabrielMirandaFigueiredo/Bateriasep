# BateriaSep (VST3)

Plugin que separa uma gravação de bateria (inclusive de **um microfone só**) em 6 peças com IA:
**Kick, Snare, Toms, Hi-Hat, Ride e Crash**. Usa o modelo DrumSep (MDX23C) rodando dentro do plugin,
sem internet e sem placa de vídeo.

## O que o plugin faz

- **Carregar bateria...** (ou arraste o arquivo para a janela): WAV, AIFF, FLAC, OGG ou MP3, até 8 minutos.
  Já dá para ouvir o original enquanto a separação roda em segundo plano.
- **Player embutido:** Play, Pause, Stop. Clique na onda para ir a um ponto; **arraste para selecionar um trecho**
  (ele toca só a seleção, com *Repetir seleção* ligado ou desligado). Duplo clique limpa a seleção.
- **BPM provável** da gravação, com as alternativas de metade/dobro, botões **÷2 / ×2** e grade de batidas
  desenhada na onda. (Calculado com bumbo, caixa e tons. Gravação sem click pode variar.)
- **Faders com Mute e Solo** para cada peça, medidores, master, e **Ouvir original** (A/B).
- **Exportar todos os stems...** (WAV 32 bits float sem alterar, ou 24 bits com ganho de segurança igual para todos).
- **Arrastar p/ DAW** em cada peça e **Arrastar todos os stems para a DAW**: os arquivos são gravados
  automaticamente em `Documentos\BateriaSep\<nome do arquivo>\`, prontos para arrastar.
- Ao reabrir o projeto da DAW, o plugin recarrega os stems da pasta (sem separar de novo) e restaura faders/mute/solo.
- **Qualidade:** *Padrão* (mais fiel ao modelo) ou *Rápida* (cerca de 2x mais veloz, um pouco menos de qualidade).


## Usando

- Coloque o plugin numa faixa (ele ignora a entrada e toca o áudio carregado). **A DAW precisa estar com o motor de
  áudio rodando** para o player do plugin tocar; no Reaper, habilite *Options → Run FX when stopped*.
- Tempo de separação: depende da CPU. Em um servidor de 2 núcleos, 34 s de áudio levaram ~2 min na qualidade
  *Rápida* e ~4 min na *Padrão*. CPUs com mais núcleos são bem mais rápidas.
- Se a janela avisar "Não consegui carregar bateriasep_onnxruntime.dll (erro 126)", instale o
  *Microsoft Visual C++ Redistributable 2015-2022 (x64)*.


## Licenças e avisos

- **Modelo DrumSep (aufr33 / jarredou):** os pesos não têm licença publicada. Trate como **uso pessoal / não comercial**.
  Não distribua o plugin com o modelo dentro nem venda sem confirmar com os autores.
- Código do modelo (`tools/mdx23c`): MIT (veja `tools/mdx23c/LICENSE-mdxnet-infer`).
- ONNX Runtime: MIT. JUCE: AGPLv3 ou licença comercial (uso pessoal sem problema; para distribuir/vender, respeite a licença).

## Instaladores (Windows, Mac e Linux)

O workflow `.github/workflows/build.yml` (Actions > Instalador BateriaSep > Run workflow) gera e publica na aba **Releases**:

- Windows: `BateriaSep-Setup.exe`
- Mac (Intel e Apple Silicon): `BateriaSep-<versao>-mac.pkg`
- Linux: `BateriaSep-<versao>-linux-amd64.deb` e `BateriaSep-<versao>-linux.tar.gz` (rode `./install.sh`)
