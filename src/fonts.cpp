#include "fonts.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "theme.h"
#include "config.h"
#include "logger.h"

#include "stb_image_write.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "imgui/imstb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION

namespace Fonts {

namespace {

struct LineMetrics {
  float width = 0.0f;
  float min_x = 0.0f;
  float max_x = 0.0f;
};

LineMetrics measure_line(const stbtt_fontinfo& font_info, const std::string& text, float scale) {
  LineMetrics metrics;
  metrics.min_x = std::numeric_limits<float>::max();
  metrics.max_x = std::numeric_limits<float>::lowest();

  float pen_x = 0.0f;
  int previous_codepoint = 0;

  for (unsigned char ch : text) {
    const int codepoint = static_cast<int>(ch);

    if (previous_codepoint != 0) {
      pen_x += static_cast<float>(stbtt_GetCodepointKernAdvance(&font_info, previous_codepoint, codepoint)) * scale;
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&font_info, codepoint, scale, scale, &x0, &y0, &x1, &y1);

    const float glyph_min_x = pen_x + static_cast<float>(x0);
    const float glyph_max_x = pen_x + static_cast<float>(x1);
    metrics.min_x = std::min(metrics.min_x, glyph_min_x);
    metrics.max_x = std::max(metrics.max_x, glyph_max_x);

    int advance = 0;
    int unused_lsb = 0;
    stbtt_GetCodepointHMetrics(&font_info, codepoint, &advance, &unused_lsb);
    pen_x += static_cast<float>(advance) * scale;
    previous_codepoint = codepoint;
  }

  if (metrics.min_x == std::numeric_limits<float>::max()) {
    metrics.min_x = 0.0f;
    metrics.max_x = 0.0f;
  }

  metrics.width = metrics.max_x - metrics.min_x;
  return metrics;
}

}  // namespace

constexpr int FONT_THUMBNAIL_SIZE = Config::MODEL_THUMBNAIL_SIZE;

void generate_font_thumbnail(const std::filesystem::path& font_path,
                             const std::filesystem::path& thumbnail_path) {
  const std::string font_path_str = font_path.u8string();

  std::string extension = font_path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (extension != ".ttf" && extension != ".otf") {
    LOG_DEBUG("[FONT] Skipping thumbnail generation for unsupported font type: {}", font_path_str);
    return;
  }

  std::ifstream file(font_path, std::ios::binary | std::ios::ate);
  if (!file) {
    throw std::runtime_error("Failed to open font file: " + font_path_str);
  }

  const std::streamsize size = file.tellg();
  if (size <= 0) {
    throw std::runtime_error("Font file is empty: " + font_path_str);
  }
  file.seekg(0, std::ios::beg);

  std::vector<unsigned char> font_buffer(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(font_buffer.data()), size)) {
    throw std::runtime_error("Failed to read font file: " + font_path_str);
  }

  stbtt_fontinfo font_info;
  const int offset = stbtt_GetFontOffsetForIndex(font_buffer.data(), 0);
  if (!stbtt_InitFont(&font_info, font_buffer.data(), offset)) {
    LOG_WARN("[FONT] stb_truetype failed to initialize font '{}'; skipping thumbnail", font_path_str);
    return;
  }

  const int thumb_w = FONT_THUMBNAIL_SIZE;
  const int thumb_h = FONT_THUMBNAIL_SIZE;
  if (thumb_w <= 0 || thumb_h <= 0) {
    LOG_WARN("[FONT] Invalid thumbnail dimensions: {}x{}", thumb_w, thumb_h);
    return;
  }

  constexpr std::array<const char*, 3> SAMPLE_LINES = {
    "ABCDEFGHIJ",
    "abcdefghij",
    "0123456789"
  };

  const float desired_pixel_height = static_cast<float>(thumb_h) * 0.6f;

  float scale = stbtt_ScaleForPixelHeight(&font_info, desired_pixel_height);
  if (scale <= 0.0f) {
    LOG_WARN("[FONT] Invalid scale computed for font '{}'; skipping thumbnail", font_path_str);
    return;
  }

  std::vector<LineMetrics> line_metrics;
  line_metrics.reserve(SAMPLE_LINES.size());

