#pragma once

#include <atomic>
#include <memory>

namespace fx
{

/** Passes objects from the message thread to the audio thread without the
    audio thread ever allocating, freeing or waiting.

    - Message thread: publish() a new object; collect() regularly (10-30 Hz)
      to delete objects the audio thread has finished with.
    - Audio thread: acquire() at the start of each block switches to the
      newest object, if any, and returns the one to use.

    There is one slot each way. The audio thread only switches once the
    message thread has collected the previous object, so nothing is ever
    overwritten or freed on the audio thread; until collect() runs, a new
    object simply waits. Only one thread at a time may act as the message
    thread. */
template <typename T>
class Handoff
{
public:
    explicit Handoff (std::unique_ptr<T> initial = std::make_unique<T>()) : current (initial.release()) {}

    ~Handoff()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete current;
    }

    Handoff (const Handoff&) = delete;
    Handoff& operator= (const Handoff&) = delete;

    // Message thread ----------------------------------------------------------

    /** Must not be null. Replaces any object still waiting (which the audio
        thread never saw, so it's deleted here). */
    void publish (std::unique_ptr<T> next)
    {
        collect();
        delete pending.exchange (next.release(), std::memory_order_acq_rel);
    }

    void collect()
    {
        delete retired.exchange (nullptr, std::memory_order_acquire);
    }

    /** Only while acquire() can't run (e.g. in prepareToPlay): applies any
        waiting object and returns the current one for the caller to modify. */
    T& takeOver()
    {
        collect();
        if (auto* next = pending.exchange (nullptr, std::memory_order_acquire))
        {
            delete current;
            current = next;
        }
        return *current;
    }

    // Audio thread ------------------------------------------------------------

    T& acquire() noexcept
    {
        if (retired.load (std::memory_order_acquire) == nullptr)
        {
            if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                retired.store (current, std::memory_order_release);
                current = next;
            }
        }
        return *current;
    }

    /** The object from the last acquire(), without checking for a newer one. */
    T& get() noexcept { return *current; }

private:
    T* current;
    std::atomic<T*> pending { nullptr };
    std::atomic<T*> retired { nullptr };
};

} // namespace fx
