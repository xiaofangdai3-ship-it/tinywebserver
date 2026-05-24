#include "../include/timer.h"
#include "../include/logger.h"

TimerWheel::TimerWheel() : curSlot_(0), lastTick_(time(nullptr)) {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        slots_[i] = new TimerNode();
    }
}

TimerWheel::~TimerWheel() {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        deleteSlot(slots_[i]);
    }
}

void TimerWheel::addNode(int slot, TimerNode* node) {
    TimerNode* head = slots_[slot];
    node->prev = head;
    node->next = head->next;
    if (head->next) {
        head->next->prev = node;
    }
    head->next = node;
}

void TimerWheel::removeNode(TimerNode* node) {
    if (!node || !node->prev) return;
    node->prev->next = node->next;
    if (node->next) {
        node->next->prev = node->prev;
    }
    node->prev = nullptr;
    node->next = nullptr;
}

void TimerWheel::deleteSlot(TimerNode* head) {
    TimerNode* cur = head;
    while (cur) {
        TimerNode* next = cur->next;
        delete cur;
        cur = next;
    }
}

TimerNode* TimerWheel::addTimer(int sockfd, int timeout_ms) {
    int ticks = timeout_ms / 1000;
    if (ticks < 1) ticks = 1;

    int rotation = ticks / SLOT_COUNT;
    int slot = (curSlot_ + (ticks % SLOT_COUNT)) % SLOT_COUNT;

    TimerNode* node = new TimerNode();
    node->sockfd = sockfd;
    node->rotation = rotation;
    node->expireSlot = slot;

    addNode(slot, node);

    LOG_DEBUG("Timer added: sockfd=%d, slot=%d, rotation=%d, timeout=%dms",
              sockfd, slot, rotation, timeout_ms);
    return node;
}

void TimerWheel::delTimer(TimerNode* timer) {
    if (!timer) return;
    removeNode(timer);
    LOG_DEBUG("Timer deleted: sockfd=%d", timer->sockfd);
    delete timer;
}

void TimerWheel::resetTimer(TimerNode* timer, int timeout_ms) {
    if (!timer) return;

    int sockfd = timer->sockfd;
    delTimer(timer);
    addTimer(sockfd, timeout_ms);
}

int TimerWheel::tick(int expired_fds[], int max_count) {
    time_t now = time(nullptr);
    int elapsed = (int)(now - lastTick_);
    if (elapsed <= 0) return 0;

    int count = 0;
    for (int i = 0; i < elapsed && count < max_count; ++i) {
        TimerNode* head = slots_[curSlot_];
        TimerNode* cur = head->next;

        while (cur && count < max_count) {
            TimerNode* next = cur->next;

            if (cur->rotation > 0) {
                cur->rotation--;
            } else {

                expired_fds[count++] = cur->sockfd;
                LOG_DEBUG("Timer expired: sockfd=%d at slot=%d", cur->sockfd, curSlot_);
                removeNode(cur);
                delete cur;
            }
            cur = next;
        }

        curSlot_ = (curSlot_ + 1) % SLOT_COUNT;
    }

    lastTick_ = now;
    if (count > 0) {
        LOG_INFO("Timer tick: %d connections expired", count);
    }
    return count;
}
