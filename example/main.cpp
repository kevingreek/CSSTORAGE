#include "csdb/csdb.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <ctime>
#include <cassert>

#ifdef _MSC_VER
# include <direct.h>
# include <Shlobj.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#define strcpy_s(d,s) strcpy(d,s)
#define strncpy_s(d,s,c) strncpy(d,s,c)
#endif

#define TEST_PRESCAN_CORRECT_CHAIN_STRAIT   0
#define TEST_PRESCAN_CORRECT_CHAIN_REVERSE  0
#define TEST_PRESCAN_CORRECT_CHAIN_MIXED_3  0
#define TEST_PRESCAN_CORRECT_CHAIN_MIXED_5  0

#define PRESCAN_TEST (\
  TEST_PRESCAN_CORRECT_CHAIN_STRAIT || \
  TEST_PRESCAN_CORRECT_CHAIN_REVERSE || \
  TEST_PRESCAN_CORRECT_CHAIN_MIXED_3 || \
  TEST_PRESCAN_CORRECT_CHAIN_MIXED_5 || \
  0)

namespace {

struct amount
{
  int32_t i_;
  uint64_t f_;
};
amount get_balance(const char* addr, const char* currency);
void display_balances();
void display_pool(std::string* hash);
void display_transactions_lists();

void some_unit_tests();

#if TEST_PRESCAN_CORRECT_CHAIN_STRAIT
void prescan_correct_chain_strait();
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_REVERSE
void prescan_correct_chain_reverse();
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_MIXED_3
void prescan_correct_chain_mixed_3();
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_MIXED_5
void prescan_correct_chain_mixed_5();
#endif

std::string get_next_hash(bool init_from_head = false);
void next_hash(std::string& curh, std::string& prevh);

void save_transctions_test_1(std::string& curh, std::string& prevh);
void save_transctions_test_2(std::string& curh, std::string& prevh);
}

int main(int /*argc*/, char* /*argv*/[])
{
  // Простая проверка некоторых функций - некоторая замена unit test-ам
  some_unit_tests();

#ifdef _MSC_VER
  char user_folder[MAX_PATH];
  HRESULT hresult= ::SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT | KF_FLAG_CREATE, user_folder);
  assert(S_OK == hresult);
  size_t path_len = strlen(user_folder);
  assert(path_len > 1);
  char end = user_folder[path_len - 1];
  if (('\\' != end) && ('/' != end)) {
    user_folder[path_len++] = '/';
  }
  strcpy_s(&(user_folder[path_len]), MAX_PATH - path_len, "csdb_example");
  _mkdir(user_folder);
#else
  std::string user_folder_str = getenv("HOME");
  char end = user_folder_str[user_folder_str.size() - 1];
  if (('\\' != end) && ('/' != end)) {
    user_folder_str += '/';
  }
  user_folder_str += ".appdata";
  if (0 != access(user_folder_str.c_str(), F_OK)) {
    int res = mkdir(user_folder_str.c_str(), 0755);
    assert(0 == res);
  }
  user_folder_str += "/csdb_example";
  if (0 != access(user_folder_str.c_str(), F_OK)) {
    int res = mkdir(user_folder_str.c_str(), 0755);
    assert(0 == res);
  }
  const char* user_folder = user_folder_str.c_str();
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_STRAIT
  prescan_correct_chain_strait();
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_REVERSE
  prescan_correct_chain_reverse();
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_MIXED_3
  prescan_correct_chain_mixed_3();
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_MIXED_5
  prescan_correct_chain_mixed_5();
#endif

#if !PRESCAN_TEST
  std::cout << "Initializing database engine..." << std::endl;
  if (!csdb::init(user_folder)) {
    std::cout << "Initialization of database engine FAILED!" << std::endl;
    return 1;
  }
  std::cout << "Database engine successfully initialized." << std::endl;

  display_transactions_lists();

  std::cout << "Current head hash: " << csdb::to_hex(csdb::GetHeadHash()) << std::endl;

  uint64_t seq = 1;
  // Поищем первый, ещё не записанный, хэш
  std::string curh(get_next_hash(true)), prevh(csdb::GetHeadHash());
  while (true) {
    if (csdb::SetTransActions(&curh, &prevh, nullptr, 0, time(NULL), seq++)) {
      break;
    }
    next_hash(curh, prevh);
  }
  std::cout << "Successfully written empty pool with hash: " << csdb::to_hex(curh) << std::endl;
  if (!prevh.empty()) {
    display_pool(&prevh);
  }
  next_hash(curh, prevh);
  display_pool(nullptr);
  std::cout << "Current head hash: " << csdb::to_hex(csdb::GetHeadHash()) << std::endl;

  std::cout << "Balances before save_transctions_test:" << std::endl;
  display_balances();

  save_transctions_test_1(curh, prevh);
  display_pool(&prevh);
  display_pool(nullptr);
  std::cout << "Current head hash: " << csdb::to_hex(csdb::GetHeadHash()) << std::endl;
  std::cout << "Balances after save_transctions_test_1:" << std::endl;
  display_balances();

  save_transctions_test_2(curh, prevh);
  display_pool(&prevh);
  display_pool(nullptr);
  std::cout << "Current head hash: " << csdb::to_hex(csdb::GetHeadHash()) << std::endl;
  std::cout << "Balances after save_transctions_test_2:" << std::endl;
  display_balances();

  std::string hash("HASH");
  display_pool(&hash);

  display_transactions_lists();

  std::cout << "Shutting down database engine..." << std::endl;
  csdb::done();
  std::cout << "Database engine shutted down." << std::endl;
