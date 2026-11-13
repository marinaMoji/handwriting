#include "PracticeSession.h"
#include "SampleIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

static Sample sampleOf(const QString& ch, const QString& writer) {
  Sample s;
  s.character = ch;
  s.strokes = {{{0, 0}, {10, 10}}};
  s.writer = writer;
  return s;
}

using practice::PracticeSession;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  qunsetenv("HANDWRITING_SAMPLES_DIR");

  // The shipped prompt lists.
  {
    QString err;
    const QVector<PracticeSet> sets = practice::loadSets(QStringLiteral(HANDWRITING_PRACTICE_DOCS_DIR), &err);
    QSet<QString> ids;
    for (const PracticeSet& s : sets) ids.insert(s.id);
    expect(ids.contains(QStringLiteral("core")) && ids.contains(QStringLiteral("dev-extra")), "core and dev-extra sets load");
    expect(ids.contains(QStringLiteral("list-simple")), "sample-chars.json categories become sets");
    for (const PracticeSet& s : sets) {
      if (s.id == QLatin1String("core")) {
        expect(s.tag == QLatin1String("test") && s.chars.size() == 150, "core is the 150-character held-out set");
        QSet<QString> uniq(s.chars.begin(), s.chars.end());
        expect(uniq.size() == s.chars.size(), "core has no duplicates");
      }
      if (s.id == QLatin1String("dev-extra")) expect(s.tag == QLatin1String("dev"), "dev-extra writes to the dev set");
    }
    // The held-out core must not contain a development character.
    QSet<QString> dev;
    for (const PracticeSet& s : sets) if (s.id.startsWith(QLatin1String("list-"))) for (const QString& c : s.chars) dev.insert(c);
    for (const PracticeSet& s : sets) {
      if (s.id != QLatin1String("core")) continue;
      int overlap = 0;
      for (const QString& c : s.chars) overlap += dev.contains(c);
      expect(overlap == 0, "core shares no character with the development lists");
    }
    expect(practice::loadSets(QStringLiteral("/nonexistent"), &err).isEmpty() && !err.isEmpty(), "missing docs dir reports an error");
  }

  // Directories by tag.
  {
    expect(practice::sampleDirForTag(QStringLiteral("test"), QStringLiteral("/p/samples")) == QLatin1String("/p/samples-test"), "test -> samples-test");
    expect(practice::sampleDirForTag(QStringLiteral("dev"), QStringLiteral("/p/samples")) == QLatin1String("/p/samples"), "dev -> samples");
    expect(practice::sampleDirForTag(QStringLiteral("textbook"), QStringLiteral("/p/samples")) == QLatin1String("/p/samples-textbook"), "textbook -> samples-textbook");
    qputenv("HANDWRITING_SAMPLES_DIR", "/tmp/forced");
    expect(practice::sampleDirForTag(QStringLiteral("test"), QStringLiteral("/p/samples")) == QLatin1String("/tmp/forced"), "$HANDWRITING_SAMPLES_DIR overrides");
    qunsetenv("HANDWRITING_SAMPLES_DIR");
  }

  // Sessions.
  PracticeSet set;
  set.id = QStringLiteral("t");
  set.tag = QStringLiteral("test");
  for (const char* c : {"一", "二", "三", "四", "五", "六", "七", "八"}) set.chars << QString::fromUtf8(c);
  set.chars << QString::fromUtf8("𠮷");

  {
    PracticeSession a(set, QStringLiteral("w1"), {});
    PracticeSession b(set, QStringLiteral("w1"), {});
    PracticeSession c(set, QStringLiteral("w2"), {});
    QStringList oa, ob, oc;
    for (PracticeSession* s : {&a, &b, &c}) {
      QStringList& o = s == &a ? oa : s == &b ? ob : oc;
      while (!s->finished()) { o << s->current(); s->saved(QString()); }
    }
    expect(oa == ob, "same writer and set give the same order");
    expect(oa != oc, "different writers see different orders");
    QSet<QString> all(oa.begin(), oa.end());
    expect(all.size() == set.chars.size() && all.contains(QString::fromUtf8("𠮷")), "every character offered once, non-BMP intact");
  }
  {
    PracticeSession s(set, QStringLiteral("w1"), {QString::fromUtf8("一"), QString::fromUtf8("二")});
    expect(s.total() == 9 && s.remaining() == 7 && s.written() == 2, "already written characters are not offered again");
    const QString first = s.current();
    s.skip();
    expect(s.current() != first && s.remaining() == 7, "skip defers the character");
    QStringList seen;
    while (!s.finished()) { seen << s.current(); s.saved(QString()); }
    expect(seen.contains(first) && seen.last() == first, "a skipped character comes back at the end");
  }
  {
    QTemporaryDir tmp;
    Sample x = sampleOf(QString::fromUtf8("三"), QStringLiteral("w1"));
    QString p1, p2, err;
    expect(sample_io::saveSample(tmp.path(), x, &p1, &err), "save sample");
    expect(sample_io::saveSample(tmp.path(), sampleOf(QString::fromUtf8("四"), QStringLiteral("w2")), &p2, &err), "save other writer's sample");
    Sample old = sampleOf(QString::fromUtf8("五"), QString());
    QString p3;
    sample_io::saveSample(tmp.path(), old, &p3, &err);
    const QStringList done = practice::charsWrittenBy(tmp.path(), QStringLiteral("w1"));
    expect(done == QStringList{QString::fromUtf8("三")}, "charsWrittenBy sees only this writer's samples");
    expect(practice::charsWrittenBy(tmp.path(), QString()).isEmpty(), "no writer, nothing counted");

    PracticeSession s(set, QStringLiteral("w1"), done);
    const QString c1 = s.current();
    QString path;
    sample_io::saveSample(tmp.path(), sampleOf(c1, QStringLiteral("w1")), &path, &err);
    s.saved(path);
    expect(s.remaining() == 7, "saved advances");
    QString removed;
    expect(s.undoLastSaved(&removed) && removed == path && !QFile::exists(path), "undo deletes the file just saved");
    expect(s.current() == c1 && s.remaining() == 8, "undo offers the character again");
    expect(!s.undoLastSaved(), "nothing more to undo");
    expect(QFile::exists(p1), "undo never touches earlier files");
  }
  return failures ? 1 : 0;
}
