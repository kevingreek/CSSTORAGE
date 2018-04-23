#include "csdb/csdb.h"
#include "csdb/csdb_internal.h"

#include <iostream>
#include <iomanip>
#include <mutex>
#include <vector>
#include <map>
#include <type_traits>
#include <memory>

#if defined(_MSC_VER)
# include <windows.h>
# include <Shlobj.h>
#elif defined(__APPLE__)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/write_batch.h"

static_assert(std::is_trivially_copyable<csdb::Tran>::value,
              "Transaction type MUST be trivially copyable!");

namespace {
std::mutex init_mutex_, update_balance_mutex_;
std::shared_ptr<leveldb::DB> main_base_;
std::shared_ptr<leveldb::DB> balance_base_;

std::mutex last_save_hash_mutex_;
std::string last_save_hash_;

std::mutex current_head_hash_mutex_;
std::string current_head_hash_;

std::string get_common_appdata_path()
{
#if defined(_MSC_VER)
  char path[MAX_PATH];
  HRESULT hresult= ::SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL,
                                      SHGFP_TYPE_CURRENT | KF_FLAG_CREATE, path);
  assert(S_OK == hresult);

  std::string res(path);
  char end = res[res.size() - 1];
  if (('\\' != end) && ('/' != end)) {
    res += '/';
  }
  return res;
#elif defined(__APPLE__)
  return "/Users/Shared/";
#else
  std::string res(getenv("HOME"));
  assert(!res.empty());
  char end = res[res.size() - 1];
  if (('\\' != end) && ('/' != end)) {
    res += '/';
  }
  res += ".appdata";
  mkdir(res.c_str(), 0755);
  res += "/";
  return res;
#endif
}

#ifndef _MSC_VER
#define sprintf_s(s,sz,f,...) sprintf(s,f,##__VA_ARGS__)
#endif

inline bool is_empty(const std::string* str)
{
  return (nullptr == str) || str->empty();
}


inline char hex_digit(uint8_t byte)
{
  return static_cast<char>((byte < 10) ? (byte + '0') : (byte - 10 + 'A'));
}

inline bool digit_from_hex(char c, uint8_t& byte)
{
  c = toupper(c);
  if ((c >= '0') && (c <= '9')) {
    byte = static_cast<uint8_t>(c - '0');
  } else if ((c >= 'A') && (c <= 'F')) {
    byte = static_cast<uint8_t>(c - 'A' + 10);
  } else {
    return false;
  }
  return true;
}

struct amount
{
  int32_t i_;
  uint64_t f_;
};

typedef std::map<std::string, amount> account_balance;
typedef std::map<std::string, account_balance> account_balances;

account_balances::iterator read_balance_if_not_exists(account_balances& balances, leveldb::DB* balance,
                                                      const std::string& account)
{
  auto res = balances.emplace(account, account_balance());
  if (res.second) {
    std::string value;
    leveldb::Status status = balance->Get(leveldb::ReadOptions(), account, &value);
    if (!status.IsNotFound()) {
      if (!status.ok()) {
        std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
        return balances.end();
      }
      csdb_internal::ibstream ib(value.data(), value.size());
      if (!ib.get(res.first->second)) {
        std::cerr << __func__ << ": Invalid record in balance database." << std::endl;
        return balances.end();
      }
    }
  }
  return res.first;
}

bool update_balances(leveldb::DB* balance, const csdb::Tran* transactions, size_t transactions_count)
{
  // TODO: Сделать либо блокировку (для многопоточного обновления), либо разобраться со Snapshots
  account_balances balances;

  for(; transactions_count--; transactions++) {
    auto it = read_balance_if_not_exists(balances, balance, transactions->A_source);
    if (balances.end() == it) {
      return false;
    }
    amount& as = it->second[transactions->Currency];
    csdb::amounts_sub(as.i_, as.f_, static_cast<int32_t>(transactions->Amount), transactions->Amount1);

    it = read_balance_if_not_exists(balances, balance, transactions->A_target);
    if (balances.end() == it) {
      return false;
    }
    amount& at = it->second[transactions->Currency];
    csdb::amounts_add(at.i_, at.f_, static_cast<int32_t>(transactions->Amount), transactions->Amount1);
  }

  leveldb::WriteBatch batch;
  for (const auto it : balances) {
    csdb_internal::obstream ob;
    ob << it.second;
    batch.Put(it.first, leveldb::Slice(ob));
  }
  leveldb::Status status = balance->Write(leveldb::WriteOptions(), &batch);
  if (!status.ok()) {
    std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
    return false;
  }

  return true;
}

