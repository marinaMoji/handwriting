// Checks the C++ renderer and InkNet inference against PyTorch/numpy fixtures (ml/make_fixtures.py).
#include "InkNet.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <cmath>

static int failures = 0;
static void expect(bool cond, const QString& msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QString dir = QStringLiteral(INK_FIXTURE_DIR);

  QFile f(dir + "/ink_fixture.json");
  if (!f.open(QIODevice::ReadOnly)) {
    QTextStream(stdout) << "FAIL cannot open fixture\n";
    return 1;
  }
  const QJsonArray cases = QJsonDocument::fromJson(f.readAll()).object().value("cases").toArray();
  expect(cases.size() >= 3, "fixture has cases");

  ink::InkNet net;
  std::string err;
  expect(net.load((dir + "/tiny.inknet").toStdString(), &err), "tiny net loads");
  expect(net.numClasses() == 20, "20 classes");
  expect(net.className(19) == "漢", "UTF-8 class names survive");

  int idx = 0;
  for (const QJsonValue& cv : cases) {
    const QJsonObject c = cv.toObject();
    std::vector<ink::StrokeD> strokes;
    for (const QJsonValue& sv : c.value("strokes").toArray()) {
      ink::StrokeD s;
      for (const QJsonValue& pv : sv.toArray()) s.emplace_back(pv.toArray()[0].toDouble(), pv.toArray()[1].toDouble());
      strokes.push_back(s);
    }
    const std::vector<uint8_t> img = ink::render(strokes, ink::kGrid, c.value("width").toDouble());
    const QByteArray want = QByteArray::fromBase64(c.value("image").toString().toLatin1());
    expect(static_cast<size_t>(want.size()) == img.size(), QString("case %1: image size").arg(idx));
    int worst = 0, differing = 0;
    for (size_t i = 0; i < img.size() && i < static_cast<size_t>(want.size()); ++i) {
      const int d = std::abs(int(img[i]) - int(static_cast<uint8_t>(want[int(i)])));
      worst = std::max(worst, d);
      differing += d > 0;
    }
    expect(worst <= 1 && differing < 60, QString("case %1: render matches numpy (worst %2, %3 px differ)").arg(idx).arg(worst).arg(differing));

    const std::vector<float> got = net.logits(img.data());
    const QJsonArray wantLogits = c.value("logits").toArray();
    double worstLogit = 0;
    for (int i = 0; i < wantLogits.size(); ++i) worstLogit = std::max(worstLogit, std::fabs(got[i] - wantLogits[i].toDouble()));
    // Compare on the fixture's own rendered image too, to separate render drift from net drift.
    std::vector<uint8_t> ref(want.size());
    for (int i = 0; i < want.size(); ++i) ref[i] = static_cast<uint8_t>(want[i]);
    const std::vector<float> got2 = net.logits(ref.data());
    double worstNet = 0;
    for (int i = 0; i < wantLogits.size(); ++i) worstNet = std::max(worstNet, std::fabs(got2[i] - wantLogits[i].toDouble()));
    expect(worstNet < 1e-3, QString("case %1: net logits match PyTorch (max diff %2)").arg(idx).arg(worstNet));
    expect(worstLogit < 0.05, QString("case %1: render+net end to end (max diff %2)").arg(idx).arg(worstLogit));
    ++idx;
  }

  QTextStream(stdout) << (failures == 0 ? "All ink_net tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
