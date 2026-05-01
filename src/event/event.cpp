#include <event/event.h>

namespace wz::event
{
    wz::core::containers::MPSCRingBuffer<Event, MAX_EVENTS> event_queue;
}