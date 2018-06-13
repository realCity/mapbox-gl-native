#pragma once

#include <memory>
#include <mutex>
#include <queue>

namespace mbgl {

class Scheduler;
class Message;

class Mailbox : public std::enable_shared_from_this<Mailbox> {
public:
    Mailbox(Scheduler&);

    void push(std::unique_ptr<Message>);

    void close();
    void receive();
    
    // Replace this mailbox's scheduler. Effectively allows a mailbox to be
    // created on one thread and moved to another one.
    void setScheduler(Scheduler* scheduler_);

    static void maybeReceive(std::weak_ptr<Mailbox>);

private:
    Scheduler* scheduler;

    std::recursive_mutex receivingMutex;
    std::mutex pushingMutex;

    bool closed { false };

    std::mutex queueMutex;
    std::queue<std::unique_ptr<Message>> queue;
};

} // namespace mbgl
