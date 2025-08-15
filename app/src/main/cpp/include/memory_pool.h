// memory_pool.h - Custom memory management for high performance
template<typename T>
class ObjectPool {
private:
    std::vector<std::unique_ptr<T>> available;
    std::vector<std::unique_ptr<T>> inUse;

public:
    T* acquire() {
        if (available.empty()) {
            available.push_back(std::make_unique<T>());
        }

        auto obj = std::move(available.back());
        available.pop_back();
        T* ptr = obj.get();
        inUse.push_back(std::move(obj));
        return ptr;
    }

    void release(T* obj) {
        auto it = std::find_if(inUse.begin(), inUse.end(),
                               [obj](const std::unique_ptr<T>& ptr) { return ptr.get() == obj; });

        if (it != inUse.end()) {
            available.push_back(std::move(*it));
            inUse.erase(it);
        }
    }
};

// Global pools for frequently used objects
extern ObjectPool<InteractionEvent> g_eventPool;
extern ObjectPool<SleepSession> g_sessionPool;