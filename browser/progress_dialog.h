#ifndef PROGRESS_DIALOG_H
#define PROGRESS_DIALOG_H

#include <QDialog>
#include <QTimer>

namespace Ui {
class ProgressDialog;
}

class ProgressDialog : public QDialog
{
  Q_OBJECT

public:
  ProgressDialog(QWidget *parent, const QString& caption, int max_value);
  ~ProgressDialog();

public:
  int value_ = 0;
  bool terminated_ = false;

protected:
  void reject() override;

private:
  Ui::ProgressDialog *ui;
  QTimer update_timer_;
};

#endif // PROGRESS_DIALOG_H
