#include "AutoSaveFunction.h"

#include "Game/Entity/EntityService.h"
#include "Game/Native/Addresses.h"
#include "Game/Native/GameInterface.h"
#include "Game/Native/ScriptTypes.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace fable::game::persistence::native
{
    namespace
    {
        constexpr std::uint8_t SaveGameStateManualPrefix[] = {
            0x6A, 0xFF, // push -1
            0x68        // push exception handler
        };
        constexpr std::uint8_t UserProfileManagerPrefix[] = {
            0x6A, 0xFF, 0x68
        };
        constexpr std::uint8_t GetAutoSavePathNamePrefix[] = {
            0x51, 0x56, 0x8B, 0x74, 0x24, 0x0C
        };
        constexpr std::uint8_t WideStringDestructorPrefix[] = {
            0x57, 0x8B, 0xF9, 0x8B, 0x07
        };
        constexpr std::size_t SaveStateOffset = 0x144;
        constexpr std::size_t LoadStateOffset = 0x148;

        void* ResolveWorld(EntityService& entities) noexcept
        {
            game::native::GameScriptInterface* const gameInterface =
                entities.Interface().Resolve();
            if (gameInterface == nullptr)
            {
                return nullptr;
            }
            void* world = nullptr;
            __try
            {
                world = reinterpret_cast<void**>(gameInterface)[1];
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                world = nullptr;
            }
            return world;
        }

        bool Matches(
            const void* const address,
            const void* const expected,
            const std::size_t size) noexcept
        {
            bool matches = false;
            __try
            {
                matches = std::memcmp(address, expected, size) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                matches = false;
            }
            return matches;
        }
    }

    bool AutoSaveFunction::Invoke(EntityService& entities) noexcept
    {
        void* const world = ResolveWorld(entities);
        const HMODULE gameModule = entities.GameModule();
        if (world == nullptr || gameModule == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        void* const saveAddress = reinterpret_cast<void*>(
            base + game::native::rva::WorldSaveGameStateManual);
        void* const profileAddress = reinterpret_cast<void*>(
            base + game::native::rva::UserProfileManager);
        void* const pathAddress = reinterpret_cast<void*>(
            base + game::native::rva::UserProfileGetAutoSavePathName);
        void* const destructorAddress = reinterpret_cast<void*>(
            base + game::native::rva::WideStringDestructor);
        if (!Matches(
                saveAddress,
                SaveGameStateManualPrefix,
                sizeof(SaveGameStateManualPrefix)) ||
            !Matches(
                profileAddress,
                UserProfileManagerPrefix,
                sizeof(UserProfileManagerPrefix)) ||
            !Matches(
                pathAddress,
                GetAutoSavePathNamePrefix,
                sizeof(GetAutoSavePathNamePrefix)) ||
            !Matches(
                destructorAddress,
                WideStringDestructorPrefix,
                sizeof(WideStringDestructorPrefix)))
        {
            return false;
        }

        using ResolveUserProfileManager = void* (__cdecl*)();
        using GetAutoSavePathName = game::native::WideString* (__thiscall*)(
            void*, game::native::WideString*);
        using SaveGameStateManual = void(__thiscall*)(
            void*, const game::native::WideString*);
        using DestroyWideString = void(__thiscall*)(game::native::WideString*);
        game::native::WideString path;
        bool invoked = false;
        __try
        {
            void* const profile =
                reinterpret_cast<ResolveUserProfileManager>(profileAddress)();
            if (profile != nullptr &&
                reinterpret_cast<GetAutoSavePathName>(pathAddress)(
                    profile, &path) == &path &&
                path.stringData != nullptr)
            {
                // Use the same complete save boundary as the in-game menu.
                // AutoSave() only stages an asynchronous snapshot in this
                // build; SaveGameStateManual() also commits it through the
                // active platform/profile manager.
                reinterpret_cast<SaveGameStateManual>(saveAddress)(
                    world, &path);
                invoked = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            invoked = false;
        }
        if (path.stringData != nullptr)
        {
            __try
            {
                reinterpret_cast<DestroyWideString>(destructorAddress)(&path);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                invoked = false;
            }
        }
        return invoked;
    }

    bool AutoSaveFunction::ReadState(
        EntityService& entities,
        AutoSaveState& state) noexcept
    {
        state = {};
        void* const world = ResolveWorld(entities);
        if (world == nullptr)
        {
            return false;
        }
        bool read = false;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(world);
            state.saveState = *reinterpret_cast<const std::int32_t*>(
                bytes + SaveStateOffset);
            state.loadState = *reinterpret_cast<const std::int32_t*>(
                bytes + LoadStateOffset);
            read = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = {};
            read = false;
        }
        return read;
    }
}
