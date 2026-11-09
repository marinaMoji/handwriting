#include "InkNet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ink {

namespace {
constexpr double kSpacing = 0.5;
constexpr double kPi = 3.14159265358979323846;

StrokeD resample(const StrokeD& s, double spacing) {
  if (s.size() < 2) return s;
  std::vector<double> arc(s.size(), 0.0);
  for (size_t i = 1; i < s.size(); ++i)
    arc[i] = arc[i - 1] + std::hypot(s[i].first - s[i - 1].first, s[i].second - s[i - 1].second);
  const double total = arc.back();
  if (total < 1e-6) return StrokeD{s[0]};
  const int m = std::max(2, static_cast<int>(std::ceil(total / spacing)) + 1);
  StrokeD out;
  out.reserve(m);
  size_t seg = 0;
  for (int j = 0; j < m; ++j) {
    const double t = total * j / (m - 1);
    while (seg + 2 < s.size() && arc[seg + 1] < t) ++seg;
    const double span = arc[seg + 1] - arc[seg];
    const double f = span > 0 ? (t - arc[seg]) / span : 0.0;
    out.emplace_back(s[seg].first + f * (s[seg + 1].first - s[seg].first),
                     s[seg].second + f * (s[seg + 1].second - s[seg].second));
  }
  return out;
}
}  // namespace

std::vector<uint8_t> render(const std::vector<StrokeD>& strokes, int size, double width) {
  const double k = static_cast<double>(size) / kCanvas;
  const double R = width / 2.0 + 0.5;
  const double g = kSpacing / R;
  const size_t plane = static_cast<size_t>(size) * size;
  std::vector<double> acc(kChannels * plane, 0.0);

  for (const StrokeD& raw : strokes) {
    if (raw.empty()) continue;
    StrokeD scaled;
    scaled.reserve(raw.size());
    for (const Point& p : raw) scaled.emplace_back(p.first * k, p.second * k);
    const StrokeD p = resample(scaled, kSpacing);
    const bool hasDir = p.size() > 1;
    for (size_t i = 0; i < p.size(); ++i) {
      int b0 = 0;
      double f = 0.0;
      if (hasDir) {
        const size_t a = (i + 1 < p.size()) ? i : i - 1;
        const double dx = p[a + 1].first - p[a].first, dy = p[a + 1].second - p[a].second;
        double theta = std::atan2(dy, dx);
        if (theta < 0) theta += 2 * kPi;
        const double t = theta / (kPi / 4);
        const double fl = std::floor(t);
        f = t - fl;
        b0 = static_cast<int>(fl) % 8;
      }
      const double x = p[i].first, y = p[i].second;
      const int ix0 = static_cast<int>(std::floor(x)), iy0 = static_cast<int>(std::floor(y));
      for (int oy = -2; oy <= 2; ++oy) {
        const int iy = iy0 + oy;
        if (iy < 0 || iy >= size) continue;
        for (int ox = -2; ox <= 2; ++ox) {
          const int ix = ix0 + ox;
          if (ix < 0 || ix >= size) continue;
          const double d = std::hypot(ix + 0.5 - x, iy + 0.5 - y);
          const double w = std::max(0.0, 1.0 - d / R) * g;
          if (w <= 0) continue;
          const size_t pix = static_cast<size_t>(iy) * size + ix;
          acc[pix] += w;
          if (hasDir) {
            acc[(1 + b0) * plane + pix] += w * (1 - f);
            acc[(1 + (b0 + 1) % 8) * plane + pix] += w * f;
          }
        }
      }
    }
  }
  std::vector<uint8_t> out(acc.size());
  for (size_t i = 0; i < acc.size(); ++i)
    out[i] = static_cast<uint8_t>(std::nearbyint(std::min(acc[i], 1.0) * 255.0));
  return out;
}

// --- Network ------------------------------------------------------------------------------

