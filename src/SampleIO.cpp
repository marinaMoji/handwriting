#include "SampleIO.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace sample_io {

QByteArray toJson(const Sample& sample) {
  QJsonArray strokes;
  for (const Stroke& s : sample.strokes) {
    QJsonArray pts;
    for (const StrokePoint& p : s) {
      pts.append(QJsonArray{p.x, p.y});
    }
    strokes.append(pts);
  }
  QJsonObject o;
  o[QStringLiteral("version")] = 2;
  o[QStringLiteral("character")] = sample.character;
  o[QStringLiteral("surface")] = QJsonArray{sample.surfaceWidth, sample.surfaceHeight};
  o[QStringLiteral("source")] = sample.source;
  o[QStringLiteral("timestamp")] = sample.timestamp;
  o[QStringLiteral("note")] = sample.note;
  o[QStringLiteral("strokes")] = strokes;
  o[QStringLiteral("writer")] = sample.writer;
  o[QStringLiteral("handedness")] = sample.handedness;
  o[QStringLiteral("device")] = sample.device;
  o[QStringLiteral("set")] = sample.set;
  o[QStringLiteral("session")] = sample.session;
  o[QStringLiteral("prompted")] = sample.prompted;
  return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

bool fromJson(const QByteArray& json, Sample* sample, QString* error) {
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (doc.isNull() || !doc.isObject()) {
    if (error) *error = QStringLiteral("invalid JSON: %1").arg(pe.errorString());
    return false;
  }
  const QJsonObject o = doc.object();
  Sample out;
  out.character = o.value(QStringLiteral("character")).toString();
  if (out.character.isEmpty()) {
    if (error) *error = QStringLiteral("missing \"character\"");
    return false;
  }
  const QJsonArray surface = o.value(QStringLiteral("surface")).toArray();
  if (surface.size() == 2) {
    out.surfaceWidth = surface[0].toInt(300);
    out.surfaceHeight = surface[1].toInt(300);
  }
  out.source = o.value(QStringLiteral("source")).toString();
  out.timestamp = o.value(QStringLiteral("timestamp")).toString();
  out.note = o.value(QStringLiteral("note")).toString();
  out.writer = o.value(QStringLiteral("writer")).toString();
  out.handedness = o.value(QStringLiteral("handedness")).toString();
  out.device = o.value(QStringLiteral("device")).toString();
  out.set = o.value(QStringLiteral("set")).toString();
  out.session = o.value(QStringLiteral("session")).toString();
  out.prompted = o.value(QStringLiteral("prompted")).toBool(false);

  const QJsonArray strokes = o.value(QStringLiteral("strokes")).toArray();
  if (strokes.isEmpty()) {
    if (error) *error = QStringLiteral("no strokes");
    return false;
  }
  for (const QJsonValue& sv : strokes) {
    Stroke s;
    for (const QJsonValue& pv : sv.toArray()) {
      const QJsonArray p = pv.toArray();
      if (p.size() != 2) {
        if (error) *error = QStringLiteral("bad point in stroke");
        return false;
      }
      s.push_back(StrokePoint{p[0].toInt(), p[1].toInt()});
    }
    if (s.isEmpty()) {
      if (error) *error = QStringLiteral("empty stroke");
      return false;
    }
    out.strokes.push_back(s);
  }
  *sample = out;
  return true;
}

bool saveSample(const QString& dir, const Sample& sample, QString* pathOut, QString* error) {
  if (sample.character.isEmpty() || sample.strokes.isEmpty()) {
    if (error) *error = QStringLiteral("nothing to save");
    return false;
  }
  if (!QDir().mkpath(dir)) {
    if (error) *error = QStringLiteral("cannot create %1").arg(dir);
    return false;
  }
  const uint cp = sample.character.toUcs4().value(0);
  const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
  const QString fileName = QStringLiteral("U+%1_%2.json")
                               .arg(QString::number(cp, 16).toUpper().rightJustified(4, QLatin1Char('0')))
                               .arg(stamp);
  const QString path = QDir(dir).filePath(fileName);
  Sample toWrite = sample;
  if (toWrite.timestamp.isEmpty()) {
    toWrite.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
  }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    if (error) *error = QStringLiteral("cannot write %1").arg(path);
    return false;
  }
  f.write(toJson(toWrite));
  if (!f.commit()) {
    if (error) *error = QStringLiteral("commit failed for %1").arg(path);
    return false;
  }
  if (pathOut) *pathOut = path;
  return true;
}

bool loadSample(const QString& path, Sample* sample, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("cannot read %1").arg(path);
    return false;
  }
  return fromJson(f.readAll(), sample, error);
}

QStringList findSamples(const QString& dir) {
  QStringList out;
  QDirIterator it(dir, QStringList{QStringLiteral("*.json")}, QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    out << it.next();
  }
  out.sort();
  return out;
}

}  // namespace sample_io
