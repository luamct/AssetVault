#!/usr/bin/env python3
"""Crop or downscale a rasterized pixel-art image by inferring the pixel grid.

The script:
1) Finds the content bounding box (alpha-aware, or by background color).
2) Expands that box to a square.
3) Snaps the square to a pixel grid inferred from the desired output size.
4) Samples the center of each grid cell, optionally merging near colors to
   smooth minor rasterization noise.

Default behavior is `crop`: write out only the detected content bounding box.
The existing downscale pipeline is kept around for later iterations.
"""

from __future__ import annotations

import argparse
import math
import pathlib
from typing import Iterable, List, Sequence, Tuple

try:
  from PIL import Image, ImageDraw
except ImportError as exc:  # pragma: no cover - depends on environment
  raise SystemExit(
      "Pillow is required to run this script. Install it with `pip install Pillow`."
  ) from exc

Color = Tuple[int, int, int, int]
BBox = Tuple[int, int, int, int]
BACKGROUND_THRESHOLD = 24
RUN_LENGTH_THRESHOLD = 4


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description=(
          "Crop or downscale a pixel-art image. Default is `crop`, which writes the "
          "detected content region to an output file."
      )
  )
  parser.add_argument(
      "--mode",
      "-m",
      choices=("crop", "downscale"),
      default="crop",
      help="Operation mode (default: crop).",
  )
  parser.add_argument(
      "--input",
      "-i",
      type=pathlib.Path,
      required=True,
      help="Path to the source image.",
  )
  parser.add_argument(
      "--output",
      "-o",
      type=pathlib.Path,
      default=None,
      help="Destination path for the output image (default: derived from input).",
  )
  parser.add_argument(
      "--size",
      "--output-size",
      "--target-size",
      "-s",
      dest="size",
      type=int,
      default=16,
      help="Width/height of the output image in pixels (default: 16)",
  )
  parser.add_argument(
      "--pixel-size",
      "-p",
      type=int,
      default=None,
      help="Size (in source pixels) of one output pixel cell (required for downscale).",
  )
  parser.add_argument(
      "--tolerance",
      "-t",
      type=float,
      default=None,
      help=(
          "RGB distance tolerance in [0,1] for merging similar colors "
          "(omit to keep original sampled colors)"
      ),
  )
  parser.add_argument(
      "--debug-grid",
      "-d",
      action="store_true",
      help="If set, writes a debug image with the aligned grid overlay.",
  )
  parser.add_argument(
      "--bg-threshold",
      "-b",
      type=int,
      default=BACKGROUND_THRESHOLD,
      help=f"Background RGB threshold for content detection (default: {BACKGROUND_THRESHOLD}).",
  )
  parser.add_argument(
      "--run-threshold",
      "-r",
      type=int,
      default=RUN_LENGTH_THRESHOLD,
      help=(
          "RGB closeness threshold for grouping runs during pixel-size inference "
          f"(default: {RUN_LENGTH_THRESHOLD}; lower values split runs sooner)."
      ),
  )
  return parser.parse_args()


def detect_content_bbox(
    image: Image.Image, background_threshold: int = BACKGROUND_THRESHOLD
) -> BBox:
  """Detect the bounding box of visible content."""
  alpha = image.getchannel("A")
  alpha_min, alpha_max = alpha.getextrema()

  if alpha_max == 0:
    raise ValueError("Image contains no visible pixels.")

  if alpha_min < 255:
    bbox = alpha.getbbox()
    if bbox is None:
      raise ValueError("Image contains no visible pixels.")
    return bbox

  width, height = image.size
  bg = detect_background_color(image)

  min_x, min_y = width, height
  max_x, max_y = -1, -1

  for idx, pixel in enumerate(image.getdata()):
    if not _color_close(pixel, bg, threshold=background_threshold):
      x = idx % width
      y = idx // width
      if x < min_x:
        min_x = x
      if y < min_y:
        min_y = y
      if x > max_x:
        max_x = x
      if y > max_y:
        max_y = y

  if max_x == -1:
    raise ValueError("No content detected; image appears to be a solid color.")

  return (min_x, min_y, max_x + 1, max_y + 1)


