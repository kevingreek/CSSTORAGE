#include "main_window.h"
#include "ui_main_window.h"

#include <set>

#include <csdb/csdb.h>
#include <csdb/csdb_internal.h>

#include <leveldb/db.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <QByteArray>
#include <QTreeWidgetItem>
#include <QDateTime>
#include <QUuid>
#include <QSettings>

#include "analize_database_dialog.h"
#include "progress_dialog.h"

namespace {
void saveElementsState(QWidget *parent, QSettings &settings)
{
  for (QTreeView *tree: parent->findChildren<QTreeView*>()) {
    settings.setValue(tree->objectName() + "-column-widths", tree->header()->saveState());
  }

  for (QSplitter *splitter : parent->findChildren<QSplitter*>()) {
    settings.setValue(splitter->objectName() + "-splitter-state", splitter->saveState());
  }

  for (QTabWidget *tab : parent->findChildren<QTabWidget*>()) {
    QWidget *pCur = tab->currentWidget();
    settings.setValue(tab->objectName() + "-current-tab", pCur ? pCur->objectName() : QString());
  }
}

void restoreElementsState(QWidget *parent, QSettings &settings)
{
  for (QTreeView *tree : parent->findChildren<QTreeView*>()) {
    tree->header()->restoreState(settings.value( tree->objectName() + "-column-widths").toByteArray());
  }

  for (QSplitter *splitter : parent->findChildren<QSplitter*>()) {
    splitter->restoreState(settings.value(splitter->objectName() + "-splitter-state").toByteArray());
  }

  for (QTabWidget *tab : parent->findChildren<QTabWidget*>()) {
    QString cur_tab_name = settings.value(tab->objectName() + "-current-tab").toString();
    if (cur_tab_name.isEmpty()) continue;
    QWidget *cur_tab = parent->findChild<QWidget*>(cur_tab_name);
    if (0 != cur_tab)
      tab->setCurrentWidget(cur_tab);
  }
}

}

#define SETTINGS_MAINWINDOW_GROUP QStringLiteral("Main Window")
#define SETTINGS_GEOMETRY_KEY QStringLiteral("Geometry")
#define SETTINGS_STATE_KEY QStringLiteral("State")

MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  ui->splitter->widget(0)->setMinimumWidth(260);
  ui->splitter->setSizes(QList<int>() << 260 << 650);

  QSettings settings;
  settings.beginGroup(SETTINGS_MAINWINDOW_GROUP);
  restoreGeometry(settings.value(SETTINGS_GEOMETRY_KEY).toByteArray());
  restoreState(settings.value(SETTINGS_STATE_KEY).toByteArray());
  restoreElementsState(this, settings);

  ui->splitter->setEnabled(false);

  selected_chain_timer_.setSingleShot(true);
  selected_chain_timer_.setInterval(300);
  connect(&selected_chain_timer_, SIGNAL(timeout()), SLOT(selected_chain_timer_timeout()));
  selected_key_timer_.setSingleShot(true);
  selected_key_timer_.setInterval(300);
  connect(&selected_key_timer_, SIGNAL(timeout()), SLOT(selected_key_timer_timeout()));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  QSettings settings;
  settings.beginGroup(SETTINGS_MAINWINDOW_GROUP);
  settings.setValue(SETTINGS_GEOMETRY_KEY, saveGeometry());
  settings.setValue(SETTINGS_STATE_KEY, saveState());
  saveElementsState(this, settings);

  QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
  delete ui;
}

void MainWindow::on_actionClose_triggered()
{
  ui->comboHeads->clear();
  ui->listByKeys->clear();
  ui->listByChains->clear();
  clear_current_pool_pane();
  ui->splitter->setEnabled(false);
  selected_chain_head_hash_.clear();
  db_.reset();

  setWindowTitle(windowTitle().split(" :: ").front());
}

void MainWindow::clear_current_pool_pane()
{
  ui->listTransactions->clear();
  ui->labelCurHash->setText(QString());
  ui->labelPrevHash->setText(QString());
  ui->labelPoolTime->setText(QString());
  ui->labelPoolSequence->setText(QString());
  ui->labelTransactions->setText(QString());
}

void MainWindow::on_actionOpen_triggered()
{
  QString path = QFileDialog::getExistingDirectory(this);
  if (path.isEmpty()) {
    return;
  }

  QDir dir(path);
  QFileInfo i1(dir.absoluteFilePath(QLatin1Literal("transactions")));
  QFileInfo i2(dir.absoluteFilePath(QLatin1Literal("../transactions")));
  if ((!i1.isDir()) && (!i2.isDir())) {
    QMessageBox::critical(this, QStringLiteral("Ошибка открытия базы"), QStringLiteral("Ни один из путей (\"%1\" и \"%2\" "
      "не является валидным путём для размещения базы транзакций.")
      .arg(i1.absoluteFilePath(), i2.absoluteFilePath()));
    return;
  }

  leveldb::Status status(leveldb::Status::NotFound(leveldb::Slice()));
  leveldb::DB *temp;
  QString open_path;
  if (i1.isDir()) {
    open_path = i1.absoluteFilePath();
    status = leveldb::DB::Open(leveldb::Options(), open_path.toUtf8().constData(), &temp);
  }
  if (!status.ok() && (i2.isDir())) {
    open_path = i2.absoluteFilePath();
    status = leveldb::DB::Open(leveldb::Options(), open_path.toUtf8().constData(), &temp);
  }
  if (!status.ok()) {
    leveldb_error(QStringLiteral("Не удалось открыть базу."), status);
    return;
  }

  on_actionClose_triggered();
  if (!analize_database(temp)) {
    on_actionClose_triggered();
    delete temp;
    return;
  }
  db_.reset(temp);
  setWindowTitle(windowTitle().split(" :: ").front() + " :: " + open_path);
  ui->splitter->setEnabled(true);
}

