# EMI-Modbus

We adapted the original modbus library and created a lightweight version of it. Ours is also more flexible, but only offers support for the RTU configuration, and only for the `modbus_read_input_registers` call.

# How to run

1. Make sure you have git, make and gcc.
1. Install [https://github.com/eclipse/paho.mqtt.c](paho.mqtt.c):
    1. `git clone git@github.com:eclipse/paho.mqtt.c.git`
    2. `cd paho.mqtt.c`
    3. `make install`
    
1. Install [https://man7.org/linux/man-pages/man3/libsystemd.3.html](libsystemd-dev):
    1. `sudo apt install libsystemd-dev`
1. Build the software: `make`
1. Run it: `build/emi-read mqtt://<mqtt-host> <mqtt-user> <mqtt-pwd>`

# Future steps

1. Code cleanup.
1. Add support for write (so that EMI address can be changed. Might be useful for residential buildings).
1. Allow further customisation via args:
    1. Serial device file name
    1. Interval for instant values polling
    1. Values to poll
    0. We may consider going next level with a yaml config file instead.
1. Contrib flow and guidelines
1. install signal handlers for mqtt logoff and signaling.
