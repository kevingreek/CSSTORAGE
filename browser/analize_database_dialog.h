#ifndef ANALIZE_DATABASE_DIALOG_H
#define ANALIZE_DATABASE_DIALOG_H

#include <QDialog>
#include <QTimer>

namespace Ui {
class AnalizeDatabaseDialog;
}

class AnalizeDatabaseDialog : public QDialog
{
  Q_OBJECT

public:
  explicit AnalizeDatabaseDialog(QWidget *parent = 0);
  ~AnalizeDatabaseDialog();

public:
  quint64 pools_ = 0;
  quint64 bad_pools_ = 0;
  size_t chaines_ = 0;
  bool terminated_ = false;

protected:
  void reject() override;

private:
  Ui::AnalizeDatabaseDialog *ui;
  QTimer update_timer_;
};

#endif // ANALIZE_DATABASE_DIALOG_H
