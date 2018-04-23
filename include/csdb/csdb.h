#pragma once
#ifndef _CREDITS_CSDB_H_INCLUDED_
#define _CREDITS_CSDB_H_INCLUDED_

#include <cinttypes>
#include <string>
#include <vector>

#ifdef _MSC_VER
// Для использования UUID
# include <Rpc.h>
#else
# include <uuid/uuid.h>
#endif

namespace csdb {

/**
 * @brief Инициализация библиотеки работы с базами данных.
 * @param[in] path_to_database Путь к папке, где размещаются базы данных. Если передан nullptr,
 *                             то используется папка по умолчанию (зависит от платформы).
 *                             Завершающий '/' (или '\') не обязателен.
 * @return true, если открытие (или создание) баз и предварительно сканирование
 *         прошли успешно.
 *
 * Функция открывает (или создаёт, если они не существуют) необходимые базы leveldb,
 * и осуществляет их предварительное сканирование, если они существовали. На этапе
 * сканирования проверяется логическая связанность баз и подсчитываются балансы.
 *
 * В случае ошибок информация об ошибках выводится на stderr.
 *
 * Функция должна вызываться один раз для процесса. Повторный вызов допустим только
 * после вызова \ref done().
 *
 * \warning Вызов может занять длительное время, т.к. во время него производится
 * предварительное сканирование баз и подсчёт балансов.
 */
bool init(const char* path_to_database = nullptr);

/**
 * @brief Завершение работы библиотеки
 *
 * Сбрасываются все буфера и кэши, удаляются временные объекты и завершаются рабочие потоки.
 *
 * После вызова это функции функции \ref init() может быть вызвана снова.
 */
void done();

#ifdef _MSC_VER
  typedef UUID uuid_t;
#endif

static_assert(16 == sizeof(uuid_t), "uuid_t MUST have size of 16 bytes.");

#define MAX_STR 256
#pragma pack(push, 8)
typedef struct Transaction {
  uint64_t Hash;
  uuid_t InnerID;
  char A_source[MAX_STR];
  char A_target[MAX_STR];
  uint32_t Amount;
  uint64_t Amount1;
  char Currency[MAX_STR];
} Tran;
#pragma pack(pop)
static_assert(808 == sizeof(csdb::Tran),
              "csdn::Tran MUST have strict size of 808 bytes!");

bool SetTransActions(std::string* pool, std::string* previous_pool,const Tran* ta,
                     uint32_t cn_TA, uint64_t Time, uint64_t sequence);

#pragma pack(push, 8)
struct Balance {
  char A_source[MAX_STR];
  char Currency[MAX_STR];
  uint32_t amount;
  uint64_t amount1;
};
#pragma pack(pop)
static_assert(528 == sizeof(csdb::Balance),
              "csdn::Balance MUST have strict size of 528 bytes!");

void GetBalance(struct Balance *bal);

/**
 * @brief Проверка наличия пулов в базе транзакций
 * @return true, если в базе транзакций есть хоть один пул (вне зависмости от
 *         того, обазуют пулы цепочку или нет), и false, если база транзакций
 *         полностью пустая.
 */
bool hasAnyPools();

bool GetPool(std::string* PoolHash, std::string* previous_pool, std::vector<Tran>* ta, time_t* Time,
             uint64_t* sequence);

/**
 * @brief Хэш головного пула в цепочке.
 * @return Возвращает хэш головного пула текущей цепочки, или пустую строку,
 *         если текущая цепочка пуста.
 */
std::string GetHeadHash();

/**
 * @brief Возвращает список идентификатров транзакций для указанного адреса.
 * @param[out] transaction_ids  Список, куда поместить идентификаторы транзакций.
 * @param[in]  addr             Адрес кошелька, для которого сформировать список транзакций.
 * @param[in]  limit            Максимальное количество транзакций в списке.
 * @param[in]  offset           Номер первой транзакции, которая будет помещена в список.
 * @return true, если имеются ещё транзакции для этого адреса.
 *
 * Функция строит список транзакций для указанного адреса кошелька в порядке, обратном их
 * записи в цепочку, т.е. первой в списке будет транзакция, помещённая последней. Транзакции
 * в списке нумеруются с 0, т.е. чтобы получить первые N транзакций, нужно указать offset = 0.
 *
 * Функция может уменьшить параметр limit, если он будет слишком большой, и операция извлечения
 * транзакция займёт много времени. Реальное ограничение на limit не специфицировано, и может
 * меняться в зависимости от реализации, но не может быть меньше 16.
 *
 * Реальное количество возврашённых транзакций определяется размером вектора \ref transaction_ids.
 * В случае, если транзакция по заданному адресу и диапазону не найдено, функция возвращает
 * false, а вектор \ref transaction_ids очищается.
 *
 * Функция возвращает список уникальных идентификаторов транзакций. Для получени конкретных данных
 * по транзакции нужно вызвать функцию \ref GetTransactionInfo.
 *
 * Идентификатор транзакции представляет собой, вообще говоря, произвольную строку. Однако в
 * текущей реализации - это hex-предствления хэша пула, за которым следуюет знак '.' и строковое
 * представление порядкогово номера транзакции в этом пуле.
 *
 * \warning Поскольку построение списка транзакций возможно только путём последовательного
 * сканирования всей цепочки, выполнение функции может быть достаточно длительным даже для
 * небольшого списка транзакций, особенно если эти транзакции были проведены "давно".
 */
bool GetTransactions(std::vector<std::string>& transaction_ids, const char* addr, size_t limit, size_t offset);

/**
 * @brief Содержимое транзации
 * @param[out] transaction    Буфер, куда будут записаны данные о транзакции.
 * @param[in]  transaction_id Идентификатор транзакции (см. описание к \ref GetTransactions)
 * @return true, если данные по транзакции помещены в буфер, и false, если транзакция
 *         не найдена. В последнем случае буфер остаётся без изменения.
 *
 * Сложность выполнения функции - O(log(N)), где N - общее количество пулов в цепочке. Т.е.
 * функиця выполняется очень быстро.
 */
bool GetTransactionInfo(Tran& transaction, const std::string& transaction_id);

// Дополнительные полезные функции
std::string to_hex(const void* data, size_t size);
inline std::string to_hex(const std::string data)
  { return to_hex(data.data(), data.size()); }
std::string from_hex(const std::string& str);
void amounts_add(int32_t& id, uint64_t& fd, int32_t is, uint64_t fs);
void amounts_sub(int32_t& id, uint64_t& fd, int32_t is, uint64_t fs);
std::string amount_to_string(int32_t i, uint64_t f, size_t min_digits = 0);
std::string uuid_to_string(const uuid_t& uuid);

} //namespace csdb

#endif // _CREDITS_CSDB_H_INCLUDED_
