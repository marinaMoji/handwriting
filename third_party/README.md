# third_party

## zinnia-src

Vendored copy of [taku910/zinnia](https://github.com/taku910/zinnia) recognition engine sources (BSD-style license; see `zinnia-src/COPYING`).

Local patch for modern C++:

- `svm.cpp`: replaced removed `std::random_shuffle` with `std::shuffle` + `std::mt19937`.
