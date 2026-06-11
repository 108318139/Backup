#include <mutex>
#include <memory>

template<typename T>
class threadsafe_list
{
    // 🌟 核心結構：每一個節點都自帶一把鎖
    struct node
    {
        std::mutex m; 
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
        
        node(): next() {}
        
        node(T const& value):
            data(std::make_shared<T>(value)) {}
    };

    // 這裡同樣使用了「虛擬節點 (Dummy node)」的概念作為開頭
    node head;

public:
    threadsafe_list() {}

    ~threadsafe_list()
    {
        // 解構時移除所有節點
        remove_if([](node const&){return true;});
    }

    threadsafe_list(threadsafe_list const& other) = delete;
    threadsafe_list& operator=(threadsafe_list const& other) = delete;

    // --- 新增節點 ---
    void push_front(T const& value)
    {
        // ⚡ 效能優化：在加鎖前，先完成耗時的記憶體配置
        std::unique_ptr<node> new_node(new node(value));
        
        // 🔒 只需要鎖定頭部節點 (head)，因為我們只修改最前面的指標
        std::lock_guard<std::mutex> lk(head.m);
        new_node->next = std::move(head.next);
        head.next = std::move(new_node);
    }

    // --- 走訪並執行動作 ---
    template<typename Function>
    void for_each(Function f)
    {
        node* current = &head;
        // 🐒 盪猴架第一步：鎖住目前節點 (初始為 head)
        std::unique_lock<std::mutex> lk(head.m);
        
        while(node* const next = current->next.get())
        {
            // 🐒 盪猴架第二步：取得並鎖住「下一個」節點
            std::unique_lock<std::mutex> next_lk(next->m);
            
            // 🐒 盪猴架第三步：放開目前節點的鎖 (現在手上只握著下一個節點的鎖)
            // 這讓後面的執行緒可以跟上來處理我們剛離開的節點
            lk.unlock();
            
            // 執行使用者自訂的函數。此時資料被 next_lk 妥善保護著。
            f(*next->data);
            
            // 往前移動：將 current 設為 next，並把鎖的所有權轉移
            current = next;
            lk = std::move(next_lk);
        }
    }

    // --- 尋找符合條件的第一個元素 ---
    template<typename Predicate>
    std::shared_ptr<T> find_first_if(Predicate p)
    {
        node* current = &head;
        std::unique_lock<std::mutex> lk(head.m); // 鎖住當前節點
        
        while(node* const next = current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m); // 鎖住下一個節點
            lk.unlock(); // 放開當前節點
            
            if(p(*next->data))
            {
                return next->data; // 找到就直接回傳，鎖會在離開範圍時自動釋放
            }
            current = next;
            lk = std::move(next_lk);
        }
        return std::shared_ptr<T>();
    }

    // --- 移除符合條件的元素 ---
    template<typename Predicate>
    void remove_if(Predicate p)
    {
        node* current = &head;
        std::unique_lock<std::mutex> lk(head.m);
        
        while(node* const next = current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            
            if(p(*next->data))
            {
                // ✂️ 修改指標以跳過這個節點 (將它從鏈結串列中拔除)
                std::unique_ptr<node> old_next = std::move(current->next);
                current->next = std::move(next->next);
                
                // 🛡️ 例外安全與死結防護：
                // 這裡我們「先解鎖」，然後 old_next 離開這個 if 區塊時才會被解構銷毀。
                // 絕對不能在持有鎖的狀態下銷毀節點，否則會造成未定義行為！
                next_lk.unlock(); 
            }
            else
            {
                // 如果沒有要移除，就繼續正常的盪猴架移動
                lk.unlock();
                current = next;
                lk = std::move(next_lk);
            }
        }
    }
};