#endif
  return 0;
}

namespace {

template<size_t size>
inline void init_trans(csdb::Tran (&t)[size])
{
  memset(t, 0, sizeof(csdb::Tran) * size);
  for(size_t i = 0; i < size; i++) {
#ifdef _MSC_VER
    ::UuidCreate(&(t[i].InnerID));
#else
    uuid_generate(t[i].InnerID);
#endif
  }
}

void save_transctions_test_1(std::string& curh, std::string& prevh)
{
  csdb::Tran t[2];
  init_trans(t);

  strcpy_s(t[0].A_source, "Client 1");
  strcpy_s(t[0].A_target, "Client 2");
  strcpy_s(t[0].Currency, "CS");
  t[0].Amount = 100;
  t[0].Amount1 = 10000000000000000UL;   // 0.01

  strcpy_s(t[1].A_source, "Client 2");
  strcpy_s(t[1].A_target, "Client 1");
  strcpy_s(t[1].Currency, "CS");
  t[1].Amount = 50;
  t[1].Amount1 = 20000000000000000UL;   // 0.01
  if (!csdb::SetTransActions(&curh, &prevh, t, 2, time(NULL), 1)) {
    return;
  }
  std::cout << "Successfully written pool (2 transactions) with hash: " << csdb::to_hex(curh) << std::endl;
  next_hash(curh, prevh);
}

void save_transctions_test_2(std::string& curh, std::string& prevh)
{
  csdb::Tran t[4];
  init_trans(t);

  strcpy_s(t[0].A_source, "Milking cow 1");
  strcpy_s(t[0].A_target, "Client 1");
  strcpy_s(t[0].Currency, "RUB");
  t[0].Amount = 100000;
  t[0].Amount1 = 0UL;

  strcpy_s(t[1].A_source, "Milking cow 1");
  strcpy_s(t[1].A_target, "Fee Accumulator");
  strcpy_s(t[1].Currency, "CS");
  t[1].Amount = 0;
  t[1].Amount1 = 5000000000000000UL;   // 0.005

  strcpy_s(t[2].A_source, "Milking cow 2");
  strcpy_s(t[2].A_target, "Client 1");
  strcpy_s(t[2].Currency, "USD");
  t[2].Amount = 100;
  t[2].Amount1 = 0UL;

  strcpy_s(t[3].A_source, "Milking cow 2");
  strcpy_s(t[3].A_target, "Fee Accumulator");
  strcpy_s(t[3].Currency, "CS");
  t[3].Amount = 0;
  t[3].Amount1 = 5000000000000000UL;   // 0.005

  if (!csdb::SetTransActions(&curh, &prevh, t, 4, time(NULL), 2)) {
    return;
  }
  std::cout << "Successfully written pool (4 transactions) with hash: " << csdb::to_hex(curh) << std::endl;
  next_hash(curh, prevh);
}

void some_unit_tests()
{
  // to_hex
  uint64_t val = 0x1234567890ABCDEFULL;
  assert(csdb::to_hex(&val, sizeof(uint64_t)) == "EFCDAB9078563412");
  assert(csdb::to_hex(&val, sizeof(uint32_t)) == "EFCDAB90");
  assert(csdb::to_hex(&val, sizeof(uint16_t)) == "EFCD");
  assert(csdb::to_hex(&val, sizeof(uint8_t)) == "EF");
  const void *pval = &val;
  const char *sval = static_cast<const char*>(pval);
  assert(csdb::to_hex(std::string(sval, sizeof(uint64_t))) == "EFCDAB9078563412");
  assert(csdb::to_hex(std::string(sval, sizeof(uint32_t))) == "EFCDAB90");
  assert(csdb::to_hex(std::string(sval, sizeof(uint16_t))) == "EFCD");
  assert(csdb::to_hex(std::string(sval, sizeof(uint8_t))) == "EF");

  // from_hex
  assert(csdb::from_hex("414A") == "AJ");
  assert(csdb::from_hex("41.4A") == "A");
  assert(csdb::from_hex("414.A") == "A");
  assert(csdb::from_hex("414a") == "AJ");

  // amount_to_string
  int32_t i = 0;
  uint64_t f = 0;
  assert(csdb::amount_to_string(i, f) == "0");
  assert(csdb::amount_to_string(i, f, 1) == "0.0");
  assert(csdb::amount_to_string(i, f, 2) == "0.00");
  i = 2;
  f = 100;
  assert(csdb::amount_to_string(i, f) == "2.0000000000000001");
  f = 10000;
  assert(csdb::amount_to_string(i, f) == "2.00000000000001");
  f = 10000000000000000ULL;   // 0.01
  assert(csdb::amount_to_string(i, f) == "2.01");

  i = -2;
  assert(csdb::amount_to_string(i, f) == "-1.99");

  // amount operations
  i = 0;
  f = 0;
  csdb::amounts_add(i, f, 0, 500000000000000000ULL);
  assert(csdb::amount_to_string(i, f) == "0.5");
  csdb::amounts_add(i, f, 0, 800000000000000000ULL);
  assert(csdb::amount_to_string(i, f) == "1.3");

  csdb::amounts_sub(i, f, 0, 500000000000000000ULL);
  assert(csdb::amount_to_string(i, f) == "0.8");

  csdb::amounts_sub(i, f, 1, 500000000000000000ULL);
  assert(csdb::amount_to_string(i, f) == "-0.7");
  assert(i == -1);

  csdb::amounts_sub(i, f, 1, 500000000000000000ULL);
  assert(csdb::amount_to_string(i, f) == "-2.2");
  assert(i == -3);

  // uuid_to_string
  uuid_t uuid;
  memset(&uuid, 0, sizeof(uuid));
  assert(csdb::uuid_to_string(uuid) == "{00000000-0000-0000-0000-000000000000}");
}

std::string get_next_hash(bool init_from_head)
{
  static uint64_t hash_count = 0;
  if (init_from_head) {
    std::string head = csdb::GetHeadHash();
    if (!head.empty()) {
      assert(head.size() >= sizeof(hash_count));
      memcpy(&hash_count, head.data(), sizeof(hash_count));
    }
  }
  ++hash_count;
  const void *phash = &hash_count;
  return std::string(static_cast<const char*>(phash), sizeof(hash_count));
}

void next_hash(std::string& curh, std::string& prevh)
{
  prevh = curh;
  curh = get_next_hash();
}

amount get_balance(const char* addr, const char* currency)
{
  csdb::Balance b;
  memset(&b, 0, sizeof(b));
  strncpy_s(b.A_source, addr, sizeof(b.A_source) - 1);
  strncpy_s(b.Currency, currency, sizeof(b.Currency) - 1);
  csdb::GetBalance(&b);
  return amount{static_cast<int32_t>(b.amount), b.amount1};
}

inline std::string amount_to_string(const amount& a)
{
  return csdb::amount_to_string(a.i_, a.f_);
}

void display_balance(const char* address)
{
  std::cout << "  " << address << ":" << std::endl;
  std::cout << "    CS:  " << amount_to_string(get_balance(address, "CS")) << std::endl;
  std::cout << "    RUB: " << amount_to_string(get_balance(address, "RUB")) << std::endl;
  std::cout << "    USD: " << amount_to_string(get_balance(address, "USD")) << std::endl;
}

void display_transaction(const csdb::Tran& t)
{
  std::cout << csdb::amount_to_string(static_cast<int32_t>(t.Amount), t.Amount1)
            << " " << t.Currency << " ("
            << t.A_source << " ==> " << t.A_target << ")" << std::endl;
}

void display_transactions(const std::vector<csdb::Tran>& transactions)
{
  for (size_t i = 0; i < transactions.size(); ++i) {
    std::cout << std::setw(7) << i << ": ";
    display_transaction(transactions[i]);
  }
}

void display_transactions_list(const char* addr)
{
  std::vector<std::string> ids;
  bool result = csdb::GetTransactions(ids, addr, 999, 0);
  if (ids.empty()) {
    std::cout << "There are no transactions on account " << addr << std::endl;
    return;
  }

  std::cout << "Last " << ids.size() << " transactions on account " << addr << ":" << std::endl;
  for (size_t i = 0; i < ids.size(); i++) {
    std::cout << std::setw(5) << i << ": "
              << std::setw(21) << std::left << ids[i] << std::internal << ": ";
    csdb::Tran transaction;
    if (csdb::GetTransactionInfo(transaction, ids[i])) {
      display_transaction(transaction);
    } else {
      std::cout << "Error retrieving transaction details." << std::endl;
    }
  }
  if (result) {
    std::cout << "  There are more earlier transactions on this account. " << std::endl;
  }
}

void display_transactions_lists()
{
  display_transactions_list("Client 1");
  display_transactions_list("Client 2");
  display_transactions_list("Fee Accumulator");
}
void display_balances()
{
  display_balance("Client 1");
  display_balance("Client 2");
  display_balance("Fee Accumulator");
}

void display_pool(std::string* hash)
{
  if (nullptr == hash) {
    std::cout << "Trying to get last written pool..." << std::endl;
  } else {
    std::cout << "Trying to get pool by hash:" << csdb::to_hex(*hash) << "..." << std::endl;
  }
  std::vector<csdb::Tran> transactions;
  std::string prev_hash;
  time_t pool_time;
  if (!csdb::GetPool(hash, &prev_hash, &transactions, &pool_time, nullptr)) {
    std::cout << "...GetPool failed!" << std::endl;
    return;
  }

  std::cout << "  Pool retrieved: {Previous hash: " << csdb::to_hex(prev_hash)
            << "; Transaction count: " << transactions.size() << ";}";
  if (0 < transactions.size()) {
    std::cout << "; Transactions:";
  }
  std::cout << std::endl;
  display_transactions(transactions);
}

#if TEST_PRESCAN_CORRECT_CHAIN_STRAIT
void prescan_correct_chain_strait()
{
  std::cout << __func__ << "() executed." << std::endl;
  if (!csdb::init()) {
    std::cerr << __func__ << ": first init failed." << std::endl;
    return;
  }

  std::string hc, hp;

  // 01 -> 02 -> 03
  hc = "01";
  hp = "02";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }
  hc = "02";
  hp = "03";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  hc = "03";
  hp = "";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  csdb::done();

