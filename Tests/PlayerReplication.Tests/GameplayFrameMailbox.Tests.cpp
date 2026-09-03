#include "Game/Runtime/GameplayFrameMailbox.h"
#include "Core/GameThread/Native/SimulationFrameFunction.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

int RunGameplayFrameMailboxTests()
{
    using fable::game::GameplayFrameMailbox;
    using Layout = fable::core::game_thread::native::SimulationFrameFunction;
    int failures = 0;
    std::array<std::uint8_t, 8> prefix{};
    std::copy(std::begin(Layout::Prefix), std::end(Layout::Prefix), prefix.begin());
    constexpr std::uint8_t end[] = {0x5E, 0xC9, 0xC3};
    constexpr std::uint8_t caller[] = {0xE8, 0x9F, 0xD0, 0xFF, 0xFF};
    failures += !Layout::Matches(prefix.data(), end, caller);
    prefix[0] = 0xE9;
    failures += Layout::Matches(prefix.data(), end, caller);
    prefix[0] = Layout::Prefix[0];
    constexpr std::uint8_t wrongReturn[] = {0xC2, 0x04, 0x00};
    failures += Layout::Matches(prefix.data(), wrongReturn, caller);
    constexpr std::uint8_t wrongCaller[] = {0xE8, 0x9E, 0xD0, 0xFF, 0xFF};
    failures += Layout::Matches(prefix.data(), end, wrongCaller);
    failures += Layout::Matches(nullptr, end, caller);

    GameplayFrameMailbox mailbox;
    failures += mailbox.ConsumeDeparture();
    mailbox.WorldReady();
    mailbox.WorldReady();
    mailbox.AutomationIdle();
    mailbox.Reload();
    mailbox.Background(true);
    failures += !mailbox.KeyPressed(1, false);
    failures += !mailbox.KeyPressed(2, true);
    const auto first = mailbox.Take();
    failures += !first.worldReady || !first.automationIdle || !first.reload || !first.background;
    failures += first.keyCount != 2 || first.keys[0].code != 1 ||
        first.keys[0].shift || first.keys[1].code != 2 || !first.keys[1].shift;
    const auto empty = mailbox.Take();
    failures += empty.worldReady || empty.automationIdle || empty.reload || empty.keyCount != 0;
    failures += !empty.background; // Focus is state, not a one-shot command.
    mailbox.Background(false);
    failures += mailbox.Take().background;
    mailbox.Departed();
    mailbox.Departed();
    failures += !mailbox.ConsumeDeparture() || mailbox.ConsumeDeparture();

    for (unsigned int key = 0; key < 16; ++key) failures += !mailbox.KeyPressed(key, false);
    failures += mailbox.KeyPressed(16, true);
    const auto full = mailbox.Take();
    failures += full.keyCount != 16;
    for (unsigned int key = 0; key < 16; ++key) failures += full.keys[key].code != key;

    // Producer never runs native work or grows a queue during a stalled frame.
    // A single owner drains complete ordered batches; no key is duplicated.
    constexpr unsigned int total = 2000;
    std::atomic_bool done{false};
    std::thread producer([&] {
        for (unsigned int key = 0; key < total; ++key)
        {
            while (!mailbox.KeyPressed(key, (key & 1) != 0)) std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    unsigned int received = 0;
    for (;;)
    {
        const bool completed = done.load(std::memory_order_acquire);
        const auto batch = mailbox.Take();
        for (std::size_t index = 0; index < batch.keyCount; ++index)
        {
            failures += batch.keys[index].code != received ||
                batch.keys[index].shift != ((received & 1) != 0);
            ++received;
        }
        if (completed && batch.keyCount == 0) break;
        std::this_thread::yield();
    }
    producer.join();
    failures += received != total;
    mailbox.WorldReady();
    mailbox.Departed();
    mailbox.Background(true);
    mailbox.Reset();
    const auto reset = mailbox.Take();
    failures += reset.worldReady || reset.background || mailbox.ConsumeDeparture();
    return failures;
}