namespace {
struct Reader {
  const uint8_t* p;
  const uint8_t* end;
  bool ok = true;
  bool take(void* dst, size_t n) {
    if (!ok || static_cast<size_t>(end - p) < n) return ok = false;
    std::memcpy(dst, p, n);
    p += n;
    return true;
  }
  uint32_t u32() {
    uint32_t v = 0;
    take(&v, 4);  // little endian hosts only (x86, arm64): all supported targets
    return v;
  }
  std::vector<float> floats(size_t n) {
    std::vector<float> v;
    if (!ok || static_cast<size_t>(end - p) < n * 4) {
      ok = false;
      return v;
    }
    v.resize(n);
    std::memcpy(v.data(), p, n * 4);
    p += n * 4;
    return v;
  }
};

// out[cout][oh][ow] = bias + sum conv(in[cin][ih][iw]); 'same' padding for odd k.
void conv(const std::vector<float>& in, int cin, int ih, int iw, int cout, int k, int stride,
          const std::vector<float>& w, const std::vector<float>& b, std::vector<float>* out, int* oh,
          int* ow) {
  const int pad = k / 2;
  *oh = (ih + 2 * pad - k) / stride + 1;
  *ow = (iw + 2 * pad - k) / stride + 1;
  out->assign(static_cast<size_t>(cout) * *oh * *ow, 0.0f);
  for (int oc = 0; oc < cout; ++oc) {
    float* o = out->data() + static_cast<size_t>(oc) * *oh * *ow;
    for (int i = 0; i < *oh * *ow; ++i) o[i] = b[oc];
    for (int ic = 0; ic < cin; ++ic) {
      const float* src = in.data() + static_cast<size_t>(ic) * ih * iw;
      const float* wk = w.data() + (static_cast<size_t>(oc) * cin + ic) * k * k;
      for (int ky = 0; ky < k; ++ky) {
        for (int kx = 0; kx < k; ++kx) {
          const float wv = wk[ky * k + kx];
          if (wv == 0.0f) continue;
          // valid ox range so that 0 <= ox*stride + kx - pad < iw
          const int oxLo = std::max(0, (pad - kx + stride - 1) / stride);
          const int oxHi = std::min(*ow - 1, (iw - 1 + pad - kx) / stride);
          for (int oy = 0; oy < *oh; ++oy) {
            const int iy = oy * stride + ky - pad;
            if (iy < 0 || iy >= ih) continue;
            const float* row = src + static_cast<size_t>(iy) * iw;
            float* orow = o + static_cast<size_t>(oy) * *ow;
            for (int ox = oxLo; ox <= oxHi; ++ox) orow[ox] += wv * row[ox * stride + kx - pad];
          }
        }
      }
    }
  }
}

void relu(std::vector<float>* v) {
  for (float& x : *v)
    if (x < 0) x = 0;
}
}  // namespace

bool InkNet::loadFromMemory(const uint8_t* data, size_t size, std::string* error) {
  auto fail = [&](const char* msg) {
    if (error) *error = msg;
    classes_.clear();
    return false;
  };
  Reader r{data, data + size};
  char magic[4] = {};
  r.take(magic, 4);
  if (!r.ok || std::memcmp(magic, "INKN", 4) != 0) return fail("not an InkNet file");
  if (r.u32() != 1) return fail("unsupported InkNet version");
  const int inCh = static_cast<int>(r.u32());
  const uint32_t nWidths = r.u32();
  if (!r.ok || inCh != kChannels || nWidths == 0 || nWidths > 8) return fail("bad header");
  std::vector<int> widths;
  for (uint32_t i = 0; i < nWidths; ++i) widths.push_back(static_cast<int>(r.u32()));
  const uint32_t nClasses = r.u32();
  if (!r.ok || nClasses == 0 || nClasses > 1000000) return fail("bad class count");
  classes_.clear();
  for (uint32_t i = 0; i < nClasses && r.ok; ++i) {
    uint8_t len = 0;
    r.take(&len, 1);
    std::string s(len, '\0');
    r.take(s.data(), len);
    classes_.push_back(std::move(s));
  }
  auto readConv = [&](int cin, int cout, int k, int stride) {
    Conv c;
    c.cin = cin;
    c.cout = cout;
    c.k = k;
    c.stride = stride;
    c.w = r.floats(static_cast<size_t>(cout) * cin * k * k);
    c.b = r.floats(cout);
    return c;
  };
  stem_ = readConv(inCh, widths[0], 3, 1);
  blocks_.clear();
  int cin = widths[0];
  for (int w : widths) {
    for (int j = 0; j < 2; ++j) {
      Block b;
      const int stride = j == 0 ? 2 : 1;
      b.c1 = readConv(cin, w, 3, stride);
      b.c2 = readConv(w, w, 3, 1);
      if (stride != 1 || cin != w) {
        b.hasSkip = true;
        b.skip = readConv(cin, w, 1, stride);
      }
      blocks_.push_back(std::move(b));
      cin = w;
    }
  }
  fcIn_ = cin;
  fcW_ = r.floats(static_cast<size_t>(nClasses) * cin);
  fcB_ = r.floats(nClasses);
  if (!r.ok) return fail("truncated InkNet file");
  return true;
}

