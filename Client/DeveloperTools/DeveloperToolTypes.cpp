#include "DeveloperToolTypes.h"

namespace fable::developer_tools
{
    DeveloperToolText DeveloperToolText::From(const char* text) noexcept
    {
        DeveloperToolText result;
        if (text == nullptr)
        {
            return result;
        }

        std::size_t index = 0U;
        while (index + 1U < result.value.size() && text[index] != '\0')
        {
            result.value[index] = text[index];
            ++index;
        }
        result.value[index] = '\0';
        return result;
    }

    bool DeveloperToolText::Empty() const noexcept
    {
        return value[0] == '\0';
    }
}
