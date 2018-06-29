#pragma once

#include <mbgl/actor/mailbox.hpp>
#include <mbgl/actor/message.hpp>
#include <mbgl/actor/actor_ref.hpp>
#include <mbgl/util/noncopyable.hpp>

#include <memory>
#include <future>
#include <type_traits>
#include <cassert>

namespace mbgl {

template <class Object>
class EstablishedActor;

template <class Object>
class Actor;

template <class Object>
class AspiringActor {
public:
    AspiringActor() : mailbox(std::make_shared<Mailbox>()) {
        assert(!mailbox->isOpen());
    }
    
    AspiringActor(const AspiringActor&) = delete;
    
    ActorRef<std::decay_t<Object>> self() {
        return ActorRef<std::decay_t<Object>>(object(), mailbox);
    }
    
    template <typename Fn, class... Args>
    void invoke(Fn fn, Args&&... args) {
        mailbox->push(actor::makeMessage(object(), fn, std::forward<Args>(args)...));
    }

    template <typename Fn, class... Args>
    auto ask(Fn fn, Args&&... args) {
        // Result type is deduced from the function's return type
        using ResultType = typename std::result_of<decltype(fn)(Object, Args...)>::type;

        std::promise<ResultType> promise;
        auto future = promise.get_future();
        mailbox->push(actor::makeMessage(std::move(promise), object(), fn, std::forward<Args>(args)...));
        return future;
    }

private:
    std::shared_ptr<Mailbox> mailbox;
    std::aligned_storage_t<sizeof(Object)> objectStorage;
    
    Object& object() {
        return *reinterpret_cast<Object *>(&objectStorage);
    }
    
    friend class EstablishedActor<Object>;
    friend class Actor<Object>;
};

template <class Object>
class EstablishedActor {
public:
    template <class... Args>
    EstablishedActor(Scheduler& scheduler, AspiringActor<Object>& parent_, Args&& ... args)
    :   parent(parent_) {
        emplaceObject(std::forward<Args>(args)...);
        parent.mailbox->open(scheduler);
    }

    template <class ArgsTuple, std::size_t ArgCount = std::tuple_size<ArgsTuple>::value>
    EstablishedActor(Scheduler& scheduler, AspiringActor<Object>& parent_, ArgsTuple&& args)
    :   parent(parent_) {
        emplaceObject(std::forward<ArgsTuple>(args), std::make_index_sequence<ArgCount>{});
        parent.mailbox->open(scheduler);
    }
    
    EstablishedActor(const EstablishedActor&) = delete;

    ~EstablishedActor() {
        parent.mailbox->close();
        parent.object().~Object();
    }
    
private:
    // Enabled for Objects with a constructor taking ActorRef<Object> as the first parameter
    template <typename U = Object, class... Args, typename std::enable_if<std::is_constructible<U, ActorRef<U>, Args...>::value>::type * = nullptr>
    void emplaceObject(Args&&... args_) {
        new (&parent.objectStorage) Object(parent.self(), std::forward<Args>(args_)...);
    }

    // Enabled for plain Objects
    template <typename U = Object, class... Args, typename std::enable_if<!std::is_constructible<U, ActorRef<U>, Args...>::value>::type * = nullptr>
    void emplaceObject(Args&&... args_) {
        new (&parent.objectStorage) Object(std::forward<Args>(args_)...);
    }
    
    // Used to expand a tuple of arguments created by Actor<Object>::captureArguments()
    template <class ArgsTuple, std::size_t... I>
    void emplaceObject(ArgsTuple args, std::index_sequence<I...>) {
        emplaceObject(std::move(std::get<I>(std::forward<ArgsTuple>(args)))...);
    }

    AspiringActor<Object>& parent;
};

/*
    An `Actor<O>` is an owning reference to an asynchronous object of type `O`: an "actor".
    Communication with an actor happens via message passing: you send a message to the object
    (using `invoke`), passing a pointer to the member function to call and arguments which
    are then forwarded to the actor.

    The actor receives messages sent to it asynchronously, in a manner defined its `Scheduler`.
    To store incoming messages before their receipt, each actor has a `Mailbox`, which acts as
    a FIFO queue. Messages sent from actor S to actor R are guaranteed to be processed in the
    order sent. However, relative order of messages sent by two *different* actors S1 and S2
    to R is *not* guaranteed (and can't be: S1 and S2 may be acting asynchronously with respect
    to each other).

    An `Actor<O>` can be converted to an `ActorRef<O>`, a non-owning value object representing
    a (weak) reference to the actor. Messages can be sent via the `Ref` as well.

    It's safe -- and encouraged -- to pass `Ref`s between actors via messages. This is how two-way
    communication and other forms of collaboration between multiple actors is accomplished.

    It's safe for a `Ref` to outlive its `Actor` -- the reference is "weak", and does not extend
    the lifetime of the owning Actor, and sending a message to a `Ref` whose `Actor` has died is
    a no-op. (In the future, a dead-letters queue or log may be implemented.)

    Construction and destruction of an actor is currently synchronous: the corresponding `O`
    object is constructed synchronously by the `Actor` constructor, and destructed synchronously
    by the `~Actor` destructor, after ensuring that the `O` is not currently receiving an
    asynchronous message. (Construction and destruction may change to be asynchronous in the
    future.) The constructor of `O` is passed an `ActorRef<O>` referring to itself (which it
    can use to self-send messages), followed by the forwarded arguments passed to `Actor<O>`.

    Please don't send messages that contain shared pointers or references. That subverts the
    purpose of the actor model: prohibiting direct concurrent access to shared state.
*/
template <class Object>
class Actor {
public:
    template <class... Args>
    Actor(Scheduler& scheduler, Args&&... args)
        : target(scheduler, parent, std::forward<Args>(args)...) {}

    Actor(const Actor&) = delete;

    ActorRef<std::decay_t<Object>> self() {
        return parent.self();
    }

    operator ActorRef<std::decay_t<Object>>() {
        return self();
    }

    template <typename Fn, class... Args>
    void invoke(Fn fn, Args&&... args) {
        parent.invoke(std::move(fn), std::forward<Args>(args)...);
    }

    template <typename Fn, class... Args>
    auto ask(Fn fn, Args&&... args) {
        return parent.ask(std::move(fn), std::forward<Args>(args)...);
    }

private:
    AspiringActor<Object> parent;
    EstablishedActor<Object> target;
};

} // namespace mbgl