bool InkNet::load(const std::string& path, std::string* error) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    if (error) *error = "cannot open " + path;
    return false;
  }
  std::vector<uint8_t> buf;
  uint8_t chunk[1 << 16];
  size_t n;
  while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) buf.insert(buf.end(), chunk, chunk + n);
  std::fclose(f);
  return loadFromMemory(buf.data(), buf.size(), error);
}

std::vector<float> InkNet::logits(const uint8_t* image) const {
  std::vector<float> x(static_cast<size_t>(kChannels) * kGrid * kGrid);
  for (size_t i = 0; i < x.size(); ++i) x[i] = image[i] / 255.0f;
  int h = kGrid, w = kGrid;
  std::vector<float> y;
  conv(x, stem_.cin, h, w, stem_.cout, stem_.k, stem_.stride, stem_.w, stem_.b, &y, &h, &w);
  relu(&y);
  int c = stem_.cout;
  std::vector<float> t, s;
  for (const Block& b : blocks_) {
    int h1, w1, h2, w2;
    conv(y, c, h, w, b.c1.cout, 3, b.c1.stride, b.c1.w, b.c1.b, &t, &h1, &w1);
    relu(&t);
    std::vector<float> u;
    conv(t, b.c1.cout, h1, w1, b.c2.cout, 3, 1, b.c2.w, b.c2.b, &u, &h2, &w2);
    if (b.hasSkip) {
      int hs, ws;
      conv(y, c, h, w, b.skip.cout, 1, b.skip.stride, b.skip.w, b.skip.b, &s, &hs, &ws);
      for (size_t i = 0; i < u.size(); ++i) u[i] += s[i];
    } else {
      for (size_t i = 0; i < u.size(); ++i) u[i] += y[i];
    }
    relu(&u);
    y = std::move(u);
    c = b.c2.cout;
    h = h2;
    w = w2;
  }
  std::vector<float> pooled(c, 0.0f);
  for (int ch = 0; ch < c; ++ch) {
    double sum = 0;
    for (int i = 0; i < h * w; ++i) sum += y[static_cast<size_t>(ch) * h * w + i];
    pooled[ch] = static_cast<float>(sum / (h * w));
  }
  std::vector<float> out(classes_.size());
  for (size_t k = 0; k < out.size(); ++k) {
    const float* wr = fcW_.data() + k * fcIn_;
    float acc = fcB_[k];
    for (int i = 0; i < fcIn_; ++i) acc += wr[i] * pooled[i];
    out[k] = acc;
  }
  return out;
}

std::vector<float> InkNet::logProbs(const uint8_t* image) const {
  std::vector<float> l = logits(image);
  const float mx = *std::max_element(l.begin(), l.end());
  double sum = 0;
  for (float v : l) sum += std::exp(v - mx);
  const float lse = mx + static_cast<float>(std::log(sum));
  for (float& v : l) v -= lse;
  return l;
}

std::vector<std::pair<int, float>> InkNet::top(const uint8_t* image, int n) const {
  const std::vector<float> lp = logProbs(image);
  std::vector<int> idx(lp.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<int>(i);
  n = std::min<int>(n, static_cast<int>(idx.size()));
  std::partial_sort(idx.begin(), idx.begin() + n, idx.end(), [&](int a, int b) { return lp[a] > lp[b]; });
  std::vector<std::pair<int, float>> out;
  for (int i = 0; i < n; ++i) out.emplace_back(idx[i], lp[idx[i]]);
  return out;
}

}  // namespace ink
