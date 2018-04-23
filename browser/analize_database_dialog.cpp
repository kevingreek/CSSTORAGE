#include "analize_database_dialog.h"
#include "ui_analize_database_dialog.h"

AnalizeDatabaseDialog::AnalizeDatabaseDialog(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::AnalizeDatabaseDialog)
{
  ui->setupUi(this);
  setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));

  connect(&update_timer_, &QTimer::timeout, [this]()
  {
    ui->labelPools->setText(QString::number(pools_));
    ui->labelBadPools->setText(QString::number(bad_pools_));
    ui->labelChaines->setText(QString::number(chaines_));
  });
  update_timer_.start(300);
}

AnalizeDatabaseDialog::~AnalizeDatabaseDialog()
{
  delete ui;
}

void AnalizeDatabaseDialog::reject()
{
  terminated_ = true;
  QDialog::reject();
}
