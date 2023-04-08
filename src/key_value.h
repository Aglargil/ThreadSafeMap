#pragma once

#include <chrono>
#include <memory>


using TimeStamp = std::chrono::system_clock::time_point;

template<typename K, typename V>
class KeyValue {

    using KeyValueSharedPtr = std::shared_ptr<KeyValue<K, V>>;
    using SystemClock = std::chrono::system_clock;

public:
    KeyValue();

    KeyValue(const K& key, const V& value, int expire_time_interval = -1)
        : _key(key)
        , _value(value)
        , _insert_time(SystemClock::now())
        , _expire_time(_insert_time + std::chrono::milliseconds(expire_time_interval))
        , _expire_time_interval(expire_time_interval) // 传入expire_time_interval计算过期时间
        , _is_delete(false) {}

    KeyValue(const K& key, const V& value, const TimeStamp& expire_time)
        : _key(key)
        , _value(value)
        , _insert_time(SystemClock::now())
        , _expire_time(expire_time)
        , _expire_time_interval(0) // 0表示会过期, 过期时间为expire_time
        , _is_delete(false) {}

    static KeyValueSharedPtr create(const K& key, const V& value, int expire_time_interval = -1) {
        return std::make_shared<KeyValue<K, V>>(key, value, expire_time_interval);
    }

    static KeyValueSharedPtr create(const K& key, const V& value, const TimeStamp& expire_time) {
        return std::make_shared<KeyValue<K, V>>(key, value, expire_time);
    }
    /*
        * @brief 更新插入时间为当前时间
    */
    void update_insert_time() {
        _insert_time = std::chrono::system_clock::now();
    }

    /*
        * @brief 更新过期时间
        * @param expire_time_interval 过期时间, 单位ms, -1表示永不过期
    */
    void update_expire_time(int expire_time_interval) {
        _expire_time_interval = expire_time_interval;
        _expire_time = std::chrono::system_clock::now() + std::chrono::milliseconds(_expire_time_interval);
    }
    
    /*
        * @brief 判断是否过期
        * @return 过期返回true, 否则返回false
    */
    bool is_expire() {
        if (_is_delete) {
            return true;
        }

        if (_expire_time_interval == -1) {
            return false;
        }
        return std::chrono::system_clock::now() > _expire_time;
    }

    void delete_value() {
        _is_delete = true;
    }

    const V& get_value() const {
        return _value;
    }

    const K& get_key() const {
        return _key;
    }

    const TimeStamp& get_insert_time() const {
        return _insert_time;
    }

    const TimeStamp& get_expire_time() const {
        return _expire_time;
    }

    const int& get_expire_time_interval() const {
        return _expire_time_interval;
    }
private:
    K _key;
    V _value;
    TimeStamp _insert_time;
    TimeStamp _expire_time;
    int _expire_time_interval;
    bool _is_delete;
};