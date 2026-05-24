#ifndef TIMER_H
#define TIMER_H

#include <ctime>
#include <cstdlib>

struct TimerNode {
    int expireSlot;
    int rotation;
    int sockfd;
    TimerNode* prev;
    TimerNode* next;

    TimerNode() : expireSlot(0), rotation(0), sockfd(-1), prev(nullptr), next(nullptr) {}
};

class TimerWheel {
public:
    static const int SLOT_COUNT = 60;

    TimerWheel();
    ~TimerWheel();

    TimerNode* addTimer(int sockfd, int timeout_ms);

    void delTimer(TimerNode* timer);

    int tick(int expired_fds[], int max_count);

    void resetTimer(TimerNode* timer, int timeout_ms);

private:
    TimerNode* slots_[SLOT_COUNT];
    int curSlot_;
    time_t lastTick_;

    void addNode(int slot, TimerNode* node);
    void removeNode(TimerNode* node);
    void deleteSlot(TimerNode* head);
};

#endif