  auto compute_metrics = [&](float test_scale) {
    line_metrics.clear();
    float max_width = 0.0f;
    for (const char* line : SAMPLE_LINES) {
      LineMetrics metrics = measure_line(font_info, line, test_scale);
      line_metrics.push_back(metrics);
      max_width = std::max(max_width, metrics.width);
    }
    return max_width;
  };

  float max_width = compute_metrics(scale);
  const float max_allowed_width = static_cast<float>(thumb_w) * 0.9f;
  if (max_width > max_allowed_width && max_width > 0.0f) {
    const float adjustment = max_allowed_width / max_width;
    scale *= adjustment;
    max_width = compute_metrics(scale);
  }

  if (line_metrics.empty()) {
    max_width = compute_metrics(scale);
  }

  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
  const float scaled_ascent = static_cast<float>(ascent) * scale;
  const float scaled_descent = static_cast<float>(descent) * scale;
  const float line_height = scaled_ascent - scaled_descent;
  const float scaled_line_gap = static_cast<float>(line_gap) * scale;
  const float total_text_height =
    (line_height * static_cast<float>(SAMPLE_LINES.size())) +
    (scaled_line_gap * static_cast<float>(SAMPLE_LINES.size() - 1));
  const float first_baseline = std::roundf((static_cast<float>(thumb_h) - total_text_height) * 0.5f + scaled_ascent);

  std::vector<unsigned char> pixels(static_cast<size_t>(thumb_w * thumb_h * 4), 0);

  const unsigned char bg_r = 236;
  const unsigned char bg_g = 240;
  const unsigned char bg_b = 245;
  const unsigned char text_r = 40;
  const unsigned char text_g = 44;
  const unsigned char text_b = 52;

  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i + 0] = bg_r;
    pixels[i + 1] = bg_g;
    pixels[i + 2] = bg_b;
    pixels[i + 3] = 255;
  }

  for (size_t line_index = 0; line_index < SAMPLE_LINES.size(); ++line_index) {
    const std::string line_text = SAMPLE_LINES[line_index];
    const LineMetrics& metrics = line_metrics[line_index];

    const float clamped_width = std::clamp(metrics.width, 0.0f, static_cast<float>(thumb_w));
    const float left_margin = (static_cast<float>(thumb_w) - clamped_width) * 0.5f;
    const float origin_x = left_margin - metrics.min_x;
    const float baseline = first_baseline + static_cast<float>(line_index) * (line_height + scaled_line_gap);

    float pen_x = 0.0f;
    int previous_codepoint = 0;

    for (unsigned char ch : line_text) {
      const int codepoint = static_cast<int>(ch);

      if (previous_codepoint != 0) {
        pen_x += static_cast<float>(stbtt_GetCodepointKernAdvance(&font_info, previous_codepoint, codepoint)) * scale;
      }

      float glyph_x = origin_x + pen_x;
      const float shift_x = glyph_x - std::floor(glyph_x);

      int glyph_w = 0;
      int glyph_h = 0;
      int xoff = 0;
      int yoff = 0;
      unsigned char* glyph_bitmap = stbtt_GetCodepointBitmapSubpixel(
        &font_info, scale, scale, shift_x, 0.0f, codepoint, &glyph_w, &glyph_h, &xoff, &yoff);
      if (!glyph_bitmap) {
        int advance = 0;
        int unused_lsb = 0;
        stbtt_GetCodepointHMetrics(&font_info, codepoint, &advance, &unused_lsb);
        pen_x += static_cast<float>(advance) * scale;
        previous_codepoint = codepoint;
        continue;
      }

      const int pixel_x = static_cast<int>(std::floor(glyph_x)) + xoff;
      const int pixel_y = static_cast<int>(std::floor(baseline)) + yoff;

      for (int row = 0; row < glyph_h; ++row) {
        const int dest_y = pixel_y + row;
        if (dest_y < 0 || dest_y >= thumb_h) {
          continue;
        }

        for (int col = 0; col < glyph_w; ++col) {
          const int dest_x = pixel_x + col;
          if (dest_x < 0 || dest_x >= thumb_w) {
            continue;
          }

          const unsigned char alpha = glyph_bitmap[row * glyph_w + col];
          if (alpha <= 128) {
            continue;
          }

          const size_t index = static_cast<size_t>((dest_y * thumb_w + dest_x) * 4);
          pixels[index + 0] = text_r;
          pixels[index + 1] = text_g;
          pixels[index + 2] = text_b;
          pixels[index + 3] = 255;
        }
      }

      stbtt_FreeBitmap(glyph_bitmap, nullptr);

      int advance = 0;
      int unused_lsb = 0;
      stbtt_GetCodepointHMetrics(&font_info, codepoint, &advance, &unused_lsb);
      pen_x += static_cast<float>(advance) * scale;
      previous_codepoint = codepoint;
    }
  }

  std::error_code ec;
  std::filesystem::create_directories(thumbnail_path.parent_path(), ec);
  if (ec) {
    throw std::runtime_error("Failed to create thumbnail directory: " +
                             thumbnail_path.parent_path().generic_u8string() + ": " + ec.message());
  }

  const std::string out_path = thumbnail_path.u8string();
  if (!stbi_write_png(out_path.c_str(), thumb_w, thumb_h, 4, pixels.data(), thumb_w * 4)) {
    throw std::runtime_error("Failed to write font thumbnail: " + out_path);
  }

  LOG_TRACE("[FONT] Generated thumbnail for '{}' at {}", font_path_str, out_path);
}

