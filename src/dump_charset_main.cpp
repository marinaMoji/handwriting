// dump_charset: print every character a Zinnia model can output, one per line (UTF-8).
//   dump_charset MODEL... > charset.txt
// Used by scripts/gen_stroke_counts.py to keep the stroke-count table to characters we can
// actually recognize.
#include "zinnia.h"

#include <cstdio>

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    zinnia::Recognizer* r = zinnia::Recognizer::create();
    if (!r->open(argv[i])) {
      std::fprintf(stderr, "cannot open %s\n", argv[i]);
      return 1;
    }
    for (size_t k = 0; k < r->size(); ++k) {
      std::printf("%s\n", r->value(k));
    }
    delete r;
  }
  return 0;
}