def expand_to_square(bbox: BBox, image_size: Tuple[int, int]) -> BBox:
  """Expand a bounding box to the smallest square that fits within the image.

  The expansion is anchored at the top-left to avoid recentering the content.
  """
  left, top, right, bottom = bbox
  width = right - left
  height = bottom - top
  side = max(width, height)

  img_w, img_h = image_size

  new_left = left
  new_top = top

  if new_left + side > img_w:
    new_left = max(0, img_w - side)
  if new_top + side > img_h:
    new_top = max(0, img_h - side)

  return (new_left, new_top, new_left + side, new_top + side)


def _color_close(a: Color, b: Color, threshold: int = BACKGROUND_THRESHOLD) -> bool:
  """Cheap closeness check in RGB space."""
  return (
      abs(a[0] - b[0]) <= threshold and
      abs(a[1] - b[1]) <= threshold and
      abs(a[2] - b[2]) <= threshold
  )


def detect_background_color(image: Image.Image) -> Color:
  """Estimate the dominant background color from the border pixels."""
  width, height = image.size
  pixels = image.load()
  bg_candidates = []
  for x in range(width):
    bg_candidates.append(pixels[x, 0])
    bg_candidates.append(pixels[x, height - 1])
  for y in range(height):
    bg_candidates.append(pixels[0, y])
    bg_candidates.append(pixels[width - 1, y])

  opaque_candidates = [c for c in bg_candidates if len(c) >= 4 and c[3] >= 250]
  if opaque_candidates:
    bg_candidates = opaque_candidates

  if not bg_candidates:
    return (0, 0, 0, 0)

  rs = sorted(c[0] for c in bg_candidates)
  gs = sorted(c[1] for c in bg_candidates)
  bs = sorted(c[2] for c in bg_candidates)
  mid = len(bg_candidates) // 2
  if len(bg_candidates) % 2 == 0:
    r = (rs[mid - 1] + rs[mid]) // 2
    g = (gs[mid - 1] + gs[mid]) // 2
    b = (bs[mid - 1] + bs[mid]) // 2
  else:
    r = rs[mid]
    g = gs[mid]
    b = bs[mid]

  return (int(r), int(g), int(b), 255)


def make_background_transparent(
    image: Image.Image, background_color: Color, threshold: int
) -> Image.Image:
  if threshold < 0:
    raise ValueError("Background threshold must be non-negative.")

  output = image.copy()
  data = list(output.getdata())
  new_data: List[Color] = []
  for pixel in data:
    if pixel[3] == 0:
      new_data.append(pixel)
      continue
    if _color_close(pixel, background_color, threshold=threshold):
      new_data.append((pixel[0], pixel[1], pixel[2], 0))
    else:
      new_data.append(pixel)
  output.putdata(new_data)
  return output


