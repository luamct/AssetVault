#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace embedded_assets {

struct AssetView {
  const unsigned char* data = nullptr;
  std::size_t size = 0;

  bool empty() const {
    return data == nullptr || size == 0;
  }
};

struct EmbeddedAsset {
  std::string_view path;
  const unsigned char* data;
  std::size_t size;
};

const EmbeddedAsset* begin();
const EmbeddedAsset* end();
std::size_t count();

const EmbeddedAsset* find_asset(std::string_view path);
bool contains(std::string_view path);
std::optional<AssetView> get(std::string_view path);

namespace detail {
extern const EmbeddedAsset kAssets[];
extern const std::size_t kAssetCount;
}  // namespace detail

}  // namespace embedded_assets
