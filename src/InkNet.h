#pragma once

// Order-free neural recognizer: C++ port of ml/render.py (9-channel splat render) and the
// InkNet ResNet (ml/model.py), reading the flat weights written by ml/export.py.
// Std-only on purpose (no Qt), so it is easy to test against PyTorch fixtures.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ink {

constexpr int kGrid = 64;
constexpr int kChannels = 9;
constexpr int kCanvas = 300;  // strokes are in the 300x300 canvas the recognizer fits to

using Point = std::pair<double, double>;
using StrokeD = std::vector<Point>;

// Spec shared with the Python renderer (see ml/render.py): returns kChannels*size*size bytes.
std::vector<uint8_t> render(const std::vector<StrokeD>& strokes, int size = kGrid, double width = 1.8);

class InkNet {
 public:
  bool load(const std::string& path, std::string* error = nullptr);
  bool loadFromMemory(const uint8_t* data, size_t size, std::string* error = nullptr);
  bool loaded() const { return !classes_.empty(); }

  int numClasses() const { return static_cast<int>(classes_.size()); }
  const std::string& className(int i) const { return classes_[i]; }  // UTF-8

  // Raw logits for a rendered image (kChannels*kGrid*kGrid bytes).
  std::vector<float> logits(const uint8_t* image) const;
  // Log-softmax of the above.
  std::vector<float> logProbs(const uint8_t* image) const;

  // Best `n` classes by log-probability, best first.
  std::vector<std::pair<int, float>> top(const uint8_t* image, int n) const;

 private:
  struct Conv {
    int cin = 0, cout = 0, k = 0, stride = 1;
    std::vector<float> w, b;
  };
  struct Block {
    Conv c1, c2, skip;
    bool hasSkip = false;
  };
  Conv stem_;
  std::vector<Block> blocks_;
  std::vector<float> fcW_, fcB_;
  int fcIn_ = 0;
  std::vector<std::string> classes_;
};

}  // namespace ink
