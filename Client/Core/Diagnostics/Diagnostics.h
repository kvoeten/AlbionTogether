#pragma once

namespace fable::core
{
    struct Diagnostics
    {
        void (*log)(const char* message) = nullptr;
        void (*event)(const char* state, const char* detail) = nullptr;

        void Log(const char* message) const
        {
            if (log != nullptr)
            {
                log(message);
            }
        }

        void Event(const char* state, const char* detail) const
        {
            if (event != nullptr)
            {
                event(state, detail);
            }
        }
    };
}
