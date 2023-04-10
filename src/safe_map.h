#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <utility>
#include <thread>

#include "key_value.h"

const int kCheckAllTimes = 100; // 间隔多少次全量检查一次过期数据
const int kDefaultCheckInterval = 5; // 默认检查间隔, 单位ms

using TimeStamp = std::chrono::system_clock::time_point;

template<typename K, typename V>
class SafeMap {
    using SystemClock = std::chrono::system_clock;
    using KeyValueSharedPtr = std::shared_ptr<KeyValue<K, V>>;
public:
    SafeMap() : _is_running(true) {
        _tick_thread = std::thread([this]{loop_tick();});
    }

    SafeMap(std::initializer_list<std::pair<K, V>> key_value_list) : SafeMap() {
        for (auto& item : key_value_list) {
            insert(item.first, item.second);
        }
    }

    ~SafeMap() {
        std::cout << "SafeMap destructor" << std::endl;
        _is_running = false;
        
        decltype(_data_map) temp_map;
        decltype(_min_expire_heap) temp_heap;
        decltype(_queue) temp_queue;

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _data_map.swap(temp_map);
            _min_expire_heap.swap(temp_heap);
            _queue.swap(temp_queue);
        }
        
        _tick_thread.detach();
    }

    /*
        * @brief 线程安全插入
        * @param key 键
        * @param value 值
        * @param expire_time_interval 过期时间, 单位ms, -1表示永不过期
        * @return 插入成功返回true, 否则返回false
    */
    bool insert(const K& key, const V& value, int expire_time_interval = -1) {
        auto map_value = KeyValue<K, V>::create(key, value, expire_time_interval);

        std::lock_guard<std::mutex> lock(_mutex);

        return insert_without_lock(key, map_value);
    }

    /*
        * @brief 线程安全删除
        * @param key 键
        * @return 删除成功返回true, 否则返回false
    */
    bool erase_by_key(const K& key) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        return erase_without_lock(key);
    }

    /*
        * @brief 删除某个时间段内的数据
        * @param start_time 开始时间
        * @param end_time 结束时间
        * @return 删除的数据个数
    */
    int erase_by_time_range(const TimeStamp& start_time, const TimeStamp& end_time) {
        if (start_time > end_time) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(_mutex);

        auto pair = get_range(_queue, start_time, end_time);

        int erase_count = 0;
        for (auto it = pair.first; it != pair.second; ++it) {
            (*it)->delete_value();
            if (erase_without_lock((*it)->get_key())) {
                ++erase_count;
            }
        }

        return erase_count;
    }

    /*
        * @brief 删除insert_time最大或最小前的N条数据
        * @param n N
        * @param asc 是否按照插入时间升序排列
        * @return 删除的数据个数
    */
    int erase_by_order(int n, bool asc = true) {
        std::vector<KeyValue<K, V>> result;

        std::lock_guard<std::mutex> lock(_mutex);

        int count = 0;
        auto lambda = [&count, n, this](KeyValueSharedPtr map_value) {
            if (!map_value->is_expire()) {
                map_value->delete_value();
                if (erase_without_lock(map_value->get_key())) {
                    ++count;
                }
                if (count >= n) {
                    return false;
                }
            }
            return true;
        };

        if (asc) {
            for (auto it = _queue.begin(); it != _queue.end(); ++it) {
                if (!lambda(*it)) {
                    break;
                }
            }
        } else {
            for (auto it = _queue.rbegin(); it != _queue.rend(); ++it) {
                if (!lambda(*it)) {
                    break;
                }
            }
        }

        return count;
    }

    /*
        * @brief 线程安全更新
        * @param key 键
        * @param value 值
        * @param expire_time_interval 过期时间, 单位ms, -1表示永不过期, 0表示不更新过期时间
        * @return 更新成功返回true, 否则返回false
    */
    bool update_value(const K& key, const V& value, int expire_time_interval = 0) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_data_map.count(key) == 0) {
            return false;
        } else {
            // 先删除旧的数据, 再插入新的数据
            auto old_map_value = _data_map[key];
            decltype(old_map_value) map_value;
            if (expire_time_interval == 0) {
                if (old_map_value->get_expire_time_interval() == -1) {
                    map_value = KeyValue<K, V>::create(key, value, -1);
                } else {
                    map_value = KeyValue<K, V>::create(key, value, old_map_value->get_expire_time_interval());
                }
            } else {
                map_value = KeyValue<K, V>::create(key, value, expire_time_interval);
            }
            old_map_value->delete_value();
            erase_without_lock(old_map_value->get_key());
            insert_without_lock(key, map_value);
            return true;
        }
    }

    /*
        * @brief 线程安全获取
        * @param key 键
        * @param value 值
        * @return 获取成功返回true, 否则返回false
    */
    bool get_by_key(const K& key, V& value) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_data_map.count(key) == 0) {
            return false;
        } else {
            if (_data_map[key]->is_expire()) {
                _data_map.erase(key);
                return false;
            }
            value = _data_map[key]->get_value();
            return true;
        }
    }

    /*
        * @brief 获取某个时间范围内的数据
        * @param start_time 起始时间
        * @param end_time 结束时间
        * @param asc 是否按照插入时间升序排列
        * @return 返回某个时间范围内的数据
    */
    std::vector<KeyValue<K, V>> get_by_time_range(const TimeStamp& start_time, const TimeStamp& end_time, bool asc = true) {
        if (start_time > end_time) {
            return {};
        }

        std::vector<KeyValue<K, V>> result;
        decltype(_queue) temp_queue;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            temp_queue = _queue;
        }

        auto pair = get_range(temp_queue, start_time, end_time);

        for (auto it = pair.first; it != pair.second; ++it) {
            if (!(*it)->is_expire()) {
                result.push_back(*(*it));
            }
        }

        return result;
    }

    /*
        * @brief 获取insert_time最大或最小前的N条数据
        * @param n N
        * @param asc 是否按照插入时间升序排列
        * @return 返回N条数据
    */
    std::vector<KeyValue<K, V>> get_by_order(int n, bool asc = true) {
        std::vector<KeyValue<K, V>> result;
        decltype(_queue) temp_queue;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            temp_queue = _queue;
        }

        int count = 0;
        auto lambda = [&result, &count, n](KeyValueSharedPtr map_value) {
            if (!map_value->is_expire()) {
                result.push_back(*map_value);
                ++count;
                if (count >= n) {
                    return false;
                }
            }
            return true;
        };

        if (asc) {
            for (auto it = temp_queue.begin(); it != temp_queue.end(); ++it) {
                if (!lambda(*it)) {
                    break;
                }
            }
        } else {
            for (auto it = temp_queue.rbegin(); it != temp_queue.rend(); ++it) {
                if (!lambda(*it)) {
                    break;
                }
            }
        }

        return result;
    }

