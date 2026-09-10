#include "stdafx.h"
#include "Core/Time/FrameTimerScheduler.h"

#include <chrono>
#include <vector>

namespace Core::Time
{
    FrameTimerScheduler& FrameTimerScheduler::Instance()
    {
        // Heap-allocate the singleton and intentionally never destroy it.
        // Subsystems call Kill() during static destruction (e.g. CSlideHelpMgr's
        // destructor, which runs when CNewUISystem is torn down) AFTER this
        // function-local static would otherwise have been destroyed, so a stack
        // static here leaves a use-after-free (AV reading freed m_timers at
        // offset 0x1C on shutdown). Leaking one tiny object at process exit is
        // the standard, safe way to guarantee the scheduler outlives every
        // subsystem that references it during teardown.
        static FrameTimerScheduler* instance = new FrameTimerScheduler;
        return *instance;
    }

    std::uint64_t FrameTimerScheduler::NowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    void FrameTimerScheduler::SetRepeating(TimerId id, unsigned intervalMs, Callback callback)
    {
        m_timers[id] = Timer{ intervalMs, NowMs() + intervalMs, std::move(callback) };
    }

    void FrameTimerScheduler::Kill(TimerId id)
    {
        m_timers.erase(id);
    }

    void FrameTimerScheduler::Tick()
    {
        const std::uint64_t now = NowMs();

        // Collect the due ids first, then fire. A callback may register or kill
        // timers (e.g. a buff timer kills itself on expiry), so we must not hold
        // an iterator into m_timers across a callback.
        std::vector<TimerId> due;
        for (const auto& [id, timer] : m_timers)
        {
            if (now >= timer.nextDueMs)
            {
                due.push_back(id);
            }
        }

        for (TimerId id : due)
        {
            auto it = m_timers.find(id);
            if (it == m_timers.end())
            {
                continue; // killed by an earlier callback this tick
            }

            // Reschedule before firing so a callback that re-registers or kills
            // this id wins over the reschedule. No catch-up: the next due time is
            // measured from now, matching WM_TIMER coalescing.
            it->second.nextDueMs = now + it->second.intervalMs;

            Callback callback = it->second.callback;
            callback();
        }
    }
}
