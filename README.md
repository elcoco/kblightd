## kblightd

Kblightd turns on the keyboard backlight after a keypress and turns it back off after a specified amount of seconds.  
It should work out of the box on thinkpads without using any arguments.  
For other keyboards you may have to specify the device class and id.  

Permissions are obtained by using the systemd-logind API.  
Keypress monitoring depends on X11 and doesn't work with wayland.  

    KBLIGHTD :: Handle keyboard backlight
    Optional arguments:
        -t TIME       Time in MS to turn LED on after keypress
        -c            Device class
        -i            Device id
        -b            Brightness level

## Compile and run

    $ make
    $ make install
    $ /usr/local/bin/kblightd
