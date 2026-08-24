#include "ScopedEnvironmentVariable.h"

namespace fable::launcher::platform
{
ScopedEnvironmentVariable::ScopedEnvironmentVariable(const wchar_t *name, const wchar_t *value) : name_(name)
{
    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
    if (required != 0)
    {
        previousValue_.resize(required);
        const DWORD length = GetEnvironmentVariableW(name_.c_str(), previousValue_.data(), required);
        previousValue_.resize(length);
        hadPreviousValue_ = true;
    }
    else if (GetLastError() != ERROR_ENVVAR_NOT_FOUND)
    {
        return;
    }

    applied_ = SetEnvironmentVariableW(name_.c_str(), value) != FALSE;
}

ScopedEnvironmentVariable::~ScopedEnvironmentVariable()
{
    if (applied_)
    {
        SetEnvironmentVariableW(name_.c_str(), hadPreviousValue_ ? previousValue_.c_str() : nullptr);
    }
}
} // namespace fable::launcher::platform
