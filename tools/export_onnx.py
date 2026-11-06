"""Converte o checkpoint DrumSep (PyTorch) para ONNX.

Exporta apenas o NÚCLEO da rede (tudo entre a STFT e a STFT inversa). A STFT/ISTFT e a divisão em
blocos são feitas em C++ dentro do plugin.

  entrada : espectrograma  float32 [1, 4, 1024, 256]   (L_real, L_imag, R_real, R_imag)
  saída   : espectrogramas float32 [1, 6, 4, 1024, 256] (kick, snare, toms, hh, ride, crash)

Uso: python export_onnx.py drumsep.ckpt drumsep.yaml drumsep_core.onnx
"""
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mdx23c.checkpoint import load_checkpoint_state  # noqa: E402
from mdx23c.config import MDX23CConfig               # noqa: E402
from mdx23c.model import TFC_TDF_net                 # noqa: E402


class Core(torch.nn.Module):
    """Mesma conta de TFC_TDF_net.forward, sem a STFT no começo e sem a ISTFT no fim."""

    def __init__(self, net: TFC_TDF_net):
        super().__init__()
        self.net = net

    def forward(self, x):
        net = self.net
        mix = x = net.cac2cws(x)
        first_conv_out = x = net.first_conv(x)
        x = x.transpose(-1, -2)

        enc = []
        for block in net.encoder_blocks:
            x = block.tfc_tdf(x)
            enc.append(x)
            x = block.downscale(x)

        x = net.bottleneck_block(x)

        for block in net.decoder_blocks:
            x = block.upscale(x)
            x = torch.cat([x, enc.pop()], 1)
            x = block.tfc_tdf(x)

        x = x.transpose(-1, -2)
        x = x * first_conv_out
        x = net.final_conv(torch.cat([mix, x], 1))
        x = net.cws2cac(x)
        b, c, f, t = x.shape
        return x.reshape(b, net.num_target_instruments, -1, f, t)


def main(ckpt, yaml_path, out_path):
    cfg = MDX23CConfig.from_yaml(Path(yaml_path))
    net = TFC_TDF_net(cfg, torch.device("cpu"))
    net.load_state_dict(load_checkpoint_state(ckpt))
    net.eval()

    core = Core(net).eval()
    dummy = torch.randn(1, 4, cfg.audio.dim_f, cfg.audio.dim_t)
    with torch.inference_mode():
        torch.onnx.export(core, dummy, out_path, opset_version=17, dynamo=False,
                          input_names=["spec"], output_names=["stems"],
                          do_constant_folding=True)
    print("ONNX gravado em", out_path)


if __name__ == "__main__":
    main(*sys.argv[1:4])
