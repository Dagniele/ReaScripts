#pragma once

#include <cstddef>

namespace guitartabs {

struct ModelFile {
  const char* path;
  const unsigned char* data;
  std::size_t size;
};

extern const ModelFile kBasicPitchFiles[];
extern const int kBasicPitchFileCount;

}  // namespace guitartabs