bool initial_scan(leveldb::DB* main, leveldb::DB* balance)
{
  csdb_internal::heads_t heads;
  csdb_internal::tails_t tails;
  std::shared_ptr<leveldb::Iterator> it(main->NewIterator(leveldb::ReadOptions()));

  // TODO: Реализовать проверку на зацикливание цепочек.
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    std::string ch = it->key().ToString();
    leveldb::Slice sp = it->value();
    csdb_internal::ibstream is(sp.data(), sp.size());
    csdb_internal::PoolHeader ph;
    if (!ph.get(is)) {
      std::cerr << "Invalid pool encountered under hash: " << csdb::to_hex(ch) << "; skipped." << std::endl;
      continue;
    }

    size_t tran_count = is.size() / sizeof(csdb::Tran);
    if ((0 != (is.size() % sizeof(csdb::Tran))) || (tran_count != ph.transaction_count_)) {
      std::cerr << "Invalid transactions block in poolunder hash: " << csdb::to_hex(ch) << "; skipped." << std::endl;
      continue;
    }
    if ( 0 < tran_count) {
      if (!update_balances(balance, static_cast<const csdb::Tran*>(is.data()), tran_count)) {
        return false;
      }
    }
    csdb_internal::update_heads_and_tails(heads, tails, ch, ph.prev_pool_hash_);
  }

  // Посмотрим, сколько у нас завершённых цепочек.
  if([&heads]() -> bool {
      std::lock_guard<std::mutex> lock(current_head_hash_mutex_);
      current_head_hash_.clear();
      for (const auto it : heads) {
        if (!it.second.next_.empty()) {
          continue;
        }
        if (!current_head_hash_.empty()) {
          return false;
        }
        current_head_hash_ = it.first;
      }
      return true;
    }()) {
    return true;
  }

  std::cerr << "Database prescan errors." << std::endl;
  std::cerr << "Encountered more than one chains or orphan chains. List follows:" << std::endl;
  for (auto it = heads.begin(); it != heads.end(); ++it) {
    std::cerr << "  " << csdb::to_hex(it->first) << " (lenght = " << it->second.len_ << "): ";
    if (it->second.next_.empty()) {
      std::cerr << "Normal";
    } else {
      std::cerr << "Orphan";
    }
    std::cerr << std::endl;
  }

  return false;
}

bool get_transactions(bool& has_more, const char* addr, size_t limit, size_t offset,
                      std::function<void(const std::string&, size_t, const csdb::Tran&)> putfn)
{
  has_more = false;
  std::string hash;
  {
    std::lock_guard<std::mutex> lock(current_head_hash_mutex_);
    hash = current_head_hash_;
  }

  csdb_internal::PoolHeader ph;
  for(size_t index = 0; !hash.empty(); hash = ph.prev_pool_hash_) {
    std::string value;
    leveldb::Status status = main_base_->Get(leveldb::ReadOptions(), hash, &value);
    if (status.IsNotFound()) {
      std::cerr << __func__ << ": Unexpected chain break - pool not found for hash: "
                << csdb::to_hex(hash) << std::endl;
      return false;
    }
    if (!status.ok()) {
      std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
      return false;
    }

    csdb_internal::ibstream is(value.data(), value.size());
    if (!ph.get(is)) {
      std::cerr << "Invalid pool encountered under hash: " << csdb::to_hex(hash) << std::endl;
      return false;
    }

    size_t tran_count = is.size() / sizeof(csdb::Tran);
    if ((0 != (is.size() % sizeof(csdb::Tran))) || (tran_count != ph.transaction_count_)) {
      std::cerr << "Invalid transactions block in pool under hash: " << csdb::to_hex(hash) << std::endl;
      return false;
    }
    if (0 == tran_count) {
      continue;
    }

    const csdb::Tran* transactions = static_cast<const csdb::Tran*>(is.data());

    for (size_t i = 0; i < tran_count; i++) {
      const csdb::Tran& t = transactions[tran_count - i - 1];
      if ((0 != strcmp(addr, t.A_source)) && (0 != strcmp(addr, t.A_target))) {
        continue;
      }
      if (index >= (offset + limit)) {
        has_more = true;
        return true;
      }
      if (index >= offset) {
        putfn(hash, tran_count - i, t);
      }
      ++index;
    }
  }

  return true;
}

} //namespace

