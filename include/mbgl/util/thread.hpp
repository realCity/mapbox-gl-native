#pragma once

#include <mbgl/actor/actor.hpp>
#include <mbgl/actor/mailbox.hpp>
#include <mbgl/actor/scheduler.hpp>
#include <mbgl/util/platform.hpp>
#include <mbgl/util/run_loop.hpp>
#include <mbgl/util/util.hpp>

#include <cassert>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

namespace mbgl {
namespace util {

// Manages a thread with `Object`.

// Upon creation of this object, it launches a thread and creates an object of type `Object`
// in that thread. When the `Thread<>` object is destructed, the destructor waits
// for thread termination. The `Thread<>` constructor blocks until the thread and
// the `Object` are fully created, so after the object creation, it's safe to obtain the
// `Object` stored in this thread. The thread created will always have low priority on
// the platforms that support setting thread priority.
//
// The following properties make this class different from `ThreadPool`:
//
// - Only one thread is created.
// - `Object` will live in a single thread, providing thread affinity.
// - It is safe to use `ThreadLocal` in an `Object` managed by `Thread<>`
// - A `RunLoop` is created for the `Object` thread.
// - `Object` can use `Timer` and do asynchronous I/O, like wait for sockets events.
//
template<class Object>
class Thread : public Scheduler {
public:
    template <class... Args>
    Thread(const std::string& name, Args&&... args) {
        std::unique_ptr<std::promise<void>> running_ = std::make_unique<std::promise<void>>();
        running = running_->get_future();
        
        // Pre-create a "holding" mailbox for this actor, whose messages are
        // guaranteed not to be consumed until we explicitly call start(), which
        // we'll do on the target thread, once its RunLoop and Object instance
        // are ready.
        // Meanwhile, this allows us to immediately provide ActorRef using this
        // mailbox to queue any messages that come in before the thread is ready.
        std::shared_ptr<Mailbox> mailbox_ = std::make_shared<Mailbox>();
        mailbox = mailbox_;
        
        auto tuple = std::make_tuple(std::forward<Args>(args)...);

        thread = std::thread([
            this,
            name,
            tuple,
            sharedMailbox = std::move(mailbox_),
            runningPromise = std::move(running_)
        ] {
            platform::setCurrentThreadName(name);
            platform::makeThreadLowPriority();

            util::RunLoop loop_(util::RunLoop::Type::New);
            loop = &loop_;

            Actor<Object>* actor = emplaceActor(
                std::move(sharedMailbox),
                std::move(tuple),
                std::make_index_sequence<std::tuple_size<decltype(tuple)>::value>{});
            
            // Replace the NoopScheduler on the mailbox with the RunLoop to
            // begin actually processing messages.
            actor->mailbox->start(this);

            runningPromise->set_value();
            
            loop->run();
            loop = nullptr;
        });
    }

    ~Thread() override {
        if (paused) {
            resume();
        }

        std::promise<void> joinable;
        
        running.wait();

        // Kill the actor, so we don't get more
        // messages posted on this scheduler after
        // we delete the RunLoop.
        loop->invoke([&] {
            reinterpret_cast<const Actor<Object>*>(&actorStorage)->~Actor<Object>();
            joinable.set_value();
        });

        joinable.get_future().get();

        loop->stop();
        thread.join();
    }

    // Returns a non-owning reference to `Object` that
    // can be used to send messages to `Object`. It is safe
    // to the non-owning reference to outlive this object
    // and be used after the `Thread<>` gets destroyed.
    ActorRef<std::decay_t<Object>> actor() {
        // The actor->object reference we provide here will not actually be
        // valid until the child thread constructs Actor<Object> into
        // actorStorage using "placement new".
        // We guarantee that the object reference isn't actually used by
        // using the NoopScheduler to prevent messages to this mailbox from
        // being processed until after the actor has been constructed.
        auto actor = reinterpret_cast<Actor<Object>*>(&actorStorage);
        return ActorRef<std::decay_t<Object>>(actor->object, mailbox);
    }

    // Pauses the `Object` thread. It will prevent the object to wake
    // up from events such as timers and file descriptor I/O. Messages
    // sent to a paused `Object` will be queued and only processed after
    // `resume()` is called.
    void pause() {
        MBGL_VERIFY_THREAD(tid);

        assert(!paused);

        paused = std::make_unique<std::promise<void>>();
        resumed = std::make_unique<std::promise<void>>();

        auto pausing = paused->get_future();

        running.wait();

        loop->invoke(RunLoop::Priority::High, [this] {
            auto resuming = resumed->get_future();
            paused->set_value();
            resuming.get();
        });

        pausing.get();
    }

    // Resumes the `Object` thread previously paused by `pause()`.
    void resume() {
        MBGL_VERIFY_THREAD(tid);

        assert(paused);

        resumed->set_value();

        resumed.reset();
        paused.reset();
    }

private:
    MBGL_STORE_THREAD(tid);

    void schedule(std::weak_ptr<Mailbox> mailbox_) override {
        assert(loop);
        loop->schedule(mailbox_);
    }
    
    template <typename ArgsTuple, std::size_t... I>
    Actor<Object>* emplaceActor(std::shared_ptr<Mailbox> sharedMailbox, ArgsTuple args, std::index_sequence<I...>) {
        return new (&actorStorage) Actor<Object>(std::move(sharedMailbox), std::move(std::get<I>(std::forward<ArgsTuple>(args)))...);
    }

    std::weak_ptr<Mailbox> mailbox;
    std::aligned_storage<sizeof(Actor<Object>)> actorStorage;

    std::thread thread;

    std::future<void> running;
    
    std::unique_ptr<std::promise<void>> paused;
    std::unique_ptr<std::promise<void>> resumed;

    util::RunLoop* loop = nullptr;
};

} // namespace util
} // namespace mbgl
