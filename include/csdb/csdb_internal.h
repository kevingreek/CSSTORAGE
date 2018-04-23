/**
 * @file csdb/csdb_internal.h
 * @brief Внутренние классы для csdb
 *
 * Файл содержит внутренни классы и структуры csdb, в основном используемые для сериализации.
 * Эти классы и структуры были вынесены в интерфейс для использования в браузере базы
 * транзакций. Не рекомендуется использовать без особой необходимости.
 */

#pragma once
#ifndef _CREDITS_CSDB_INTERNAL_H_INCLUDED_
#define _CREDITS_CSDB_INTERNAL_H_INCLUDED_

#include <string>
#include <cstring>
#include <map>

namespace csdb_internal {
class obstream
{
public:
  inline obstream& append(const void* data, size_t cb_data)
  {
    data_.append(static_cast<const char*>(data), cb_data);
    return *this;
  }

  inline operator std::string&() { return data_; }
  inline operator const std::string& () const { return data_; }
  inline const void* data() const {return data_.data();}
  inline size_t size() const {return data_.size();}

  template<typename T>
  typename std::enable_if<std::is_trivially_copyable<T>::value, obstream&>::type
  inline operator <<(const T& value)
  {
    append(&value, sizeof(T));
    return *this;
  }

  template<typename T>
  inline obstream& operator <<(const std::map<std::string,T>& value)
  {
    uint16_t sz = static_cast<uint16_t>(value.size());
    append(&sz, sizeof(sz));
    for (const auto it : value) {
      operator << (it.first);
      operator << (it.second);
    }
    return *this;
  }

  inline obstream& operator <<(const std::string& value)
  {
    uint16_t sz = static_cast<uint16_t>(value.size());
    append(&sz, sizeof(sz));
    data_.append(value);
    return *this;
  }
private:
  std::string data_;
};

class ibstream
{
public:
  inline ibstream(const void* data, size_t size) :
    data_(static_cast<const char*>(data)), size_(size) {}

  inline const void* data() const {return data_;}
  inline size_t size() const {return size_;}

  template <typename T>
  typename std::enable_if<std::is_trivially_copyable<T>::value, bool>::type
  inline get(T& value)
  {
    if (sizeof(T) > size_) {
      return false;
    }
    memcpy(&value, data_, sizeof(T));
    data_ += sizeof(T);
    size_ -= sizeof(T);
    return true;
  }

  template<typename T>
  inline bool get(std::map<std::string, T>& value)
  {
    uint16_t sz;
    if ((!get(sz)) ||(sz > size_)) {
      return false;
    }
    value.clear();
    for (uint16_t i = 0; i < sz; i++) {
      std::string key;
      if (!get(key)) {
        return false;
      }
      if (!get(value[key])) {
        return false;
      }
    }
    return true;
  }

  inline bool get(std::string& value)
  {
    uint16_t sz;
    if ((!get(sz)) ||(sz > size_)) {
      return false;
    }

    value.assign(data_, sz);
    data_ += sz;
    size_ -= sz;
    return true;
  }

private:
  const char* data_;
  size_t size_;
};

class PoolHeader
{
public:
  bool get(ibstream& ib)
  {
    return ib.get(prev_pool_hash_) && ib.get(time_) && ib.get(sequence_) && ib.get(transaction_count_);
  }
public:
  std::string prev_pool_hash_;
  uint64_t time_;
  uint64_t sequence_;
  uint64_t transaction_count_;
};

inline obstream& operator <<(obstream& ob, const PoolHeader& ph)
{
  ob << ph.prev_pool_hash_ << ph.time_ << ph.sequence_ << ph.transaction_count_;
  return ob;
}

struct head_info_t {
  size_t len_;        // Количество блоков в цепочке
  std::string next_;  // хеш следующего пула, или пустая строка для первого пула
                      // в цепочее (нет родителя, начало цепочки).
};
typedef std::map<std::string, head_info_t> heads_t;
typedef std::map<std::string, std::string> tails_t;

void update_heads_and_tails(heads_t& heads, tails_t& tails, const std::string& cur_hash, const std::string& prev_hash);

}

#endif // _CREDITS_CSDB_INTERNAL_H_INCLUDED_