namespace csdb {

bool init(const char* path_to_database)
{
  std::lock_guard<std::mutex> guard(init_mutex_);

  if (main_base_ || balance_base_) {
    return false;
  }

  leveldb::Status status;
  std::string path;
  if (nullptr == path_to_database) {
    path = get_common_appdata_path() + "CREDITS";
    if(!leveldb::Env::Default()->FileExists(path)) {
      status = leveldb::Env::Default()->CreateDir(path);
      if (!status.ok()) {
        std::cerr << "Error create directory: " << status.ToString().c_str() << std::endl;
        return false;
      }
    }

    path += "/database";
    if(!leveldb::Env::Default()->FileExists(path)) {
      status = leveldb::Env::Default()->CreateDir(path);
      if (!status.ok()) {
        std::cerr << "Error create directory: " << status.ToString().c_str() << std::endl;
        return false;
      }
    }
  } else {
    path = path_to_database;
    char end = path[path.size() - 1];
    if (('\\' != end) && ('/' != end)) {
      path += '/';
    }
    if(!leveldb::Env::Default()->FileExists(path)) {
      std::cerr << "Error access directory: " << path << std::endl;
      return false;
    }
  }

  std::string main_path(path), balance_path(path);
  main_path += "/transactions";
  balance_path += "/balance";

  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* temp = nullptr;

  status = leveldb::DB::Open(options, main_path, &temp);
  if (!status.ok()) {
    std::cerr << "Error open main database: " << status.ToString().c_str() << std::endl;
    return false;
  }
  std::shared_ptr<leveldb::DB> main(temp);

  leveldb::DestroyDB(balance_path, leveldb::Options());
  options.create_if_missing = true;
  options.error_if_exists = true;
  status = leveldb::DB::Open(options, balance_path, &temp);
  if (!status.ok()) {
    std::cerr << "Error creating balance database: " << status.ToString().c_str() << std::endl;
    return false;
  }
  std::shared_ptr<leveldb::DB> balance(temp);

  // Сканирование баз и подсчёт баланса.
  if (!initial_scan(main.get(), balance.get())) {
    return false;
  }

  main_base_.swap(main);
  balance_base_.swap(balance);
  return true;
}

void done()
{
  std::lock_guard<std::mutex> guard(init_mutex_);

  main_base_.reset();
  balance_base_.reset();
}

bool SetTransActions(std::string* pool, std::string* previous_pool, const Tran* ta,
                     uint32_t cn_TA, uint64_t Time, uint64_t sequence)
{
  csdb_internal::PoolHeader ph;
  ph.time_ = Time;
  ph.sequence_ = sequence;
  ph.transaction_count_ = cn_TA;

  std::string buffer;
  leveldb::Status status;

// Не проверяем previous_pool - т.к. на момент помещения в пул цепочка
// может быть ещё не связанной
//  if (!is_empty(previous_pool)) {
//    // Check for previous pool really exists - to prevent orphan pools.
//    status = main_base_->Get(leveldb::ReadOptions(), *previous_pool, &buffer);
//    if (status.IsNotFound()) {
//      std::cerr << __func__ << ": Can not find previous pool." << std::endl;
//      return false;
//    }
//    if (!status.ok()) {
//      std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
//      return false;
//    }
//    ph.prev_pool_hash_ = *previous_pool;
//  } else {
//    // Check for data base is empty for pool withour parent.
//    std::shared_ptr<leveldb::Iterator> it(main_base_->NewIterator(leveldb::ReadOptions()));
//    it->SeekToFirst();
//    if (it->Valid()) {
//      std::cerr << __func__ << ": Can not put pool without parent into non-empty base." << std::endl;
//      return false;
//    }
//  }
  if (!is_empty(previous_pool)) {
    ph.prev_pool_hash_ = *previous_pool;
  }

  if (is_empty(pool)) {
    std::cerr << __func__ << ": Can not put pool with emtpy hash." << std::endl;
    return false;
  }
  status = main_base_->Get(leveldb::ReadOptions(), *pool, &buffer);
  if (status.ok()) {
    std::cerr << __func__ << ": Pool with specified hash (" << to_hex(*pool) << ") already exists." << std::endl;
    return false;
  } else if (!status.IsNotFound()) {
    std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
    return false;
  }

  csdb_internal::obstream ob;
  ob << ph;
  if ((nullptr != ta) && (0 < cn_TA)) {
    ob.append(ta, sizeof(Tran) * cn_TA);
  }
  status = main_base_->Put(leveldb::WriteOptions(), *pool, leveldb::Slice(ob));
  if (!status.ok()) {
    std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(last_save_hash_mutex_);
    last_save_hash_ = *pool;
  }

  {
    std::lock_guard<std::mutex> lock(current_head_hash_mutex_);
    if (current_head_hash_ == ph.prev_pool_hash_) {
      current_head_hash_ = *pool;
    }
  }

  {
    std::lock_guard<std::mutex> lock(update_balance_mutex_);
    update_balances(balance_base_.get(), ta, cn_TA);
  }

  return true;
}

bool hasAnyPools()
{
  std::shared_ptr<leveldb::Iterator> it(main_base_->NewIterator(leveldb::ReadOptions()));
  it->SeekToFirst();
  return it->Valid();
}

void GetBalance(struct Balance *bal)
{
  bal->amount = 0;
  bal->amount1 = 0;

  std::string value;
  leveldb::Status status = balance_base_->Get(leveldb::ReadOptions(), bal->A_source, &value);
  if (status.IsNotFound()) {
    return;
  }
  if (!status.ok()) {
    std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
    return;
  }

  account_balance b;
  if (!csdb_internal::ibstream(value.data(), value.size()).get(b)) {
    std::cerr << __func__ << ": Invalid record format in balance database." << std::endl;
    return;
  }

  const auto it = b.find(bal->Currency);
  if (it != b.end()) {
    bal->amount = static_cast<uint32_t>(it->second.i_);
    bal->amount1 = it->second.f_;
  }
}

bool GetPool(std::string* PoolHash, std::string* previous_pool, std::vector<Tran>* ta, time_t* Time,
             uint64_t* sequence)
{
  std::string hash;
  if (nullptr == PoolHash) {
    std::lock_guard<std::mutex> lock(last_save_hash_mutex_);
    hash = last_save_hash_;
  } else {
    hash = *PoolHash;
  }

  if (hash.empty()) {
    std::cerr << __func__ << ": Empty hash passed or no last saved hash found." << std::endl;
    if (nullptr == PoolHash) {
      abort();
    }
    return false;
  }

  std::string value;
  leveldb::Status status = main_base_->Get(leveldb::ReadOptions(), hash, &value);
  if (status.IsNotFound()) {
    return false;
  }
  if (!status.ok()) {
    std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
    return false;
  }

  csdb_internal::ibstream is(value.data(), value.size());
  csdb_internal::PoolHeader ph;
  if (!ph.get(is)) {
    std::cerr << "Invalid pool encountered under hash: " << csdb::to_hex(hash) << std::endl;
    return false;
  }

  size_t tran_count = is.size() / sizeof(csdb::Tran);
  if ((0 != (is.size() % sizeof(csdb::Tran))) || (tran_count != ph.transaction_count_)) {
    std::cerr << "Invalid transactions block in pool under hash: " << csdb::to_hex(hash) << std::endl;
    return false;
  }

  if (nullptr != previous_pool) {
    *previous_pool = ph.prev_pool_hash_;
  }

  if (nullptr != Time) {
    *Time = static_cast<time_t>(ph.time_);
  }

  if (nullptr != sequence) {
    *sequence = ph.sequence_;
  }

  if(nullptr != ta) {
    ta->resize(tran_count);
    memcpy(ta->data(), is.data(), tran_count * sizeof(Tran));
  }

  return true;
}

std::string GetHeadHash()
{
  std::string hash;
  {
    std::lock_guard<std::mutex> lock(current_head_hash_mutex_);
    hash = current_head_hash_;
  }
  return hash;
}

bool GetTransactions(std::vector<std::string>& transaction_ids, const char* addr, size_t limit, size_t offset)
{
  transaction_ids.clear();
  bool has_more = false;

  if (!get_transactions(has_more, addr, limit, offset,
                        [&transaction_ids](const std::string& hash, size_t index, const csdb::Tran&)
    {
      transaction_ids.push_back(csdb::to_hex(hash) + "." + std::to_string(index));
    })) {
    transaction_ids.clear();
  }

  return has_more;
}

bool GetTransactionInfo(Tran& transaction, const std::string& transaction_id)
{
  std::string hash = csdb::from_hex(transaction_id);
  if (hash.empty() || (transaction_id.size() < (hash.size() * 2 + 2)) || ('.' != transaction_id[hash.size() * 2])) {
    return false;
  }
  const char *str = transaction_id.c_str();
  char *end;
  size_t index = static_cast<size_t>(strtoull(str + (hash.size() * 2) + 1, &end, 10));
  if (end != str + transaction_id.size()) {
    return false;
  }
  --index;

  std::string value;
  leveldb::Status status = main_base_->Get(leveldb::ReadOptions(), hash, &value);
  if (status.IsNotFound()) {
    return false;
  }
  if (!status.ok()) {
    std::cerr << __func__ << ": leveldb error: " << status.ToString() << std::endl;
    return false;
  }

  csdb_internal::ibstream is(value.data(), value.size());
  csdb_internal::PoolHeader ph;
  if (!ph.get(is)) {
    std::cerr << "Invalid pool encountered under hash: " << csdb::to_hex(hash) << std::endl;
    return false;
  }

  size_t tran_count = is.size() / sizeof(csdb::Tran);
  if ((0 != (is.size() % sizeof(csdb::Tran))) || (tran_count != ph.transaction_count_)) {
    std::cerr << "Invalid transactions block in pool under hash: " << csdb::to_hex(hash) << std::endl;
    return false;
  }

  if (index > tran_count) {
    return false;
  }

  transaction = static_cast<const csdb::Tran*>(is.data())[index];

  return true;
}

std::string to_hex(const void* data, size_t size)
{
  std::string res(size * 2, '\0');
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i, ++p) {
    res[i * 2] = hex_digit((*p >> 4) & 0x0F);
    res[i * 2 + 1] = hex_digit(*p & 0x0F);
  }
  return res;
}

