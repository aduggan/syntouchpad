syntouchpad
===========

Userspace daemon for controlling TouchPad settings and palm detection. Works in conjunction
with the Synaptics RMI4 driver. Currently, this daemon needs version 1.8.1-rmihid.0 or later. This
version of the driver has support for the sysfs files in 
/sys/devices/sensor00/sensor00.fn11/suppress, suppress_highw, and sensor00.fn30/suppress. 
The driver needs to be compiled with CONFIG_RMI4_CONTROL.

This daemon has two functions, to control touchpad settings and to perform accidental contact 
mitigation. 

Accidental Contact Mitigation
-----------------------------
Accidental contact mitigation is performed by listening for key events from all keyboards connected
to the system and suppressing pointing motion for a certain timeout in order to prevent the cursor
from moving when palms bursh against the touchpad while typing. In addition to listening for key events, the daemon also sets the threshold in the driver will suppress large contacts which are most
likely palms. Accidental contact mitigations settings are configurable in the settings (see below).
The setting value ranges from 0 to 7, with 0 being minimal suppression and 7 being the most
aggressive.


Settings
--------
The daemon's settings are stored in a file located in /data/system/syntouchpad. The format of the 
file is very simple, with one entry per line. The first line is the accidental contact mitigation
setting with a value ranging from 0 - 7 and the second line controls the physical click of the
clickpad (1 to suppress the click, 0 otherwise). The intention is that an external tool such as a
control panel will modify the entries in this file.

