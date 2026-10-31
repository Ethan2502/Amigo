## Alarm and timer

The 3x3 keypad opens the alarm screen with key 1 and the timer screen with key 2.
On the home screen, keys 3-9 still play their assigned tones.

The alarm screen lets you set the software clock (keys 1/2), set the daily alarm
(keys 4/5), toggle the alarm (key 7), and return home or dismiss a ringing alarm
(key 9). The timer screen adds 1, 5, or 10 minutes (keys 1/2/3), starts or pauses
(key 5), resets (key 8), and returns home or dismisses a ringing timer (key 9).

Alarm settings are stored in ESP32 Preferences. The clock is software-based and
starts at 00:00 after power loss; set it again from the alarm screen. Timer and
clock state are not retained across power loss. This firmware has not yet been
flashed to hardware.