std::string from_hex(const std::string& str)
{
  std::string res;
  for(size_t i = 0; i < (str.size()/2); i++) {
    uint8_t b1, b2;
    if ((!digit_from_hex(str[i*2], b1)) || (!digit_from_hex(str[i*2 + 1], b2))) {
      break;
    }
    res += static_cast<char>((b1 << 4) | b2);
  }
  return res;
}

constexpr const uint64_t AMOUNT_MAX_FRACTION = 999999999999999999ULL;

void amounts_add(int32_t& id, uint64_t& fd, int32_t is, uint64_t fs)
{
  if (fd > AMOUNT_MAX_FRACTION) {
    fd = AMOUNT_MAX_FRACTION;
  }

  if (fs > AMOUNT_MAX_FRACTION) {
    fs = AMOUNT_MAX_FRACTION;
  }

  fd += fs;
  id += is;
  if (fd > AMOUNT_MAX_FRACTION) {
    fd -= (AMOUNT_MAX_FRACTION + 1);
    id++;
  }
}

void amounts_sub(int32_t& id, uint64_t& fd, int32_t is, uint64_t fs)
{
  if (fd > AMOUNT_MAX_FRACTION) {
    fd = AMOUNT_MAX_FRACTION;
  }

  if (fs > AMOUNT_MAX_FRACTION) {
    fs = AMOUNT_MAX_FRACTION;
  }

  id -= is;
  if (fs > fd) {
    fd += (AMOUNT_MAX_FRACTION + 1);
    id--;
  }
  fd -= fs;
}

