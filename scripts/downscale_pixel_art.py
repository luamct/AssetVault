#!/usr/bin/env python3
"""Downscale a rasterized 1024x1024 pixel-art image to 16x16.

The script samples the center pixel of each 64x64 block (computed from the
input size and desired output size) and then merges colors that are within a
given RGB tolerance to reduce tiny rasterization variations.
"""

from __future__ import annotations

import argparse
import math
import pathlib
from typing import Iterable, List, Sequence, Tuple

try:
  from PIL import Image
except ImportError as exc:  # pragma: no cover - depends on environment
  raise SystemExit(
      "Pillow is required to run this script. Install it with `pip install Pillow`."
  ) from exc

Color = Tuple[int, int, int, int]


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description=(
          "Downscale a pixel-art image by sampling the center of each block and "
          "merging similar colors."
      )
  )
  parser.add_argument(
      "--input",
      type=pathlib.Path,
      default=pathlib.Path("images/zoom-in-16.png"),
      help="Path to the source image (default: images/zoom-in-16.png)",
  )
  parser.add_argument(
      "--output",
      type=pathlib.Path,
      default=pathlib.Path("images/zoom-in-16-downscaled.png"),
      help="Destination path for the 16x16 output image",
  )
  parser.add_argument(
      "--target-size",
      type=int,
      default=16,
      help="Width/height of the output image (default: 16)",
  )
  parser.add_argument(
      "--tolerance",
      type=float,
      default=0.2,
      help=(
          "RGB distance tolerance in [0,1] for merging similar colors "
          "(default: 0.2)"
      ),
  )
  return parser.parse_args()


def sample_centers(image: Image.Image, target_size: int) -> List[Color]:
  """Sample a grid of centers even if the source size is not an exact multiple."""
  pixels = image.load()
  samples: List[Color] = []

  step_x = image.width / float(target_size)
  step_y = image.height / float(target_size)

  for y_index in range(target_size):
    for x_index in range(target_size):
      x = int((x_index + 0.5) * step_x)
      y = int((y_index + 0.5) * step_y)
      x = min(max(x, 0), image.width - 1)
      y = min(max(y, 0), image.height - 1)
      samples.append(pixels[x, y])

  return samples


def normalize_rgb(color: Color) -> Tuple[float, float, float]:
  r, g, b = color[:3]
  return (r / 255.0, g / 255.0, b / 255.0)


def color_distance(a: Sequence[float], b: Sequence[float]) -> float:
  return math.sqrt(
      (a[0] - b[0]) ** 2 +
      (a[1] - b[1]) ** 2 +
      (a[2] - b[2]) ** 2
  )


def merge_palette(colors: Iterable[Color], tolerance: float) -> List[Color]:
  merged: List[Color] = []
  palette: List[Tuple[Tuple[float, float, float], Color]] = []
  threshold = tolerance

  for color in colors:
    normalized = normalize_rgb(color)
    replacement = None
    for ref_norm, ref_color in palette:
      if color_distance(normalized, ref_norm) <= threshold:
        replacement = ref_color
        break

    if replacement is None:
      palette.append((normalized, color))
      replacement = color

    merged.append(replacement)

  return merged


def write_output(path: pathlib.Path, size: int, pixels: Sequence[Color]) -> None:
  output = Image.new("RGBA", (size, size))
  output.putdata(pixels)
  output.save(path)


def downscale(
    input_path: pathlib.Path, output_path: pathlib.Path, target_size: int, tolerance: float
) -> None:
  image = Image.open(input_path).convert("RGBA")
  centers = sample_centers(image, target_size)
  merged = merge_palette(centers, tolerance)
  write_output(output_path, target_size, merged)


def main() -> None:
  args = parse_args()
  downscale(args.input, args.output, args.target_size, args.tolerance)
  print(f"Wrote {args.output} ({args.target_size}x{args.target_size})")


if __name__ == "__main__":
  main()
