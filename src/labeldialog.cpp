#include "labeldialog.h"

LabelDialog::LabelDialog(QWidget *parent)
  : Dialog(parent, Qt::MSWindowsFixedSizeDialogHint)
{
  setWindowFlags (windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowTitle(tr("New Label"));

  nameEdit_ = new LineEdit(this);

  QMenu *iconMenu = new QMenu();
  QStringList strNameLabels;
  strNameLabels << tr("Important") << tr("Work") << tr("Personal")
                << tr("To Do") << tr("Later") << tr("Amusingly");
  for (int i = 0; i < 6; i++) {
    iconMenu->addAction(QIcon(QString(":/images/label_%1").arg(i+1)),
                        strNameLabels.at(i));
  }
  iconMenu->addSeparator();
  QAction *newIcon = new QAction(tr("Load icon..."), this);
  iconMenu->addAction(newIcon);

  iconButton_ = new QToolButton(this);
  iconButton_->setIconSize(QSize(16, 16));
  iconButton_->setPopupMode(QToolButton::MenuButtonPopup);
  iconButton_->setMenu(iconMenu);

  QMenu *colorTextMenu = new QMenu();
  colorTextMenu->addAction(tr("Default"));
  colorTextMenu->addSeparator();
  colorTextMenu->addAction(tr("Select color..."));

  colorTextButton_ = new QToolButton(this);
  colorTextButton_->setIconSize(QSize(16, 16));
  colorTextButton_->setPopupMode(QToolButton::MenuButtonPopup);
  colorTextButton_->setMenu(colorTextMenu);

  QMenu *colorBgMenu = new QMenu();
  colorBgMenu->addAction(tr("Default"));
  colorBgMenu->addSeparator();
  colorBgMenu->addAction(tr("Select color..."));

  colorBgButton_ = new QToolButton(this);
  colorBgButton_->setIconSize(QSize(16, 16));
  colorBgButton_->setPopupMode(QToolButton::MenuButtonPopup);
  colorBgButton_->setMenu(colorBgMenu);

  QHBoxLayout *layoutH1 = new QHBoxLayout();
  layoutH1->addWidget(new QLabel(tr("Name:")));
  layoutH1->addWidget(nameEdit_, 1);

  QHBoxLayout *layoutH2 = new QHBoxLayout();
  layoutH2->addWidget(new QLabel(tr("Icon:")));
  layoutH2->addWidget(iconButton_);
  layoutH2->addSpacing(10);
  layoutH2->addWidget(new QLabel(tr("Color text:")));
  layoutH2->addWidget(colorTextButton_);
  layoutH2->addSpacing(10);
  layoutH2->addWidget(new QLabel(tr("Color background:")));
  layoutH2->addWidget(colorBgButton_);
  layoutH2->addStretch(1);

  pageLayout->addLayout(layoutH1);
  pageLayout->addLayout(layoutH2);

  buttonBox->addButton(QDialogButtonBox::Ok);
  buttonBox->addButton(QDialogButtonBox::Cancel);
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
  connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));

  connect(nameEdit_, SIGNAL(textChanged(const QString&)),
          this, SLOT(nameEditChanged(const QString&)));
  connect(iconButton_, SIGNAL(clicked()),
          iconButton_, SLOT(showMenu()));
  connect(iconMenu, SIGNAL(triggered(QAction*)),
          this, SLOT(selectIcon(QAction*)));
  connect(newIcon, SIGNAL(triggered()),
          this, SLOT(loadIcon()));
  connect(colorTextButton_, SIGNAL(clicked()),
          colorTextButton_, SLOT(showMenu()));
  connect(colorTextMenu->actions().at(0), SIGNAL(triggered()),
          this, SLOT(defaultColorText()));
  connect(colorTextMenu->actions().at(2), SIGNAL(triggered()),
          this, SLOT(selectColorText()));
  connect(colorBgButton_, SIGNAL(clicked()),
          colorBgButton_, SLOT(showMenu()));
  connect(colorBgMenu->actions().at(0), SIGNAL(triggered()),
          this, SLOT(defaultColorBg()));
  connect(colorBgMenu->actions().at(2), SIGNAL(triggered()),
          this, SLOT(selectColorBg()));
}

void LabelDialog::loadData()
{
  iconButton_->setIcon(icon_);

  QPixmap pixmap(14, 14);
  if (!colorTextStr_.isEmpty())
    pixmap.fill(QColor(colorTextStr_));
  else
    pixmap.fill(QColor(0, 0, 0, 0));
  colorTextButton_->setIcon(pixmap);

  if (!colorBgStr_.isEmpty())
    pixmap.fill(QColor(colorBgStr_));
  else
    pixmap.fill(QColor(0, 0, 0, 0));
  colorBgButton_->setIcon(pixmap);
}

void LabelDialog::nameEditChanged(const QString& text)
{
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.isEmpty());
}

void LabelDialog::selectIcon(QAction *action)
{
  icon_ = action->icon();
  iconButton_->setIcon(action->icon());
}

void LabelDialog::loadIcon()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select Image"),
                                                  QDir::homePath(),
                                                  tr("Image files (*.jpg; *.jpeg; *.png; *.bmp)"));

  if (fileName.isNull()) return;

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox msgBox;
    msgBox.setText(tr("Load icon: can't open a file"));
    msgBox.exec();
    return;
  }

  QPixmap pixmap;
  if (pixmap.loadFromData(file.readAll())) {
    pixmap = pixmap.scaled(16, 16, Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
  }
  icon_.addPixmap(pixmap);
  iconButton_->setIcon(icon_);

  file.close();
}

void LabelDialog::defaultColorText()
{
  colorTextStr_ = "";
  QPixmap pixmap(14, 14);
  pixmap.fill(QColor(0, 0, 0, 0));
  colorTextButton_->setIcon(pixmap);
}

void LabelDialog::selectColorText()
{

  QColorDialog *colorDialog = new QColorDialog(this);

  if (colorDialog->exec() == QDialog::Rejected) {
    delete colorDialog;
    return;
  }

  QColor color = colorDialog->selectedColor();
  delete colorDialog;

  colorTextStr_ = color.name();
  QPixmap pixmap(14, 14);
  pixmap.fill(color);
  colorTextButton_->setIcon(pixmap);
}

void LabelDialog::defaultColorBg()
{
  colorBgStr_ = "";
  QPixmap pixmap(14, 14);
  pixmap.fill(QColor(0, 0, 0, 0));
  colorBgButton_->setIcon(pixmap);
}

void LabelDialog::selectColorBg()
{

  QColorDialog *colorDialog = new QColorDialog(this);

  if (colorDialog->exec() == QDialog::Rejected) {
    delete colorDialog;
    return;
  }

  QColor color = colorDialog->selectedColor();
  delete colorDialog;

  colorBgStr_ = color.name();
  QPixmap pixmap(14, 14);
  pixmap.fill(color);
  colorBgButton_->setIcon(pixmap);
}
