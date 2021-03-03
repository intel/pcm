# Global PCM Event      events

|     Event Name              |                                Description                                  |
|-----------------------------|-----------------------------------------------------------------------------|
| Measurement_Interval_in_us  |             How many us elapsed to complete the last measurement            |
| Number_of_sockets           |                     Number of CPU sockets in the system                     |


# Core Counters per socket

OS_ID is the OS assigned ID of the logical CPU core and denotes the socket id, core id and thread id.

The events below are followed by the same {socket="socket id",core="core id",thread="thread id"} as
the OS_ID of their section with source="socket/core/thread" appended that denotes what the quantity
of the event accounts for.

For example Instructions_Retired_Any{socket="0",core="1",thread="1",source="core"} refers to 
Instructions_Retired_Any for socket 0, core 1, thread 1, and accounts for the total instructions
retired of the specified core.

|          Event                                 |                   Description                                |
|------------------------------------------------|--------------------------------------------------------------|
|   Instructions_Retired_Any                     |   Total number of Retired instructions                       |
|   Clock_Unhalted_Thread                        |                                                              |
|   Clock_Unhalted_Ref                           |   Counts the number of reference cycles that the thread is   |
|                                                |   not in a halt state. The thread enters the halt state when |
|                                                |   it is running the HLT instruction. This event is not       |
|                                                |   affected by thread frequency changes but counts as if the  |
|                                                |   thread is running at the maximum frequency all the time.   |
|   L3_Cache_Misses                              |   Total number of L3 Cache misses                            |
|   L3_Cache_Hits                                |   Total number of L3 Cache hits                              |
|   L2_Cache_Misses                              |   Total number of L2 Cache misses                            |
|   L2_Cache_Hits                                |   Total number of L3 Cache hits                              |
|   L3_Cache_Occupancy                           |   Computes L3 Cache Occupancy                                |
|   SMI_Count                                    |   SMI (System Management Interrupt) count                    |
|   Invariant_TSC                                |   Calculates the invariant TSC clocks (the invariant TSC     |
|                                                |   means that the TSC continues at a fixed rate regardless of |
|                                                |   the C-state or frequency of the processor as long as the   |
|                                                |   processor remains in the ACPI S0 state.                    |
|   Thermal_Headroom                             |   Celsius degrees before reaching TjMax temperature          |
|   CStateResidency                              |   This is the percentage of time that the core (or the whole |
|                                                |   package) spends in a particular level of C-state           |                                                                                                                                            |

References:

https://software.intel.com/content/www/us/en/develop/articles/intel-performance-counter-monitor.html
https://software.intel.com/content/dam/develop/external/us/en/documents-tps/325384-sdm-vol-3abcd.pdf - Chapter 18 Performance Monitoring