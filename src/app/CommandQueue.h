/**
 * CommandQueue.h
 * Single-consumer FIFO for commands received from the server.
 *
 * Why a dedicated queue instead of calling BarrierManager directly:
 *   - MQTT inbound messages arrive on the network thread; we need them to
 *     execute on the GUI thread in a defined order.
 *   - A late OPEN arriving while we are still lowering the gate must
 *     cancel the pending lower and re-raise immediately. Handling that
 *     centrally keeps BarrierManager free of server-specific policy.
 *   - Replays / retries / rate-limits can be added later without touching
 *     the producer side.
 *
 * Usage:
 *   connect(queue, &CommandQueue::commandReady,
 *           this,  &Application::runCommand);
 *   queue->enqueue({Kind::Open, "barrier-1", 10});
 */

#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include "types/MessageTypes.h"

class CommandQueue : public QObject {
    Q_OBJECT
public:
    enum class Kind : int {
        Open,
        Close,
    };

    struct Command {
        Kind       kind             = Kind::Open;
        QString    barrier_id;
        QString    session_id;
        OpenReason reason           = OpenReason::Unknown;
        int        auto_close_sec   = 10;
        bool       play_sound       = true;
        QString    sound_file;
    };

    explicit CommandQueue(QObject* parent = nullptr);

    // Producer side — safe to call from any thread (uses a queued signal).
    void enqueue(Command cmd);

    // Cancel every pending OPEN/CLOSE targeting `barrier_id`. Useful when
    // a higher-priority command supersedes queued work (e.g. reopen).
    int  cancelFor(const QString& barrier_id);

    // Inspect without popping — pure debug aid.
    [[nodiscard]] int  size() const { return queue_.size(); }

signals:
    // Consumer side: fired on this object's thread for each dequeued
    // command, one at a time. The handler MUST call `ack()` when done to
    // release the next command. Use `ackAndDrop()` to skip the remaining
    // queue atomically (unused today, kept for future retry policy).
    void commandReady(const CommandQueue::Command& cmd);

public slots:
    void ack();

private slots:
    void enqueueMain(CommandQueue::Command cmd);   // thread hop target
    void pump();

private:
    QQueue<Command> queue_;
    bool            busy_ = false;
};

Q_DECLARE_METATYPE(CommandQueue::Command)
