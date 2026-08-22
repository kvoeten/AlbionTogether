#include "ThingComponentProvisioner.h"

#include "Game/Native/Addresses.h"
#include "Game/Native/ScriptTypes.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace
{
    using CharStringConstructor = void(__thiscall*)(
        fable::game::native::CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(
        fable::game::native::CharString*);
    using ThingAddComponent = void* (__thiscall*)(
        void*, const fable::game::native::CharString*, bool, void*);

    constexpr std::array<std::uint8_t, 3> kThingAddComponentPrefix = {
        // The following SEH handler address is loader-relocated under ASLR.
        0x6A, 0xFF, 0x68,
    };

    bool Matches(
        const std::uint8_t* address,
        const std::uint8_t* expected,
        std::size_t size) noexcept
    {
        __try
        {
            return std::memcmp(address, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace fable::game::entity::native
{
    bool ThingComponentProvisioner::IsSupported(
        HMODULE gameModule) noexcept
    {
        if (gameModule == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        return Matches(
            reinterpret_cast<const std::uint8_t*>(
                base + game::native::rva::ThingAddComponent),
            kThingAddComponentPrefix.data(),
            kThingAddComponentPrefix.size());
    }

    void* ThingComponentProvisioner::AddNamed(
        HMODULE gameModule,
        void* thing,
        const char* componentName) noexcept
    {
        if (thing == nullptr || componentName == nullptr ||
            componentName[0] == '\0' || !IsSupported(gameModule))
        {
            return nullptr;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + game::native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + game::native::rva::CharStringDestructor);
        const auto addComponent = reinterpret_cast<ThingAddComponent>(
            base + game::native::rva::ThingAddComponent);

        game::native::CharString component;
        __try
        {
            constructString(&component, componentName, -1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }

        void* added = nullptr;
        __try
        {
            // Retail optional components use replacement semantics and no
            // parameter block when attached by name.
            added = addComponent(thing, &component, true, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            added = nullptr;
        }

        __try
        {
            destroyString(&component);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
        return added;
    }
}