  if (!csdb::init()) {
    std::cerr << __func__ << ": second init failed." << std::endl;
    return;
  }
  csdb::done();

  std::cout << __func__ << "() successfully finished." << std::endl;
}
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_REVERSE
void prescan_correct_chain_reverse()
{
  std::cout << __func__ << "() executed." << std::endl;
  if (!csdb::init()) {
    std::cerr << __func__ << ": first init failed." << std::endl;
    return;
  }

  std::string hc, hp;

  // 03 -> 02 -> 01
  hc = "03";
  hp = "02";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }
  hc = "02";
  hp = "01";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  hc = "01";
  hp = "";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  csdb::done();

  if (!csdb::init()) {
    std::cerr << __func__ << ": second init failed." << std::endl;
    return;
  }
  csdb::done();

  std::cout << __func__ << "() successfully finished." << std::endl;
}
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_MIXED_3
void prescan_correct_chain_mixed_3()
{
  std::cout << __func__ << "() executed." << std::endl;
  if (!csdb::init()) {
    std::cerr << __func__ << ": first init failed." << std::endl;
    return;
  }

  std::string hc, hp;

  // 02 -> 03 -> 01
  hc = "02";
  hp = "03";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }
  hc = "03";
  hp = "01";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  hc = "01";
  hp = "";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  csdb::done();

  if (!csdb::init()) {
    std::cerr << __func__ << ": second init failed." << std::endl;
    return;
  }
  csdb::done();

  std::cout << __func__ << "() successfully finished." << std::endl;
}
#endif

#if TEST_PRESCAN_CORRECT_CHAIN_MIXED_5
void prescan_correct_chain_mixed_5()
{
  std::cout << __func__ << "() executed." << std::endl;
  if (!csdb::init()) {
    std::cerr << __func__ << ": first init failed." << std::endl;
    return;
  }

  std::string hc, hp;

  // 04 -> 03 -> 05 -> 02 -> 01
  hc = "04";
  hp = "03";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }
  hc = "03";
  hp = "05";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  hc = "05";
  hp = "02";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  hc = "02";
  hp = "01";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  hc = "01";
  hp = "";
  if (!csdb::SetTransActions(&hc, &hp, nullptr, 0, 0)) {
    std::cerr << __func__ << ": SetTransActions failed." << std::endl;
    return;
  }

  csdb::done();

  if (!csdb::init()) {
    std::cerr << __func__ << ": second init failed." << std::endl;
    return;
  }
  csdb::done();

  std::cout << __func__ << "() successfully finished." << std::endl;
}
#endif

}
