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

## Como gerar os arquivos para Windows (sem instalar nada no seu PC)

1. Crie um repositório no GitHub e envie esta pasta inteira.
2. Aba **Actions** → *Compilar BateriaSep (Windows)* → **Run workflow**. Leva uns 10-15 minutos.
   (Ele converte o modelo para ONNX e compila o plugin.)
3. Quando terminar, baixe os artefatos:
   - **BateriaSep-VST3-Windows** (zip que contém a pasta `BateriaSep.vst3`)
   - **BateriaSep-modelo** (arquivo `drumsep_core.onnx`, ~437 MB)
   - opcional: **BateriaSep-Programa-Standalone-Windows** (`BateriaSep.exe`, funciona sem DAW)
4. Instale:
   - copie a pasta `BateriaSep.vst3` inteira para `C:\Program Files\Common Files\VST3`
   - copie `drumsep_core.onnx` para `%APPDATA%\BateriaSep\` (crie a pasta se não existir).
     Se preferir deixar em outro lugar, use o botão **Localizar modelo...** no plugin.
5. Reescaneie os plugins na DAW.

> Os artefatos do GitHub ficam 30 dias. Contas gratuitas têm 500 MB de espaço para artefatos em repositórios
> privados: o modelo sozinho usa ~437 MB, então apague execuções antigas ou baixe e apague.

## Usando

- Coloque o plugin numa faixa (ele ignora a entrada e toca o áudio carregado). **A DAW precisa estar com o motor de
  áudio rodando** para o player do plugin tocar; no Reaper, habilite *Options → Run FX when stopped*.
- Tempo de separação: depende da CPU. Em um servidor de 2 núcleos, 34 s de áudio levaram ~2 min na qualidade
  *Rápida* e ~4 min na *Padrão*. CPUs com mais núcleos são bem mais rápidas.
- Se a janela avisar "Não consegui carregar bateriasep_onnxruntime.dll (erro 126)", instale o
  *Microsoft Visual C++ Redistributable 2015-2022 (x64)*.

## Testes

- `tests/test_separator.cpp`: STFT/ISTFT/reamostrador contra o PyTorch e separação completa contra o Python (120 dB de precisão).
- `tests/test_bpm.cpp`: detector de BPM em sinais sintéticos e nas peças reais.
- `tests/test_plugin.cpp`: teste de integração do plugin (carregar, separar, tocar, mute/solo, seleção, exportar, reabrir).
  Compile com `-DBATERIASEP_BUILD_TESTS=ON`.

## Licenças e avisos

- **Modelo DrumSep (aufr33 / jarredou):** os pesos não têm licença publicada. Trate como **uso pessoal / não comercial**.
  Não distribua o plugin com o modelo dentro nem venda sem confirmar com os autores.
- Código do modelo (`tools/mdx23c`): MIT (veja `tools/mdx23c/LICENSE-mdxnet-infer`).
- ONNX Runtime: MIT. JUCE: AGPLv3 ou licença comercial (uso pessoal sem problema; para distribuir/vender, respeite a licença).
