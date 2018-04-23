#include "progress_dialog.h"
#include "ui_progress_dialog.h"

ProgressDialog::ProgressDialog(QWidget *parent, const QString& caption, int max_value) :
  QDialog(parent),
  ui(new Ui::ProgressDialog)
{
  ui->setupUi(this);
  setWindowTitle(caption);
  setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
  ui->progress->setMaximum(max_value);

  connect(&update_timer_, &QTimer::timeout, [this]()
  {
    ui->progress->setValue(value_);
  });
  update_timer_.start(300);
}

ProgressDialog::~ProgressDialog()
{
  delete ui;
}

void ProgressDialog::reject()
{
  terminated_ = true;
  QDialog::reject();
}
