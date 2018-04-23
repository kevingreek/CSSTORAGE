#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QSharedPointer>
#include <QTimer>

namespace Ui {
class MainWindow;
}

namespace leveldb {
class DB;
class Status;
}

class QItemSelection;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = 0);
  ~MainWindow();

protected:
  void closeEvent(QCloseEvent* event) override;

private slots:
  void on_actionOpen_triggered();
  void on_actionClose_triggered();
  void on_comboHeads_currentIndexChanged(int);
  void on_listByChains_itemSelectionChanged();
  void on_listByKeys_itemSelectionChanged();
  void selected_chain_timer_timeout();
  void selected_key_timer_timeout();

private:
  void leveldb_error(const QString& message, const leveldb::Status& status);
  bool analize_database(leveldb::DB* db);
  void clear_current_pool_pane();

private:
  Ui::MainWindow *ui;
  QSharedPointer<leveldb::DB> db_;
  QByteArray selected_chain_head_hash_;
  QByteArray selected_pool_hash_;
  QTimer selected_chain_timer_;
  QTimer selected_key_timer_;
};

#endif // MAIN_WINDOW_H
