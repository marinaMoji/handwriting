#include "PracticeSession.h"

#include "SampleIO.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace practice {

namespace {

QByteArray readFile(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QStringList charsOf(const QJsonValue& v) {
  QStringList out;
  if (v.isArray()) {
    for (const QJsonValue& c : v.toArray()) out << c.toString();
  } else {
    // A plain string: one entry per code point, so non-BMP characters stay whole.
    for (uint cp : v.toString().toUcs4()) out << QString::fromUcs4(&cp, 1);
  }
  out.removeAll(QString());
  return out;
}

// Stable across runs and platforms (QHash and std::hash are not guaranteed to be).
quint64 fnv1a(const QString& s) {
  quint64 h = 1469598103934665603ULL;
  for (unsigned char b : s.toUtf8()) {
    h ^= b;
    h *= 1099511628211ULL;
  }
  return h;
}

// xorshift64*: a small deterministic generator for the shuffle.
struct Rng {
  quint64 s;
  explicit Rng(quint64 seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
  quint64 next() {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ULL;
  }
};

}  // namespace

QVector<PracticeSet> loadSets(const QString& docsDir, QString* error) {
  QVector<PracticeSet> out;
  const QByteArray sets = readFile(QDir(docsDir).filePath(QStringLiteral("practice-sets.json")));
  if (!sets.isEmpty()) {
    const QJsonObject all = QJsonDocument::fromJson(sets).object().value(QStringLiteral("sets")).toObject();
    for (auto it = all.begin(); it != all.end(); ++it) {
      const QJsonObject o = it.value().toObject();
      PracticeSet s;
      s.id = it.key();
      s.label = o.value(QStringLiteral("label")).toString(it.key());
      s.tag = o.value(QStringLiteral("tag")).toString(QStringLiteral("dev"));
      s.description = o.value(QStringLiteral("description")).toString();
      s.chars = charsOf(o.value(QStringLiteral("chars")));
      if (!s.chars.isEmpty()) out.push_back(s);
    }
  }
  const QByteArray lists = readFile(QDir(docsDir).filePath(QStringLiteral("sample-chars.json")));
  if (!lists.isEmpty()) {
    const QJsonObject cats = QJsonDocument::fromJson(lists).object();
    for (auto it = cats.begin(); it != cats.end(); ++it) {
      const QJsonObject o = it.value().toObject();
      PracticeSet s;
      s.id = QStringLiteral("list-") + it.key();
      s.label = o.value(QStringLiteral("label")).toString(it.key());
      s.tag = QStringLiteral("dev");
      s.description = QStringLiteral("Development character list (docs/sample-chars.json).");
      s.chars = charsOf(o.value(QStringLiteral("chars")));
      if (!s.chars.isEmpty()) out.push_back(s);
    }
  }
  if (out.isEmpty() && error) *error = QStringLiteral("no practice sets found in %1").arg(docsDir);
  return out;
}

QString sampleDirForTag(const QString& tag, const QString& defaultSamplesDir) {
  const QString forced = qEnvironmentVariable("HANDWRITING_SAMPLES_DIR");
  if (!forced.isEmpty()) return forced;
  const QFileInfo base(defaultSamplesDir);
  const QString parent = base.dir().absolutePath();
  if (tag == QLatin1String("test")) return QDir(parent).filePath(QStringLiteral("samples-test"));
  if (tag == QLatin1String("textbook")) return QDir(parent).filePath(QStringLiteral("samples-textbook"));
  return defaultSamplesDir;
}

QStringList charsWrittenBy(const QString& dir, const QString& writer) {
  QSet<QString> seen;
  if (writer.isEmpty()) return {};
  for (const QString& path : sample_io::findSamples(dir)) {
    Sample s;
    if (sample_io::loadSample(path, &s, nullptr) && s.writer == writer) seen.insert(s.character);
  }
  return QStringList(seen.begin(), seen.end());
}

PracticeSession::PracticeSession(const PracticeSet& set, const QString& writer, const QStringList& alreadyWritten)
    : set_(set) {
  const QSet<QString> done(alreadyWritten.begin(), alreadyWritten.end());
  for (const QString& c : set.chars) {
    if (!done.contains(c) && !queue_.contains(c)) queue_ << c;
  }
  Rng rng(fnv1a(writer + QLatin1Char('\n') + set.id));
  for (int i = queue_.size() - 1; i > 0; --i) {
    queue_.swapItemsAt(i, static_cast<int>(rng.next() % static_cast<quint64>(i + 1)));
  }
  // `total()` counts the set without duplicates.
  QStringList unique;
  for (const QString& c : set.chars) if (!unique.contains(c)) unique << c;
  set_.chars = unique;
}

void PracticeSession::saved(const QString& path) {
  if (queue_.isEmpty()) return;
  savedThisSession_.push_back({queue_.takeFirst(), path});
}

void PracticeSession::skip() {
  if (queue_.size() > 1) queue_.move(0, queue_.size() - 1);
}

bool PracticeSession::undoLastSaved(QString* removedPath) {
  if (savedThisSession_.isEmpty()) return false;
  const Saved last = savedThisSession_.takeLast();
  if (QFile::exists(last.path) && !QFile::remove(last.path)) {
    savedThisSession_.push_back(last);
    return false;
  }
  queue_.prepend(last.character);
  if (removedPath) *removedPath = last.path;
  return true;
}

}  // namespace practice
