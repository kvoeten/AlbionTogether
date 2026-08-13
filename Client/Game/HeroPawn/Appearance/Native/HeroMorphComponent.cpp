#include "HeroMorphComponent.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr std::size_t kUpdateRequestedOffset = 0x55;
    constexpr std::size_t kStrengthOffset = 0x58;
    constexpr std::size_t kBerserkOffset = 0x5C;
    constexpr std::size_t kWillOffset = 0x60;
    constexpr std::size_t kSkillOffset = 0x64;
    constexpr std::size_t kAgeOffset = 0x68;
    constexpr std::size_t kAlignmentOffset = 0x6C;
    constexpr std::size_t kFatnessOffset = 0x70;
    constexpr std::size_t kAuxiliaryOffset = 0x74;
    constexpr std::size_t kChildOffset = 0x78;
    constexpr std::size_t kCompositeTextureCommandDataOffset = 0x7C;
    constexpr std::size_t kCompositeTextureCommandByteCountOffset = 0x80;
    constexpr std::size_t kCompositeTextureCommandStride = 0x10;
    constexpr std::size_t kMaximumCompositeTextureCommandCount = 64;

    constexpr std::size_t kThingGraphicOffset = 0xA4;
    constexpr std::size_t kGraphicBridgeOffset = 0x08;
    constexpr std::size_t kGraphicReadyVtableOffset = 0x10;
    constexpr std::size_t kGraphicPawnGetterVtableOffset = 0x348;
    constexpr std::size_t kPawnSkeletalMeshComponentOffset = 0x384;
    constexpr std::size_t kSkeletalMeshAssetOffset = 0x1E4;
    constexpr std::size_t kSkeletalMeshAnimTreeOffset = 0x1F0;
    // UE3 USkeletalMeshComponent::FindMorphTarget searches this native
    // TMap<FName, UMorphTarget*> cache. Each pair occupies 0x14 bytes with
    // the target pointer at +0x08; the embedded pair-array count is +0x04.
    constexpr std::size_t kSkeletalMeshMorphTargetMapOffset = 0x2E8;
    constexpr std::size_t kSkeletalMeshMorphTargetCountOffset = 0x2EC;
    constexpr std::size_t kMorphTargetPairStride = 0x14;
    constexpr std::size_t kReferenceBoneDataOffset = 0x94;
    constexpr std::size_t kReferenceBoneCountOffset = 0x98;
    constexpr std::size_t kReferenceBoneStride = 0x50;
    constexpr std::size_t kMassScalingDataOffset = 0xBC;
    constexpr std::size_t kMassScalingCountOffset = 0xC0;

    constexpr std::uintptr_t kFNameConstructRva = 0x001D1100;
    constexpr std::uintptr_t kFindSkelControlRva = 0x006F7760;
    constexpr std::uintptr_t kEnumerateMorphNodesRva = 0x00410120;
    constexpr std::uintptr_t kGameArrayFreeRva = 0x0016B8C0;
    constexpr std::uintptr_t kSetBoneTranslationScaleRva = 0x006DEA30;
    constexpr std::array<std::uint8_t, 7> kFNameConstructSignature = {
        0x8B, 0x44, 0x24, 0x08, 0x56, 0x50, 0x8B};
    constexpr std::array<std::uint8_t, 7> kFindSkelControlSignature = {
        0x8B, 0x81, 0xF0, 0x01, 0x00, 0x00, 0x50};
    constexpr std::array<std::uint8_t, 6> kSetBoneScaleSignature = {
        0x83, 0xEC, 0x14, 0x56, 0x8B, 0xF1};
    constexpr std::array<std::uint8_t, 9> kEnumerateMorphNodesSignature = {
        0x53, 0x8B, 0x5C, 0x24, 0x08, 0x83, 0x7B, 0x08, 0x00};
    constexpr std::array<std::uint8_t, 10> kGameArrayFreeSignature = {
        0x8B, 0x0D, 0x6C, 0x4C, 0x49, 0x03, 0x85, 0xC9, 0x75, 0x0B};

    struct NativeFName final
    {
        std::uint32_t index = 0;
        std::uint32_t number = 0;
    };

    struct NativeVector3 final
    {
        float x = 1.0f;
        float y = 1.0f;
        float z = 1.0f;
    };

    struct NativePointerArray final
    {
        void** data = nullptr;
        std::int32_t count = 0;
        std::int32_t capacity = 0;
    };

    struct ResolvedPresentation final
    {
        void* graphic = nullptr;
        void* graphicVtable = nullptr;
        void* graphicBridge = nullptr;
        void* graphicBridgeVtable = nullptr;
        void* pawn = nullptr;
        void* skeletalMesh = nullptr;
        void* skeletalMeshAsset = nullptr;
        void* animTree = nullptr;
        void* massBoneScaling = nullptr;
        std::int32_t morphTargetCount = 0;
        std::uint8_t* referenceBones = nullptr;
        std::int32_t referenceBoneCount = 0;
        NativeVector3* scales = nullptr;
        std::int32_t scaleCount = 0;
    };

    void* FindMorphComponent(void* nativeThing) noexcept
    {
        return fable::game::entity::native::ThingComponentAccess::Find(
            nativeThing,
            fable::game::entity::native::ThingComponentType::HeroMorph);
    }

    bool IsReadableRange(const void* address, std::size_t bytes) noexcept
    {
        if (address == nullptr || bytes == 0)
        {
            return false;
        }
        auto cursor = reinterpret_cast<std::uintptr_t>(address);
        const auto end = cursor + bytes;
        if (end < cursor)
        {
            return false;
        }
        while (cursor < end)
        {
            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(cursor),
                    &region,
                    sizeof(region)) != sizeof(region) ||
                region.State != MEM_COMMIT ||
                (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            {
                return false;
            }
            const auto regionEnd = reinterpret_cast<std::uintptr_t>(
                region.BaseAddress) + region.RegionSize;
            if (regionEnd <= cursor)
            {
                return false;
            }
            cursor = regionEnd < end ? regionEnd : end;
        }
        return true;
    }

    bool IsExecutableAddress(const void* address) noexcept
    {
        MEMORY_BASIC_INFORMATION region = {};
        if (address == nullptr ||
            VirtualQuery(address, &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        {
            return false;
        }
        const DWORD protection = region.Protect & 0xFFu;
        return protection == PAGE_EXECUTE ||
            protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
    }

    template <std::size_t Size>
    bool HasSignature(const std::uint8_t* address,
                      const std::array<std::uint8_t, Size>& signature) noexcept
    {
        return IsReadableRange(address, Size) &&
            std::memcmp(address, signature.data(), Size) == 0;
    }

    bool ConstructFName(const char* text, NativeFName& result) noexcept
    {
        result = {};
        auto* const module = reinterpret_cast<std::uint8_t*>(
            GetModuleHandleW(nullptr));
        auto* const address = module == nullptr
            ? nullptr
            : module + kFNameConstructRva;
        if (text == nullptr ||
            !HasSignature(address, kFNameConstructSignature))
        {
            return false;
        }
        __try
        {
            using ConstructFunction = void* (__thiscall*)(
                NativeFName*, const char*, int);
            reinterpret_cast<ConstructFunction>(address)(&result, text, 1);
            return result.index != 0 || result.number != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = {};
            return false;
        }
    }

    bool ResolvePresentation(
        void* nativeThing,
        ResolvedPresentation& result) noexcept
    {
        result = {};
        if (nativeThing == nullptr)
        {
            return false;
        }
        auto* const module = reinterpret_cast<std::uint8_t*>(
            GetModuleHandleW(nullptr));
        auto* const findAddress = module == nullptr
            ? nullptr
            : module + kFindSkelControlRva;
        if (!HasSignature(findAddress, kFindSkelControlSignature))
        {
            return false;
        }
        __try
        {
            auto* const thing = static_cast<std::uint8_t*>(nativeThing);
            result.graphic = *reinterpret_cast<void**>(
                thing + kThingGraphicOffset);
            if (!IsReadableRange(result.graphic, sizeof(void*)))
            {
                return false;
            }
            result.graphicVtable = *static_cast<void**>(result.graphic);
            if (!IsReadableRange(result.graphicVtable,
                    kGraphicReadyVtableOffset + sizeof(void*)))
            {
                return false;
            }
            using IsReadyFunction = bool(__thiscall*)(void*);
            auto* const isReady = reinterpret_cast<IsReadyFunction>(
                *(reinterpret_cast<void***>(result.graphicVtable) +
                    kGraphicReadyVtableOffset / sizeof(void*)));
            if (!IsExecutableAddress(reinterpret_cast<void*>(isReady)) ||
                !isReady(result.graphic))
            {
                return false;
            }
            result.graphicBridge = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(result.graphic) +
                    kGraphicBridgeOffset);
            if (!IsReadableRange(result.graphicBridge, sizeof(void*)))
            {
                return false;
            }
            result.graphicBridgeVtable = *static_cast<void**>(
                result.graphicBridge);
            if (!IsReadableRange(result.graphicBridgeVtable,
                    kGraphicPawnGetterVtableOffset + sizeof(void*)))
            {
                return false;
            }
            using GetPawnFunction = void* (__thiscall*)(void*);
            auto* const getPawn = reinterpret_cast<GetPawnFunction>(
                *(reinterpret_cast<void***>(result.graphicBridgeVtable) +
                    kGraphicPawnGetterVtableOffset / sizeof(void*)));
            if (!IsExecutableAddress(reinterpret_cast<void*>(getPawn)))
            {
                return false;
            }
            result.pawn = getPawn(result.graphicBridge);
            if (!IsReadableRange(result.pawn,
                    kPawnSkeletalMeshComponentOffset + sizeof(void*)))
            {
                return false;
            }
            auto* const pawn = static_cast<std::uint8_t*>(result.pawn);
            result.skeletalMesh = *reinterpret_cast<void**>(
                pawn + kPawnSkeletalMeshComponentOffset);
            if (!IsReadableRange(result.skeletalMesh,
                    kSkeletalMeshMorphTargetCountOffset +
                        sizeof(std::int32_t)))
            {
                return false;
            }
            auto* const mesh = static_cast<std::uint8_t*>(
                result.skeletalMesh);
            result.skeletalMeshAsset = *reinterpret_cast<void**>(
                mesh + kSkeletalMeshAssetOffset);
            result.animTree = *reinterpret_cast<void**>(
                mesh + kSkeletalMeshAnimTreeOffset);
            auto* const morphTargets = *reinterpret_cast<std::uint8_t**>(
                mesh + kSkeletalMeshMorphTargetMapOffset);
            const std::int32_t morphTargetCount =
                *reinterpret_cast<std::int32_t*>(
                    mesh + kSkeletalMeshMorphTargetCountOffset);
            if (morphTargetCount >= 0 && morphTargetCount <= 4'096 &&
                (morphTargetCount == 0 || IsReadableRange(
                    morphTargets,
                    static_cast<std::size_t>(morphTargetCount) *
                        kMorphTargetPairStride)))
            {
                result.morphTargetCount = morphTargetCount;
            }
            if (!IsReadableRange(result.skeletalMeshAsset,
                    kReferenceBoneCountOffset + sizeof(std::int32_t)) ||
                result.animTree == nullptr)
            {
                return false;
            }
            auto* const asset = static_cast<std::uint8_t*>(
                result.skeletalMeshAsset);
            result.referenceBones = *reinterpret_cast<std::uint8_t**>(
                asset + kReferenceBoneDataOffset);
            result.referenceBoneCount = *reinterpret_cast<std::int32_t*>(
                asset + kReferenceBoneCountOffset);
            if (result.referenceBoneCount <= 0 ||
                result.referenceBoneCount > 1'024 ||
                !IsReadableRange(
                    result.referenceBones,
                    static_cast<std::size_t>(result.referenceBoneCount) *
                        kReferenceBoneStride))
            {
                return false;
            }
            NativeFName controllerName;
            if (!ConstructFName("MassBoneScaling", controllerName))
            {
                return false;
            }
            using FindFunction = void* (__thiscall*)(
                void*, std::uint32_t, std::uint32_t);
            result.massBoneScaling = reinterpret_cast<FindFunction>(
                findAddress)(
                    result.skeletalMesh,
                    controllerName.index,
                    controllerName.number);
            if (!IsReadableRange(
                    result.massBoneScaling,
                    kMassScalingCountOffset + sizeof(std::int32_t)))
            {
                return false;
            }
            auto* const controller = static_cast<std::uint8_t*>(
                result.massBoneScaling);
            result.scales = *reinterpret_cast<NativeVector3**>(
                controller + kMassScalingDataOffset);
            result.scaleCount = *reinterpret_cast<std::int32_t*>(
                controller + kMassScalingCountOffset);
            return result.scaleCount >= 0 &&
                result.scaleCount <= result.referenceBoneCount &&
                (result.scaleCount == 0 ||
                    IsReadableRange(
                        result.scales,
                        static_cast<std::size_t>(result.scaleCount) *
                            sizeof(NativeVector3)));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = {};
            return false;
        }
    }

    void InspectMorphNodes(
        void* animTree,
        std::int32_t& nodeCount,
        std::int32_t& weightedNodeCount,
        float& maximumWeight) noexcept
    {
        nodeCount = 0;
        weightedNodeCount = 0;
        maximumWeight = 0.0f;
        auto* const module = reinterpret_cast<std::uint8_t*>(
            GetModuleHandleW(nullptr));
        auto* const enumerateAddress = module == nullptr
            ? nullptr
            : module + kEnumerateMorphNodesRva;
        auto* const freeAddress = module == nullptr
            ? nullptr
            : module + kGameArrayFreeRva;
        if (animTree == nullptr ||
            !HasSignature(enumerateAddress, kEnumerateMorphNodesSignature) ||
            !HasSignature(freeAddress, kGameArrayFreeSignature))
        {
            return;
        }

        NativePointerArray nodes;
        __try
        {
            using EnumerateFunction = void(__thiscall*)(
                void*, NativePointerArray*);
            reinterpret_cast<EnumerateFunction>(enumerateAddress)(
                animTree, &nodes);
            if (nodes.count >= 0 && nodes.count <= 1'024 &&
                (nodes.count == 0 || IsReadableRange(
                    nodes.data,
                    static_cast<std::size_t>(nodes.count) * sizeof(void*))))
            {
                nodeCount = nodes.count;
                for (std::int32_t index = 0; index < nodes.count; ++index)
                {
                    auto* const node = static_cast<std::uint8_t*>(
                        nodes.data[index]);
                    if (!IsReadableRange(node, 0x7C))
                    {
                        continue;
                    }
                    float weight = 0.0f;
                    std::memcpy(&weight, node + 0x78, sizeof(weight));
                    if (std::isfinite(weight) && std::fabs(weight) > 0.0001f)
                    {
                        ++weightedNodeCount;
                        maximumWeight = (std::max)(
                            maximumWeight, std::fabs(weight));
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            nodeCount = 0;
            weightedNodeCount = 0;
            maximumWeight = 0.0f;
        }
        if (nodes.data != nullptr)
        {
            __try
            {
                using FreeFunction = void(__cdecl*)(void*);
                reinterpret_cast<FreeFunction>(freeAddress)(nodes.data);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                nodeCount = 0;
                weightedNodeCount = 0;
                maximumWeight = 0.0f;
            }
        }
    }

    void InspectCompositeTextureCommands(
        void* nativeThing,
        std::int32_t& commandCount,
        std::uint32_t& commandHash,
        std::array<std::uint32_t, 3>& firstCommand,
        float& firstWeight) noexcept
    {
        commandCount = 0;
        commandHash = 0;
        firstCommand = {};
        firstWeight = 0.0f;
        const auto* const component = static_cast<const std::uint8_t*>(
            FindMorphComponent(nativeThing));
        if (!IsReadableRange(
                component,
                kCompositeTextureCommandByteCountOffset +
                    sizeof(std::uint32_t)))
        {
            return;
        }
        __try
        {
            const auto* const data = *reinterpret_cast<std::uint8_t* const*>(
                component + kCompositeTextureCommandDataOffset);
            const std::uint32_t byteCount = *reinterpret_cast<const std::uint32_t*>(
                component + kCompositeTextureCommandByteCountOffset);
            if (byteCount % kCompositeTextureCommandStride != 0 ||
                byteCount / kCompositeTextureCommandStride >
                    kMaximumCompositeTextureCommandCount ||
                (byteCount != 0 && !IsReadableRange(data, byteCount)))
            {
                return;
            }
            commandCount = static_cast<std::int32_t>(
                byteCount / kCompositeTextureCommandStride);
            std::uint32_t hash = 2166136261u;
            for (std::uint32_t index = 0; index < byteCount; ++index)
            {
                hash ^= data[index];
                hash *= 16777619u;
            }
            commandHash = hash;
            if (commandCount != 0)
            {
                std::memcpy(
                    firstCommand.data(), data, sizeof(firstCommand));
                std::memcpy(
                    &firstWeight,
                    data + sizeof(firstCommand),
                    sizeof(firstWeight));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            commandCount = 0;
            commandHash = 0;
            firstCommand = {};
            firstWeight = 0.0f;
        }
    }

    bool IsIdentity(const NativeVector3& value) noexcept
    {
        constexpr float epsilon = 0.0001f;
        return std::fabs(value.x - 1.0f) < epsilon &&
            std::fabs(value.y - 1.0f) < epsilon &&
            std::fabs(value.z - 1.0f) < epsilon;
    }

    fable::game::hero_pawn::appearance::HeroBoneScale BoneScaleAt(
        const ResolvedPresentation& presentation,
        std::int32_t index) noexcept
    {
        fable::game::hero_pawn::appearance::HeroBoneScale result;
        result.boneIndex = static_cast<std::uint16_t>(index);
        if (index < presentation.scaleCount)
        {
            result.x = presentation.scales[index].x;
            result.y = presentation.scales[index].y;
            result.z = presentation.scales[index].z;
        }
        return result;
    }
}

namespace fable::game::hero_pawn::appearance::native
{
    bool HeroMorphComponent::InspectResolution(
        void* nativeThing,
        HeroMorphResolutionState& state) noexcept
    {
        state = {};
        state.thing = nativeThing;
        state.heroMorphComponent = FindMorphComponent(nativeThing);
        ResolvedPresentation presentation;
        const bool resolved = ResolvePresentation(nativeThing, presentation);
        state.graphic = presentation.graphic;
        state.graphicVtable = presentation.graphicVtable;
        state.graphicBridge = presentation.graphicBridge;
        state.graphicBridgeVtable = presentation.graphicBridgeVtable;
        state.pawn = presentation.pawn;
        state.skeletalMeshComponent = presentation.skeletalMesh;
        state.animTree = presentation.animTree;
        state.massBoneScaling = presentation.massBoneScaling;
        state.referenceBoneCount = presentation.referenceBoneCount;
        state.scaleCount = presentation.scaleCount;
        state.morphTargetCount = presentation.morphTargetCount;
        if (resolved)
        {
            InspectMorphNodes(
                presentation.animTree,
                state.morphNodeCount,
                state.weightedMorphNodeCount,
                state.maximumMorphNodeWeight);
        }
        InspectCompositeTextureCommands(
            nativeThing,
            state.compositeTextureCommandCount,
            state.compositeTextureCommandHash,
            state.firstCompositeTextureCommand,
            state.firstCompositeTextureWeight);
        return resolved;
    }

    bool HeroMorphComponent::SetUpdateRequested(
        void* nativeThing,
        bool requested) noexcept
    {
        void* const component = FindMorphComponent(nativeThing);
        if (component == nullptr)
        {
            return false;
        }
        __try
        {
            static_cast<std::uint8_t*>(component)[kUpdateRequestedOffset] =
                requested ? 1u : 0u;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool HeroMorphComponent::Capture(
        void* nativeThing,
        HeroMorphState& state) noexcept
    {
        state = {};
        const auto* const component = static_cast<const std::uint8_t*>(
            FindMorphComponent(nativeThing));
        if (component == nullptr)
        {
            return false;
        }
        __try
        {
            state.valid = true;
            state.child = component[kChildOffset] != 0;
            std::memcpy(&state.strength, component + kStrengthOffset, sizeof(float));
            std::memcpy(&state.berserk, component + kBerserkOffset, sizeof(float));
            std::memcpy(&state.will, component + kWillOffset, sizeof(float));
            std::memcpy(&state.skill, component + kSkillOffset, sizeof(float));
            std::memcpy(&state.age, component + kAgeOffset, sizeof(float));
            std::memcpy(&state.alignment, component + kAlignmentOffset, sizeof(float));
            std::memcpy(&state.fatness, component + kFatnessOffset, sizeof(float));
            std::memcpy(&state.auxiliary, component + kAuxiliaryOffset, sizeof(float));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = {};
            return false;
        }
        if (!state.IsSane())
        {
            state = {};
            return false;
        }
        return true;
    }

    bool HeroMorphComponent::CaptureBoneScaleState(
        void* nativeThing,
        HeroBoneScaleState& state) noexcept
    {
        state = {};
        ResolvedPresentation presentation;
        if (!ResolvePresentation(nativeThing, presentation))
        {
            return false;
        }
        __try
        {
            state.valid = true;
            for (std::int32_t index = 0;
                 index < presentation.scaleCount;
                 ++index)
            {
                const NativeVector3& scale = presentation.scales[index];
                if (IsIdentity(scale))
                {
                    continue;
                }
                if (state.count >= HeroBoneScaleState::MaximumEntries)
                {
                    state = {};
                    return false;
                }
                state.entries[state.count++] = BoneScaleAt(
                    presentation, index);
            }
            return state.IsSane();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = {};
            return false;
        }
    }

    bool HeroMorphComponent::ApplyValues(
        void* nativeThing,
        const HeroMorphState& state) noexcept
    {
        if (!state.IsSane())
        {
            return false;
        }
        auto* const component = static_cast<std::uint8_t*>(
            FindMorphComponent(nativeThing));
        if (component == nullptr)
        {
            return false;
        }
        __try
        {
            component[kUpdateRequestedOffset] = 0;
            std::memcpy(component + kStrengthOffset, &state.strength, sizeof(float));
            std::memcpy(component + kBerserkOffset, &state.berserk, sizeof(float));
            std::memcpy(component + kWillOffset, &state.will, sizeof(float));
            std::memcpy(component + kSkillOffset, &state.skill, sizeof(float));
            std::memcpy(component + kAgeOffset, &state.age, sizeof(float));
            std::memcpy(component + kAlignmentOffset, &state.alignment, sizeof(float));
            std::memcpy(component + kFatnessOffset, &state.fatness, sizeof(float));
            std::memcpy(component + kAuxiliaryOffset, &state.auxiliary, sizeof(float));
            component[kChildOffset] = state.child ? 1u : 0u;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            component[kUpdateRequestedOffset] = 0;
            return false;
        }
    }

    bool HeroMorphComponent::ApplyBoneScaleState(
        void* nativeThing,
        const HeroBoneScaleState& state,
        std::uint32_t* matchedCount) noexcept
    {
        if (matchedCount != nullptr)
        {
            *matchedCount = 0;
        }
        if (!state.IsSane())
        {
            return false;
        }
        ResolvedPresentation presentation;
        if (!ResolvePresentation(nativeThing, presentation))
        {
            return false;
        }
        auto* const module = reinterpret_cast<std::uint8_t*>(
            GetModuleHandleW(nullptr));
        auto* const setAddress = module == nullptr
            ? nullptr
            : module + kSetBoneTranslationScaleRva;
        if (!HasSignature(setAddress, kSetBoneScaleSignature))
        {
            return false;
        }
        using SetFunction = void(__thiscall*)(
            void*, std::uint32_t, std::uint32_t, NativeVector3);
        auto* const setScale = reinterpret_cast<SetFunction>(setAddress);
        __try
        {
            // A remote actor can be reused for a later appearance baseline.
            // Restore every currently materialized scale before applying the
            // authoritative non-identity set so removed scales cannot linger.
            for (std::int32_t index = 0;
                 index < presentation.scaleCount;
                 ++index)
            {
                const auto* const referenceBone =
                    presentation.referenceBones +
                    static_cast<std::size_t>(index) * kReferenceBoneStride;
                NativeFName boneName;
                std::memcpy(&boneName, referenceBone, sizeof(boneName));
                setScale(
                    presentation.massBoneScaling,
                    boneName.index,
                    boneName.number,
                    {});
            }
            std::uint32_t matched = 0;
            for (std::size_t index = 0; index < state.count; ++index)
            {
                const HeroBoneScale& bone = state.entries[index];
                if (bone.boneIndex >= presentation.referenceBoneCount)
                {
                    return false;
                }
                const auto* const referenceBone =
                    presentation.referenceBones +
                    static_cast<std::size_t>(bone.boneIndex) *
                        kReferenceBoneStride;
                NativeFName boneName;
                std::memcpy(&boneName, referenceBone, sizeof(boneName));
                setScale(
                    presentation.massBoneScaling,
                    boneName.index,
                    boneName.number,
                    {bone.x, bone.y, bone.z});
                ++matched;
            }
            if (matchedCount != nullptr)
            {
                *matchedCount = matched;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}