bool load_fonts(ImGuiIO& io, float scale) {
  ImFontConfig font_config;
  font_config.FontDataOwnedByAtlas = false;  // Embedded data is owned by the binary
  font_config.PixelSnapH = true;
  font_config.OversampleH = 1;
  font_config.OversampleV = 1;

  // Use default glyph ranges which include Extended Latin for Unicode characters like × (U+00D7)
  const ImWchar* glyph_ranges = io.Fonts->GetGlyphRangesDefault();

  auto primary_asset = embedded_assets::get(Theme::PRIMARY_FONT_PATH);
  if (!primary_asset.has_value()) {
    LOG_ERROR("Embedded font asset not found: {}", Theme::PRIMARY_FONT_PATH);
    return false;
  }

  float scaled_primary_size = Theme::PRIMARY_FONT_SIZE * scale;
  float scaled_primary_large_size = Theme::PRIMARY_FONT_SIZE_LARGE * scale;
  float scaled_tag_size = Theme::TAG_FONT_SIZE * scale;

  Theme::g_primary_font = io.Fonts->AddFontFromMemoryTTF(
    const_cast<unsigned char*>(primary_asset->data),
    static_cast<int>(primary_asset->size),
    scaled_primary_size,
    &font_config,
    glyph_ranges);

  if (!Theme::g_primary_font) {
    LOG_ERROR("Failed to load primary font from embedded asset: {}", Theme::PRIMARY_FONT_PATH);
    return false;
  }

  ImFontConfig large_config = font_config;
  Theme::g_primary_font_large = io.Fonts->AddFontFromMemoryTTF(
    const_cast<unsigned char*>(primary_asset->data),
    static_cast<int>(primary_asset->size),
    scaled_primary_large_size,
    &large_config,
    glyph_ranges);

  if (!Theme::g_primary_font_large) {
    Theme::g_primary_font_large = Theme::g_primary_font;
    LOG_WARN("Failed to load enlarged primary font. Falling back to default size.");
  }

  auto tag_asset = embedded_assets::get(Theme::TAG_FONT_PATH);
  if (!tag_asset.has_value()) {
    LOG_ERROR("Embedded tag font asset not found: {}", Theme::TAG_FONT_PATH);
  } else {
    ImFontConfig tag_config = font_config;

    Theme::g_tag_font = io.Fonts->AddFontFromMemoryTTF(
      const_cast<unsigned char*>(tag_asset->data),
      static_cast<int>(tag_asset->size),
      scaled_tag_size,
      &tag_config,
      glyph_ranges);

    if (!Theme::g_tag_font) {
      LOG_ERROR("Failed to load tag font from embedded asset: {}", Theme::TAG_FONT_PATH);
    }
  }

  if (!Theme::g_tag_font) {
    Theme::g_tag_font = Theme::g_primary_font;
    LOG_WARN("Tag font unavailable. Falling back to primary font for pills.");
  }

  LOG_INFO("Fonts loaded successfully (scale={:.2f}, primary={}, primary_large={}, tag={})",
    scale, static_cast<void*>(Theme::g_primary_font), static_cast<void*>(Theme::g_primary_font_large), static_cast<void*>(Theme::g_tag_font));
  return Theme::g_primary_font != nullptr;
}

}  // namespace Fonts
