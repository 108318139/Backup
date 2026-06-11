#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <list>
#include <map>
#include <algorithm>

template<typename Key, typename Value, typename Hash=std::hash<Key>>
class threadsafe_lookup_table
{
private:
    // 🌟 內部類別：代表單一個「桶子」
    class bucket_type
    {
    private:
        typedef std::pair<Key, Value> bucket_value;
        typedef std::list<bucket_value> bucket_data;
        typedef typename bucket_data::iterator bucket_iterator;
        
        bucket_data data;
        // 🔒 每個桶子都有自己獨立的「讀寫鎖」
        mutable std::shared_mutex mutex;

        // 內部輔助函式：尋找鍵值
        bucket_iterator find_entry_for(Key const& key)
        {
            return std::find_if(data.begin(), data.end(),
                [&](bucket_value const& item) { return item.first == key; });
        }

    public:
        // 📖 讀取操作：允許多個執行緒「同時」進入這個桶子
        Value value_for(Key const& key, Value const& default_value) const
        {
            std::shared_lock<std::shared_mutex> lock(mutex);
            // 因為只是讀取，我們不需要擔心改變資料結構，例外安全(Exception-safe)
            auto const found_entry = const_cast<bucket_type*>(this)->find_entry_for(key);
            return (found_entry == data.end()) ? default_value : found_entry->second;
        }

        // 📝 寫入操作：排他鎖定，同一時間只能有一人修改
        void add_or_update_mapping(Key const& key, Value const& value)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            bucket_iterator const found_entry = find_entry_for(key);
            if(found_entry == data.end())
            {
                // push_back 保證例外安全，萬一記憶體不足拋出例外，也不會破壞原始 list
                data.push_back(bucket_value(key, value));
            }
            else
            {
                found_entry->second = value;
            }
        }

        // 📝 刪除操作：排他鎖定
        void remove_mapping(Key const& key)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            bucket_iterator const found_entry = find_entry_for(key);
            if(found_entry != data.end())
            {
                // erase 在標準庫中保證不會拋出例外
                data.erase(found_entry);
            }
        }
    };

    // 雜湊表的主體：固定數量的桶子陣列
    std::vector<std::unique_ptr<bucket_type>> buckets;
    Hash hasher;

    // 透過雜湊函數，決定這個 Key 該去哪個桶子
    bucket_type& get_bucket(Key const& key) const
    {
        std::size_t const bucket_index = hasher(key) % buckets.size();
        return *buckets[bucket_index];
    }

public:
    // 建構子：預設使用 19 個桶子 (質數能讓雜湊分佈更均勻)
    threadsafe_lookup_table(unsigned num_buckets = 19, Hash const& hasher_ = Hash()):
        buckets(num_buckets), hasher(hasher_)
    {
        for(unsigned i = 0; i < num_buckets; ++i)
        {
            buckets[i].reset(new bucket_type);
        }
    }

    // 禁用複製與賦值
    threadsafe_lookup_table(const threadsafe_lookup_table& other) = delete;
    threadsafe_lookup_table& operator=(const threadsafe_lookup_table& other) = delete;

    // --- 公開 API 區：將工作委託給對應的桶子 ---

    Value value_for(Key const& key, Value const& default_value = Value()) const
    {
        return get_bucket(key).value_for(key, default_value);
    }

    void add_or_update_mapping(Key const& key, Value const& value)
    {
        get_bucket(key).add_or_update_mapping(key, value);
    }

    void remove_mapping(Key const& key)
    {
        get_bucket(key).remove_mapping(key);
    }

    // 📸 取得完整快照 (Listing 6.12)
    std::map<Key, Value> get_map() const
    {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        
        // 🛡️ 避免死結的關鍵：
        // 為了取得完整快照，我們必須鎖定「所有」的桶子。
        // 如果執行緒不按順序亂鎖，極易引發死結 (Deadlock)。
        // 因此，我們強迫按照索引順序 (0, 1, 2...) 一路鎖下去。
        for(unsigned i = 0; i < buckets.size(); ++i)
        {
            locks.push_back(std::unique_lock<std::shared_mutex>(buckets[i]->mutex));
        }

        // 所有桶子都鎖定後，才能安心複製資料
        std::map<Key, Value> res;
        for(unsigned i = 0; i < buckets.size(); ++i)
        {
            for(auto it = buckets[i]->data.begin(); it != buckets[i]->data.end(); ++it)
            {
                res.insert(*it);
            }
        }
        return res;
    }
};
