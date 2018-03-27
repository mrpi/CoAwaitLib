#include "routine.hpp"

namespace co
{

template <typename T>
class Routine::SpecificPtr
{
private:
    using CleanupFunction = void (*)(T*);
    CleanupFunction mCleanupFunction {[](T * val)
    {
        delete val;
    }
                                     };
    boost::thread_specific_ptr<T> mThreadPtr {mCleanupFunction};

public:
    SpecificPtr() = default;
    explicit SpecificPtr(void (*cleanupFunction)(T*))
        : mCleanupFunction(cleanupFunction)
    {
    }

    ~SpecificPtr()
    {
        reset();
    }

    T* get() const
    {
        Data* curr = current();
        if (curr) {
            auto itr = curr->mLocalStorage.find(this);
            if (itr != curr->mLocalStorage.end())
                return static_cast<T*>(itr->second.data);
            return nullptr;
        } else
            return mThreadPtr.get();
    }

    T* operator->() const
    {
        return get();
    }

    T& operator*() const
    {
        return *get();
    }

    T* release()
    {
        Data* curr = current();
        if (curr) {
            auto itr = curr->mLocalStorage.find(this);
            if (itr != curr->mLocalStorage.end()) {
                auto res = itr->second.data;
                itr->second.data = nullptr;
                return res;
            }
            return nullptr;
        } else
            return mThreadPtr.release();
    }

    void reset(T* newValue = nullptr)
    {
        Data* curr = current();
        if (curr)
            curr->mLocalStorage[this] = Data::StorageItem{newValue, mCleanupFunction};
        else
            return mThreadPtr.reset(newValue);
    }
};

}