std::string amount_to_string(int32_t i, uint64_t f, size_t min_digits)
{
  char buf[32];
  char *end = buf;
  if ((0 > i) && (0 != f)) {
    f = AMOUNT_MAX_FRACTION + 1 - f;
    i = -(i + 1);
    *(end++) = '-';
  }
  end += sprintf_s(end, sizeof(buf) - (end - buf), "%" PRId32, i);

  if (0 != f || (0 < min_digits)) {
    *(end++) = '.';
    char *last = end + sprintf_s(end, sizeof(buf) - (end - buf), "%018" PRIu64, f);
    while ((static_cast<size_t>(last - end) > min_digits) && ('0' == last[-1])) {
      --last;
    }
    end = last;
  }
  return std::string(buf, static_cast<size_t>(end - buf));
}

std::string uuid_to_string(const uuid_t& uuid)
{
  char str[40];
#ifdef _MSC_VER
  RPC_CSTR uuid_str;
  ::UuidToStringA(&uuid, &uuid_str);
  memcpy(str + 1, uuid_str, 36);
  ::RpcStringFree(&uuid_str);
#else
  uuid_unparse(uuid, str + 1);
#endif
  str[0] = '{';
  str[37] = '}';
  str[38] = '\0';
  return str;
}

}

namespace csdb_internal {
void update_heads_and_tails(heads_t& heads, tails_t& tails, const std::string& cur_hash, const std::string& prev_hash)
{
  auto ith = heads.find(prev_hash);
  auto itt = tails.find(cur_hash);
  bool eith = (heads.end() != ith);
  bool eitt = (tails.end() != itt);
  if (eith && eitt) {
    // Склеиваем две подцепочки.
    assert(1 == heads.count(itt->second));
    head_info_t& ith1 = heads[itt->second];
    ith1.next_ = ith->second.next_;
    ith1.len_ += (1 + ith->second.len_);
    if (!ith->second.next_.empty()) {
      /// \todo Проверить, почему выпадает assert!
      // assert(1 == tails.count(ith->second.next_));
      tails[ith->second.next_] = itt->second;
    }
    heads.erase(ith);
    // Мы, возможно, уже изменили tails - поэтому нельзя удалять по итератору!
    tails.erase(cur_hash);
  } else if (eith && (!eitt)) {
    // Добавляем в начало цепочки.
    if (!ith->second.next_.empty()) {
      /// \todo Проверить, почему выпадает assert!
      // assert(1 == tails.count(ith->second.next_));
      tails[ith->second.next_] = cur_hash;
    }
    assert(0 == heads.count(cur_hash));
    heads.emplace(cur_hash, head_info_t{ith->second.len_ + 1, ith->second.next_} );
    heads.erase(prev_hash);
  } else if ((!eith) && eitt) {
    // Добавляем в конец цепочки.
    assert(1 == heads.count(itt->second));
    head_info_t& ith1 = heads[itt->second];
    ith1.next_ = prev_hash;
    ++ith1.len_;
    if (!prev_hash.empty()) {
      // assert не нужен, т.е. наличие такого "хвоста" говорит о пересекающихся или зацикленных
      // цепочках (т.е. уже была цепочка, имеющая этот же хвост).
      // TODO: Доделать детектирование таких цепочек (после создания unit-тестов)
      // assert(0 == tails.count(prev_hash));
      tails.emplace(prev_hash, itt->second);
    }
    tails.erase(cur_hash);
  } else {
    // Ни с чем не пересекаемся! Просто подвешиваем.
    assert(0 == heads.count(cur_hash));
    heads.emplace(cur_hash, head_info_t{1, prev_hash});
    if (!prev_hash.empty()) {
      // см. TODO к пердыдущей ветке.
      // assert(0 == tails.count(prev_hash));
      tails.emplace(prev_hash, cur_hash);
    }
  }
}

} // namespace csdb_internal