void MainWindow::leveldb_error(const QString& message, const leveldb::Status& status)
{
  QMessageBox::critical(this, QStringLiteral("Ошибка работы с LevelDB"),
                        QStringLiteral("%1\r\nLevelDB Status: %2").arg(message)
                        .arg(trUtf8(status.ToString().c_str())));
}

namespace {
QString hash_to_str(const std::string& hash)
{
  QString result = QString::fromUtf8(csdb::to_hex(hash).c_str());
  if (!hash.empty()) {
    bool is_ascii = true;
    for (size_t i = 0; i < hash.size(); i++) {
      char c = hash[i];
      if ((' ' > c) || (127 < c)) {
        is_ascii = false;
        break;
      }
    }
    if (is_ascii) {
      result += QStringLiteral(" \"%1\"").arg(hash.c_str());
    }
  }
  return result;
}
}

bool MainWindow::analize_database(leveldb::DB* db)
{
  csdb_internal::heads_t heads;
  csdb_internal::tails_t tails;
  bool ok = false;

  AnalizeDatabaseDialog a(this);
  QThread *t = QThread::create([&]()
  {
    // TODO: Реализовать проверку на зацикливание цепочек.
    QSharedPointer<leveldb::Iterator> it(db->NewIterator(leveldb::ReadOptions()));
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
      if (a.terminated_) {
        return;
      }
      a.pools_++;

      std::string ch = it->key().ToString();
      QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << hash_to_str(ch));
      item->setData(0, Qt::UserRole, QByteArray::fromStdString(ch));
      ui->listByKeys->addTopLevelItem(item);
      leveldb::Slice sp = it->value();
      csdb_internal::ibstream is(sp.data(), sp.size());
      csdb_internal::PoolHeader ph;
      if (!ph.get(is)) {
        // Невалидный блок. В список всё равно добавлился, а обрабатывать не надо.
        // TODO: Как-нибудь пометить его.
        a.bad_pools_++;
        continue;
      }

      csdb_internal::update_heads_and_tails(heads, tails, ch, ph.prev_pool_hash_);
      a.chaines_ = heads.size();
    }
    ok = it->status().ok(); // Нормально дошли до конца.

    QTimer::singleShot(0, &a, [&a](){a.accept();});
  });
  t->start();

  a.exec();
  t->wait();
  t->deleteLater();

  if (!ok) {
    return true;
  }

  for (const auto it : heads) {
    int i = ui->comboHeads->count();
    ui->comboHeads->addItem(
      QStringLiteral("%1 (%2 п.%3)").arg(hash_to_str(it.first))
        .arg(it.second.len_)
        .arg(it.second.next_.empty() ? QStringLiteral("") : QStringLiteral("; оборвана")));
    ui->comboHeads->setItemData(i, QByteArray::fromStdString(it.first), Qt::UserRole);
    ui->comboHeads->setItemData(i, static_cast<int>(it.second.len_), Qt::UserRole + 1);
  }

  QTimer::singleShot(0, this, SLOT(selected_chain_timer_timeout()));

  return ok;
}

void MainWindow::on_comboHeads_currentIndexChanged(int)
{
  selected_chain_timer_.stop();
  selected_chain_timer_.start();
}

void MainWindow::selected_chain_timer_timeout()
{
  int index = ui->comboHeads->currentIndex();
  QByteArray hash;
  if (0 <= index) {
    hash = ui->comboHeads->itemData(index, Qt::UserRole).toByteArray();
  }
  if (selected_chain_head_hash_ == hash) {
    return;
  }
  selected_chain_head_hash_ = hash;
  ui->listByChains->clear();

  if (selected_chain_head_hash_.isEmpty()) {
    return;
  }

  std::string h = selected_chain_head_hash_.toStdString();
  ProgressDialog pd(this, QStringLiteral("Загрузка цепочки..."),
                    ui->comboHeads->itemData(index, Qt::UserRole + 1).toInt());
  leveldb::Status status;

  QThread *t = QThread::create([&]()
  {
    std::set<std::string> loaded;
    while(!h.empty()) {
      if (!loaded.insert(h).second) {
        // Цепочка зациклилась!
        break;
      }
      if (pd.terminated_) {
        return;
      }
      pd.value_++;
      QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << hash_to_str(h));
      item->setData(0, Qt::UserRole, QByteArray::fromStdString(h));
      ui->listByChains->addTopLevelItem(item);

      std::string value;
      status = db_->Get(leveldb::ReadOptions(), h, &value);
      if (!status.ok()) {
        break;
      }

      csdb_internal::ibstream is(value.data(), value.size());
      csdb_internal::PoolHeader ph;
      if (!ph.get(is)) {
        // Невалидный блок. Останавлиаем сканирование
        // TODO: Как-нибудь пометить его.
        break;
      }
      h = ph.prev_pool_hash_;
    }
    QTimer::singleShot(0, &pd, [&pd](){pd.accept();});
  });
  t->start();

  pd.exec();
  t->wait();
  t->deleteLater();

  if ((!status.ok()) && (!status.IsNotFound())) {
  }
}