def infer_pixel_size(
    image: Image.Image,
    bbox: BBox,
    target_size: int,
    background_color: Color | None = None,
    run_threshold: int = RUN_LENGTH_THRESHOLD,
    background_threshold: int = BACKGROUND_THRESHOLD,
) -> int:
  """Infer the pixel size by histogramming run-lengths across sampled scanlines."""
  left, top, right, bottom = bbox
  width = right - left
  height = bottom - top

  rows = list(range(top, bottom, max(1, height // 8)))[:16]
  cols = list(range(left, right, max(1, width // 8)))[:16]

  if not rows:
    rows = [top + height // 2]
  if not cols:
    cols = [left + width // 2]

  pixels = image.load()
  runs: List[int] = []
  expected_size = max(1, round(max(width, height) / float(target_size)))

  def collect_run_lengths(line: Sequence[Color]) -> None:
    if not line:
      return
    last = None
    length = 0
    last_is_bg = False

    for color in line:
      is_bg = (
          background_color is not None and
          _color_close(color, background_color, background_threshold)
      )

      if last is None:
        last = color
        last_is_bg = is_bg
        length = 0 if is_bg else 1
        continue

      if is_bg:
        if not last_is_bg and length > 0:
          runs.append(length)
        last = color
        last_is_bg = True
        length = 0
        continue

      if last_is_bg:
        last = color
        last_is_bg = False
        length = 1
        continue

      if _color_close(color, last, threshold=run_threshold):
        length += 1
      else:
        runs.append(length)
        last = color
        length = 1

    if length > 0 and not last_is_bg:
      runs.append(length)

  for y in rows:
    line = [pixels[x, y] for x in range(left, right)]
    collect_run_lengths(line)
  for x in cols:
    line = [pixels[x, y] for y in range(top, bottom)]
    collect_run_lengths(line)

  lower = max(1, expected_size // 4)
  upper = expected_size * 4
  filtered = [r for r in runs if lower <= r <= upper]
  if not filtered:
    return expected_size

  histogram = {}
  for r in filtered:
    histogram[r] = histogram.get(r, 0) + 1

  dominant_value, dominant_count = max(
      histogram.items(),
      key=lambda kv: (
          kv[1],
          -abs(kv[0] - expected_size),
          -kv[0],
      ),
  )

  inferred = dominant_value

  inferred = max(1, inferred)
  inferred = min(inferred, max(1, min(width, height)))
  return inferred


def align_sample_region(
    square: BBox, target_size: int, image_size: Tuple[int, int], pixel_size: int
) -> Tuple[BBox, int]:
  """Align a square region to the pixel grid using a fixed pixel size."""
  left, top, right, bottom = square
  side = right - left
  img_w, img_h = image_size

  if pixel_size <= 0:
    raise ValueError("Pixel size must be positive.")

  sample_size = pixel_size * target_size

  if sample_size > img_w or sample_size > img_h:
    raise ValueError(
        "Grid does not fit inside the source image; reduce pixel size or target size."
    )
  if left + sample_size > img_w or top + sample_size > img_h:
    raise ValueError(
        "Grid anchored at content top/left exceeds image bounds; adjust pixel size or target size."
    )

  aligned_left = left
  aligned_top = top

  return ((aligned_left, aligned_top, aligned_left + sample_size,
           aligned_top + sample_size), pixel_size)


def sample_centers(
    image: Image.Image, region: BBox, target_size: int, pixel_size: int
) -> List[Color]:
  pixels = image.load()
  left, top, _, _ = region
  samples: List[Color] = []

  for y_index in range(target_size):
    for x_index in range(target_size):
      x = int((x_index + 0.5) * pixel_size) + left
      y = int((y_index + 0.5) * pixel_size) + top
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


def write_debug_overlay(
    image: Image.Image, region: BBox, pixel_size: int, target_size: int, path: pathlib.Path
) -> None:
  """Overlay the sampling grid on top of the source image."""
  overlay = image.copy()
  draw = ImageDraw.Draw(overlay, "RGBA")

  left, top, right, bottom = region
  grid_color = (255, 0, 0, 160)
  border_color = (0, 255, 0, 200)

  for x in range(left, right + 1, pixel_size):
    draw.line([(x, top), (x, bottom - 1)], fill=grid_color, width=1)
  for y in range(top, bottom + 1, pixel_size):
    draw.line([(left, y), (right - 1, y)], fill=grid_color, width=1)

  draw.rectangle([left, top, right - 1, bottom - 1], outline=border_color, width=2)
  draw.text((left + 4, top + 4), f"{target_size}x{target_size}", fill=border_color)
  overlay.save(path)


def write_grid_overlay(image: Image.Image, pixel_size: int, path: pathlib.Path) -> None:
  """Draw a grid over the full image starting at the top-left."""
  if pixel_size <= 0:
    raise ValueError("Pixel size must be positive to draw a grid.")
  overlay = image.copy()
  draw = ImageDraw.Draw(overlay, "RGBA")
  grid_color = (255, 0, 0, 160)

  width, height = image.size
  for x in range(0, width + 1, pixel_size):
    draw.line([(x, 0), (x, height)], fill=grid_color, width=1)
  for y in range(0, height + 1, pixel_size):
    draw.line([(0, y), (width, y)], fill=grid_color, width=1)

  overlay.save(path)


def crop_to_content(
    input_path: pathlib.Path,
    output_path: pathlib.Path,
    background_threshold: int,
    target_size: int,
    debug_grid: bool,
    run_threshold: int,
) -> None:
  image = Image.open(input_path).convert("RGBA")
  background_color = detect_background_color(image)
  bbox = detect_content_bbox(image, background_threshold=background_threshold)
  cropped = image.crop(bbox)

  cropped = make_background_transparent(
      cropped, background_color=background_color, threshold=background_threshold
  )

  inferred_pixel_size = infer_pixel_size(
      cropped,
      (0, 0, cropped.width, cropped.height),
      target_size=target_size,
      background_color=background_color,
      run_threshold=run_threshold,
      background_threshold=background_threshold,
  )

  cropped.save(output_path)
  print(f"Input: {image.width}x{image.height}")
  print(f"Background color: {background_color}")
  print(f"Content bbox: {bbox}")
  print(f"Inferred pixel size: {inferred_pixel_size}")
  if debug_grid:
    grid_path = grid_overlay_path(output_path)
    write_grid_overlay(cropped, inferred_pixel_size, grid_path)
    print(f"Wrote grid overlay to {grid_path}")
  print(f"Wrote {output_path} ({cropped.width}x{cropped.height})")


def downscale(
    input_path: pathlib.Path,
    output_path: pathlib.Path,
    target_size: int,
    pixel_size: int,
    tolerance: float | None,
    debug_grid: bool,
    background_threshold: int = BACKGROUND_THRESHOLD,
) -> None:
  image = Image.open(input_path).convert("RGBA")

  alpha_min, alpha_max = image.getchannel("A").getextrema()
  background_color = None
  if alpha_min == alpha_max == 255:
    background_color = detect_background_color(image)

  bbox = detect_content_bbox(image, background_threshold=background_threshold)
  square = expand_to_square(bbox, image.size)
  sample_region, aligned_pixel_size = align_sample_region(
      square, target_size, image.size, pixel_size
  )

  centers = sample_centers(image, sample_region, target_size, aligned_pixel_size)
  if tolerance is None:
    output_pixels = centers
  else:
    output_pixels = merge_palette(centers, tolerance)

  if background_color is not None:
    output_pixels = [
        (p[0], p[1], p[2], 0)
        if p[3] != 0 and _color_close(p, background_color, threshold=background_threshold)
        else p
        for p in output_pixels
    ]

  write_output(output_path, target_size, output_pixels)
  if debug_grid:
    debug_path = output_path.with_name(f"{output_path.stem}_debug.png")
    write_debug_overlay(image, sample_region, aligned_pixel_size, target_size, debug_path)
    print(f"Wrote debug overlay to {debug_path}")
  print(f"Input: {image.width}x{image.height}")
  if background_color is not None:
    print(f"Background color: {background_color}")
  print(f"Content bbox: {bbox}")
  print(f"Square region: {square}")
  print(f"Using pixel size: {aligned_pixel_size}")
  print(f"Aligned region: {sample_region} (pixel size: {aligned_pixel_size})")


def default_output_path(input_path: pathlib.Path, mode: str) -> pathlib.Path:
  if mode == "crop":
    suffix = "_cropped"
  else:
    suffix = "_downscaled"
  return input_path.with_name(f"{input_path.stem}{suffix}{input_path.suffix}")


def grid_overlay_path(output_path: pathlib.Path) -> pathlib.Path:
  stem = output_path.stem
  for suffix in ("_grid_overlay", "_grid"):
    if stem.endswith(suffix):
      stem = stem[: -len(suffix)]
      break
  return output_path.with_name(f"{stem}_grid_overlay{output_path.suffix}")


def main() -> None:
  args = parse_args()
  output_path = args.output if args.output is not None else default_output_path(
      args.input, args.mode
  )

  if args.mode == "crop":
    crop_to_content(
        args.input,
        output_path,
        args.bg_threshold,
        target_size=args.size,
        debug_grid=args.debug_grid,
        run_threshold=args.run_threshold,
    )
    return

  if args.pixel_size is None:
    raise SystemExit("`--pixel-size` is required when --mode=downscale.")

  downscale(
      args.input,
      output_path,
      args.size,
      args.pixel_size,
      args.tolerance,
      args.debug_grid,
      background_threshold=args.bg_threshold,
  )
  print(f"Wrote {output_path} ({args.size}x{args.size})")


if __name__ == "__main__":
  main()