private:
    /*
        * @brief 不加锁插入
        * @param key 键
        * @param map_value 值
        * @return 插入成功返回true, 否则返回false
    */
    bool insert_without_lock(const K& key, KeyValueSharedPtr map_value) {
        if (_data_map.count(key) > 0) {
            return false;
        }
        _min_expire_heap.push(map_value);

        _queue.push_back(map_value);
        
        _data_map[key] = map_value;

        return true;
    }

    /*
        * @brief 不加锁删除
        * @param key 键
        * @return 删除成功返回true, 否则返回false
    */
    bool erase_without_lock(const K& key) {
        if (_data_map.count(key) == 0) {
            return false;
        } else {
            // 标记删除, 后续标记删除的数据会在tick()中被延迟删除
            _data_map[key]->delete_value();
            // 从map中删除
            _data_map.erase(key);
            return true;
        }
    }

    /*
        * @brief 获取start_time到end_time对应在_queue中的两个迭代器
        * @param temp_queue 临时队列
        * @param start_time 起始时间
        * @param end_time 结束时间
        * @return std::pair<low, high> low为大于等于start_time的第一个元素, high为大于end_time的第一个元素
    */
    auto get_range(const std::deque<KeyValueSharedPtr>& temp_queue, const TimeStamp& start_time, const TimeStamp& end_time) {
        auto low = std::upper_bound(temp_queue.begin(), temp_queue.end(), start_time, [](const TimeStamp& time, const KeyValueSharedPtr map_value) {
            return map_value->get_insert_time() >= time;
        });

        auto high = std::upper_bound(temp_queue.begin(), temp_queue.end(), end_time, [](const TimeStamp& time, const KeyValueSharedPtr map_value) {
            return map_value->get_insert_time() > time;
        });

        return std::make_pair(low, high);
    }

    /*
        * @brief tick, 每次调用会根据expired_time清除顶部过期的key-value
    */
    void tick() {
        std::lock_guard<std::mutex> lock(_mutex);

        while (!_min_expire_heap.empty()) {
            auto top = _min_expire_heap.top();
            if (top->is_expire()) {
                top->delete_value();
                _min_expire_heap.pop();
                continue;
            }
            break;
        }

    }

    void tick_all() {
        decltype(_min_expire_heap) new_heap;
        decltype(_queue) new_queue;
        std::lock_guard<std::mutex> lock(_mutex);

        while (!_min_expire_heap.empty()) {
            auto top = _min_expire_heap.top();
            if (top->is_expire()) {
                top->delete_value();
                _min_expire_heap.pop();
                continue;
            }
            new_heap.push(top);
            _min_expire_heap.pop();
        }

        while (!_queue.empty()) {
            auto front = _queue.front();
            if (front->is_expire()) {
                front->delete_value();
                _queue.pop_front();
                continue;
            }
            new_queue.push_back(front);
            _queue.pop_front();
        }

        // 若map中的数据过期则清除map中的数据
        for (auto it = _data_map.begin(); it != _data_map.end();) {
            if (it->second->is_expire()) {
                it->second->delete_value();
                it = _data_map.erase(it);
            } else {
                ++it;
            }
        }

        _min_expire_heap.swap(new_heap);
        _queue.swap(new_queue);
    }

    /*
        * @brief 循环调用tick()清除过期数据
    */
    void loop_tick() {
        int count = 0;
        while (_is_running) {
            // 打印_data_map _min_expire_heap _queue的大小
            std::cout << "data_map size: " << _data_map.size() << " min_expire_heap size: " << _min_expire_heap.size() << " queue size: " << _queue.size() << std::endl;
            
            int interval = kDefaultCheckInterval;
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            if (!_is_running) {
                break;
            }
            if (count == kCheckAllTimes) {
                std::cout << "tick_all" << std::endl;
                tick_all();
                count = 0;
            } else {
                std::cout << "tick" << std::endl;
                tick();
                ++count;
            }
        }
    }

    // 存储对应的key-value
    std::unordered_map<K, KeyValueSharedPtr> _data_map;

    // KeyValue根据expire_time从小到大排序函数
    struct MinExpireCompare {
        bool operator()(const KeyValueSharedPtr lhs, const KeyValueSharedPtr rhs) {
            return lhs->get_expire_time() > rhs->get_expire_time();
        }
    };

    // 最小堆存储KeyValue, 根据expire_time从小到大排序
    std::priority_queue<KeyValueSharedPtr, std::vector<KeyValueSharedPtr>, MinExpireCompare> _min_expire_heap;

    // 双端队列, 按照insert_time从小到大存储
    std::deque<KeyValueSharedPtr> _queue;
    
    // 互斥锁
    std::mutex _mutex;

    // 执行tick()的线程
    std::thread _tick_thread;

    // 是否在运行标志
    std::atomic<bool> _is_running;
};