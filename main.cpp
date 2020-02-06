#include <type_traits>
#include <memory>
#include <atomic>
#include <mutex>
#include <boost/pool/object_pool.hpp>

template<class T>
struct AtomicCounter {
    T data;
    std::atomic<size_t> uses;

    template<class... Args>
    explicit AtomicCounter(Args... args) : data(std::move(args)...), uses(1) {}
};

template<class T>
class ParallelPool {
    boost::object_pool<AtomicCounter<T>> pool;
    std::mutex mutex;

public:
    template<class... Args>
    void free(Args &&... args) {
        std::lock_guard l(mutex);
        pool.free(std::forward<Args>(args)...);
    }

    template<class... Args>
    void destroy(Args &&... args) {
        std::lock_guard l(mutex);
        pool.destroy(std::forward<Args>(args)...);
    }

    template<class... Args>
    AtomicCounter<T> *malloc(Args &&... args) {
        std::lock_guard l(mutex);
        return pool.malloc(std::forward<Args>(args)...);
    }

    template<class... Args>
    AtomicCounter<T> *construct(Args &&... args) {
        std::lock_guard l(mutex);
        return pool.construct(std::forward<Args>(args)...);
    }
};

template<class T>
std::shared_ptr<ParallelPool<T>> getPool() {
    thread_local std::shared_ptr<ParallelPool<T>> pool = std::make_shared<ParallelPool<T>>();
    return pool;
}

template<class T, class>
class SmallImpl {
    std::shared_ptr<ParallelPool<T>> owner_;
    AtomicCounter<T> *counter_;

public:
    static const bool inlined = false;

    template<class... Args>
    explicit SmallImpl(Args... args) : owner_(getPool<T>()), counter_(owner_->construct(std::move(args)...)) {}

    SmallImpl(const SmallImpl &item) noexcept : owner_(item.owner_), counter_((++item.counter_->uses, item.counter_)) {}

    ~SmallImpl() {
        if (!--counter_->uses) {
            owner_->destroy(counter_);
        }
    }

    [[nodiscard]] const T &data() const noexcept {
        return counter_->data;
    }

    explicit operator const T &() const noexcept {
        return data();
    }
};

template<class T, class = void>
struct inlined_object : std::false_type {};

constexpr size_t MAX_SMALL_SIZE = 64;

template<class T>
struct inlined_object<T, std::enable_if_t<std::is_trivially_copyable_v<T> && sizeof(T) <= MAX_SMALL_SIZE>>
        : std::true_type {};

template<class T>
struct inlined_object<std::shared_ptr<T>, void> : std::true_type {};

template<class T>
constexpr bool inlined_object_v = inlined_object<T>::value;

template<class T>
class SmallImpl<T, std::enable_if_t<inlined_object_v<T>>> {
    T data_;

public:
    static const bool inlined = true;

    template<class... Args>
    explicit SmallImpl(Args... args) : data_(std::move(args)...) {}

    explicit operator T() const noexcept {
        return data_;
    }

    [[nodiscard]] T data() const noexcept {
        return data_;
    }
};

template<class T>
using Small = SmallImpl<T, void>;

// Tests

#include <iostream>
#include <thread>
#include <chrono>
#include <future>

struct tt {
    explicit tt(...) {} // NOLINT(hicpp-use-equals-default,modernize-use-equals-default)
};

struct CopyCounter {
    CopyCounter() = default;

    CopyCounter(CopyCounter &) noexcept {
        std::cout << "Copied!" << std::endl;
    }
};

int main() {
    Small<int> t(3);
    std::cout << t.data() << ' ' << Small<int>::inlined << std::endl;
    std::cout << Small<tt>::inlined << std::endl;
    std::cout << Small<std::shared_ptr<tt>>::inlined << std::endl;
    std::cout << Small<std::string>::inlined << std::endl;
    std::cout << Small<std::string>("lol").data() << std::endl;
    Small<std::string> s("kek");
    auto s1 = s; // NOLINT(performance-unnecessary-copy-initialization)
    std::cout << s.data() << ' ' << s1.data() << std::endl;
    Small<CopyCounter> copyCounter;
    auto copyCounter1 = copyCounter;
    std::cout << decltype(copyCounter1)::inlined << std::endl;

    // multithread tests
    std::mutex out;
    auto bigTest = [&] {
        for (size_t i = 0; i < 1000; ++i) {
            Small<std::string> s("s1");
        }
        std::lock_guard g(out);
        std::cout << std::this_thread::get_id() << ": finished; getPool: " << getPool<std::string>().get() << std::endl;
    };
    std::thread thread([=] { bigTest(); });
    using namespace std::chrono_literals;
    bigTest();
    thread.join();
    Small<std::string> shared("Common");
    auto sharedTest = [&] {
        for (size_t i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(1ms);
            std::lock_guard g(out);
            std::cout << std::this_thread::get_id() << " " << i << " " << shared.data()
                      << Small<std::string>(shared).data() << " " << std::endl;
        }
    };
    std::thread t1(sharedTest);
    sharedTest();
    t1.join();
    auto alienSmall = std::async(std::launch::async, [&out] {
        std::lock_guard g(out);
        std::cout << getPool<std::string>().get() << std::endl;
        return Small<std::string>("Alien string");
    }).get();
    std::cout << alienSmall.data() << " " << getPool<std::string>().get() << std::endl;
}