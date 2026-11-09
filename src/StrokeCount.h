#pragma once

#include <QString>

// Expected stroke count of a character (Unihan kTotalStrokes), for the characters the Zinnia
// models can output. Returns 0 when unknown (kana, ASCII, characters outside the models).
namespace stroke_count {

int expected(uint codePoint);
// First code point of the string (handles surrogate pairs); 0 for an empty string.
int expected(const QString& character);

}  // namespace stroke_count