void MainWindow::on_listByChains_itemSelectionChanged()
{
  selected_key_timer_.stop();
  selected_key_timer_.start();
}

void MainWindow::on_listByKeys_itemSelectionChanged()
{
  selected_key_timer_.stop();
  selected_key_timer_.start();
}

void MainWindow::selected_key_timer_timeout()
{
  QList<QTreeWidgetItem*> selected;
  ui->listByKeys->selectedItems();
  if (0 == ui->tabKeys->currentIndex()) {
    selected = ui->listByKeys->selectedItems();
  } else {
    selected = ui->listByChains->selectedItems();
  }

  QByteArray new_hash;
  if (!selected.isEmpty()) {
    new_hash = selected.first()->data(0, Qt::UserRole).toByteArray();
  }

  if (selected_pool_hash_ == new_hash) {
    return;
  }
  selected_pool_hash_ = new_hash;

  clear_current_pool_pane();
  if (selected_pool_hash_.isEmpty()) {
    return;
  }

  std::string h = selected_pool_hash_.toStdString();
  ui->labelCurHash->setText(hash_to_str(h));

  std::string value;
  leveldb::Status status = db_->Get(leveldb::ReadOptions(), h, &value);
  if (!status.ok()) {
    leveldb_error(QStringLiteral("Не удалось получить данные пула."), status);
    return;
  }

  csdb_internal::ibstream is(value.data(), value.size());
  csdb_internal::PoolHeader ph;
  if (!ph.get(is)) {
    // TODO: Пометить блок, если уже не помечен
    QMessageBox::critical(this, QStringLiteral("Ошибка чтения пула"),
                          QStringLiteral("Не удалось декодировать заголовок блока по хэшем %1.")
                          .arg(hash_to_str(h)));
    return;
  }
  // TODO: Если блок помечен, как плохой - снять отметку.

  if (ph.prev_pool_hash_.empty()) {
    ui->labelPrevHash->setText(QStringLiteral("---"));
  } else {
    ui->labelPrevHash->setText(QStringLiteral("<A HREF=\"%1\">%2<A>")
                               .arg(QString(QByteArray::fromStdString(ph.prev_pool_hash_).toHex()),
                                    hash_to_str(ph.prev_pool_hash_)));
  }
  ui->labelPoolTime->setText(QDateTime::fromTime_t(static_cast<time_t>(ph.time_))
                             .toString("dd.MM.yyyy HH:mm:ss"));
  ui->labelPoolSequence->setText(QString::number(ph.sequence_));

  size_t tran_count = is.size() / sizeof(csdb::Tran);
  if ((0 != (is.size() % sizeof(csdb::Tran))) || (tran_count != ph.transaction_count_)) {
    // TODO: Пометить как блок с плохим транзакциями
    QMessageBox::critical(this, QStringLiteral("Ошибка чтения пула"),
                          QStringLiteral("Блок транзакций в пуле с хэшем %1 не корректен.")
                          .arg(hash_to_str(h)));
    ui->labelTransactions->setText(QStringLiteral("&lt;блок транзакций не валидный&gt;"));
    return;
  }
  ui->labelTransactions->setText(QStringLiteral("%1 шт.").arg(tran_count));

  const csdb::Tran* transactions = static_cast<const csdb::Tran*>(is.data());
  for (size_t i = 0; i < tran_count; i++) {
    const csdb::Tran& t = transactions[i];
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, QStringLiteral("%1.%2").arg(QString(QByteArray::fromStdString(h).toHex().toUpper()))
                  .arg(i + 1));
    item->setText(1, QString::asprintf("0x%016" PRIX64, t.Hash));
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);

    QByteArray uuid;
    uuid.resize(sizeof(t.InnerID));
    memcpy(uuid.data(), &(t.InnerID), sizeof(t.InnerID));
    item->setText(2, QUuid::fromRfc4122(uuid).toString());
    item->setText(3, trUtf8(t.A_source));
    item->setText(4, trUtf8(t.A_target));
    item->setText(5, trUtf8(csdb::amount_to_string(t.Amount, t.Amount1, 2).c_str()));
    item->setTextAlignment(5, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(6, trUtf8(t.Currency));
    ui->listTransactions->addTopLevelItem(item);
  }
}
