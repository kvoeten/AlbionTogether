#pragma once

#include <Windows.h>
#include <string>

namespace fable::launcher::platform
{
class ScopedEnvironmentVariable final
{
  public:
    ScopedEnvironmentVariable(const wchar_t *name, const wchar_t *value);
    ~ScopedEnvironmentVariable();

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
    ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;

    [[nodiscard]] bool applied() const
    {
        return applied_;
    }

  private:
    std::wstring name_;
    std::wstring previousValue_;
    bool hadPreviousValue_ = false;
    bool applied_ = false;
};
} // namespace fable::launcher::platform
