## kblightd

Kblightd turns on the keyboard backlight after a keypress and turns it back off after a specified amount of seconds.  

Keyboard LED device is discovered in:

    /sys/class/leds

Keyboard input device is discovered from:

    /proc/bus/input/devices

And should exist in:

    /dev/input/event[0-9]

Custom paths can be given by using the following args:

    -l path/to/led/device
    -i path/to/input/device

Daemon must run as a privileged user.  
This is easily achieved by using the provided systemd unit file.  

    KBLIGHTD :: Handle keyboard backlight
    Optional arguments:
        -t TIME       Time in MS to turn LED on after keypress
        -l            LED device path
        -i            Input device path
        -b            Brightness level

## Compile and install

    $ make
    $ sudo make install
    $ sudo systemctl enable kblightd.service
    $ sudo systemctl start kblightd.service
