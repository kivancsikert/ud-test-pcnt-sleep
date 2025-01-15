# Ugly Duckling Test: pulse counting in light sleep

A simple test project to figure out the most efficient mode to count pulses while staying in light sleep
as much as possible.
This is for low-frequency pulse counting, like counting pulses from a water meter.

The way it works is:

- While awake count pulses via interrupts on edges.
- Light sleep on idle is enabled, so the device should go to sleep for most of its life.
- When going to sleep, the device should be configured to wake up on the opposite of the current state of the pulse pin.
- When waking up, the device should check if we woke up because of a pulse, and then handle it accordingly.
