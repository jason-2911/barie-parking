#include "app/CommandQueue.h"

#include <QDebug>
#include <QMetaType>

CommandQueue::CommandQueue(QObject* parent) : QObject(parent) {
    qRegisterMetaType<CommandQueue::Command>("CommandQueue::Command");
}

void CommandQueue::enqueue(Command cmd) {
    // Always hop onto this object's thread before touching the queue.
    QMetaObject::invokeMethod(this, "enqueueMain", Qt::QueuedConnection,
                               Q_ARG(CommandQueue::Command, cmd));
}

void CommandQueue::enqueueMain(Command cmd) {
    qInfo().nospace() << "[Cmd] enqueue kind="
                      << (cmd.kind == Kind::Open ? "OPEN" : "CLOSE")
                      << " barrier=" << cmd.barrier_id
                      << " session=" << cmd.session_id
                      << " depth=" << (queue_.size() + 1);
    queue_.enqueue(std::move(cmd));
    if (!busy_) pump();
}

int CommandQueue::cancelFor(const QString& barrier_id) {
    int removed = 0;
    auto it = queue_.begin();
    while (it != queue_.end()) {
        if (it->barrier_id == barrier_id) {
            it = queue_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        qInfo() << "[Cmd] cancelled" << removed << "pending cmd(s) for" << barrier_id;
    }
    return removed;
}

void CommandQueue::ack() {
    busy_ = false;
    pump();
}

void CommandQueue::pump() {
    if (busy_ || queue_.isEmpty()) return;
    busy_ = true;
    Command c = queue_.dequeue();
    emit commandReady(c);
}
