#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

#include "key_value.h"
#include "safe_map.h"

/*
    * @brief 打印vector 中 KeyValue的内容
    * @param vec 要打印的vector
*/
template<typename K, typename V>
void print_key_value(std::vector<KeyValue<K, V>> vec) {
    int count = 0;
    for (auto& kv : vec) {
        // std::cout << "key: " << kv.get_key() << ", value: " << kv.get_value() << std::endl;
        ++count;
        if (count > 10) {
            break;
        }
    }
}

void print_data(SafeMap<int, int>& safe_map) {
    for (int i = 0; i < 10; ++i) {
        int v;
        safe_map.get_by_key(rand() % 1000, v);
        // std::cout << "get_by_key " << v << std::endl;
        // std::cout << "get_by_order " << std::endl;
        print_key_value(safe_map.get_by_order(10));
        // std::cout << "get_by_time_range " << std::endl;
        print_key_value(safe_map.get_by_time_range(std::chrono::system_clock::now() - std::chrono::milliseconds(rand() % 1000), std::chrono::system_clock::now() + std::chrono::seconds(rand() % 1000)));
    }
}
/*
    * @brief 向SafeMap中插入数据
    * @param safe_map 要插入数据的SafeMap
*/
void fill_data(SafeMap<int, int>& safe_map) {
    for (int i = 0; i < 10000; ++i) {
        // 随机插入数据, key随机, value随机, expire_time_interval随机
        safe_map.insert(rand() % 1000, rand() % 1000, rand() % 1000);
        safe_map.update_value(rand() % 1000, rand() % 1000, -1);
    }
}

void clear_data(SafeMap<int, int>& safe_map) {
    for (int i = 0; i < 100; ++i) {
        auto flag = safe_map.erase_by_key(rand() % 1000);
        // std::cout << "erase_by_key " << flag << std::endl;
        auto n = safe_map.erase_by_order(1000);
        // std::cout << "erase_by_order " << n << " elements" << std::endl;
        n = safe_map.erase_by_time_range(std::chrono::system_clock::now() - std::chrono::milliseconds(rand() % 1000), std::chrono::system_clock::now() + std::chrono::seconds(rand() % 1000));
        std::cout << "erase_by_time_range " << n << " elements" << std::endl;
    }
}

int main() {
    {
    SafeMap<int, int> safe_map;
    // 多线程插入数据
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.emplace_back(std::async(std::launch::async, [&safe_map]() {
            fill_data(safe_map);
        }));
    }
    // 多线程获取数据
    for (int i = 0; i < 10; ++i) {
        futures.emplace_back(std::async(std::launch::async, [&safe_map]() {
            print_data(safe_map);
        }));
    }
    
    // 多线程删除数据
    for (int i = 0; i < 10; ++i) {
        futures.emplace_back(std::async(std::launch::async, [&safe_map]() {
            clear_data(safe_map);
        }));
    }

    for (auto& future : futures) {
        future.get();
    }

    std::cout << "---------------------------" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    std::cout << "---------------------------" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
}