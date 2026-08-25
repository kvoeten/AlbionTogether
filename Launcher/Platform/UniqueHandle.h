#pragma once

#include <Windows.h>

namespace fable::launcher::platform
{
class UniqueHandle final
{
  public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle)
    {
    }
    ~UniqueHandle()
    {
        reset();
    }
    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;
    UniqueHandle(UniqueHandle &&other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }
    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const
    {
        return handle_;
    }
    [[nodiscard]] bool valid() const
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    void reset(HANDLE handle = nullptr)
    {
        if (valid())
            CloseHandle(handle_);
        handle_ = handle;
    }

  private:
    HANDLE handle_ = nullptr;
};
} // namespace fable::launcher::platform
