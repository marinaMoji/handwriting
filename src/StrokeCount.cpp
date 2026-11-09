#include "StrokeCount.h"

#include <QList>

#include <algorithm>
#include <iterator>

#include "StrokeCountTable.inc"

namespace stroke_count {

int expected(uint codePoint) {
  const unsigned int* begin = std::begin(kStrokeCountCodePoints);
  const unsigned int* end = std::end(kStrokeCountCodePoints);
  const unsigned int* it = std::lower_bound(begin, end, static_cast<unsigned int>(codePoint));
  if (it == end || *it != codePoint) {
    return 0;
  }
  return kStrokeCounts[it - begin];
}

int expected(const QString& character) {
  const QList<uint> cps = character.toUcs4();
  return cps.isEmpty() ? 0 : expected(cps.first());
}

}  // namespace stroke_count
