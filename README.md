## kblightd

Kblightd turns on the keyboard backlight after a keypress and turns it back off after a specified amount of seconds.  
It should work properly without using any arguments.  

    KBLIGHTD :: Handle keyboard backlight
    Optional arguments:
        -t TIME       Time in MS to turn LED on after keypress
        -c            Device class
        -i            Device id
        -b            Brightness level

## Compile

    $ make
    $ ./kblightd
