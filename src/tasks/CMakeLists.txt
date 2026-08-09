# wz::tasks -- fork-join task API (#293). S0: serial-inline backend only. The
# worker pool + work-stealing deque (S1) and the Win32 fibre backend (S2, which
# lands in platform/win32) slot in behind the same task.h signatures later.
target_sources(window_engine PRIVATE
    task.cpp
)
