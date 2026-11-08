#include "CandidatePanel.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

CandidatePanel::CandidatePanel(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* title = new QLabel(tr("Candidates"), this);
  QFont titleFont = title->font();
  titleFont.setBold(true);
  title->setFont(titleFont);
  layout->addWidget(title);

  list_ = new QListWidget(this);
  list_->setMinimumWidth(220);
  list_->setFont(QFont(QStringLiteral("Hiragino Sans"), 18));
  layout->addWidget(list_, 1);

  emptyLabel_ = new QLabel(tr("Draw a character to see candidates."), this);
  emptyLabel_->setWordWrap(true);
  emptyLabel_->setStyleSheet(QStringLiteral("color: #666;"));
  layout->addWidget(emptyLabel_);

  connect(list_, &QListWidget::currentTextChanged, this, [this](const QString&) {
    emit selectionChanged(selectedCharacter());
  });
}

void CandidatePanel::setResult(const RecognitionResult& result) {
  list_->clear();
  for (const Candidate& c : result.candidates) {
    const QString text = QStringLiteral("%1  [%2 #%3]  %4")
                             .arg(c.character)
                             .arg(c.modelSource)
                             .arg(c.modelRank)
                             .arg(c.score, 0, 'f', 4);
    auto* item = new QListWidgetItem(text, list_);
    item->setData(Qt::UserRole, c.character);
  }
  const bool empty = result.candidates.isEmpty();
  list_->setVisible(!empty);
  emptyLabel_->setVisible(empty);
  if (!empty) {
    list_->setCurrentRow(0);
  }
}

void CandidatePanel::clear() {
  list_->clear();
  list_->setVisible(false);
  emptyLabel_->setVisible(true);
}

QString CandidatePanel::selectedCharacter() const {
  QListWidgetItem* item = list_->currentItem();
  if (!item) {
    return {};
  }
  return item->data(Qt::UserRole).toString();
}
