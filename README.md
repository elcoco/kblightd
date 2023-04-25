## kblightd

Kblightd turns on the keyboard backlight after a keypress and turns it back off after a specified amount of seconds.  
It should work out of the box on thinkpads without using any arguments.  
For other keyboards you may have to specify the device class and id.  

Daemon must run as a privileged user.  
This is easily done by using the installed systemd unit file.  

    KBLIGHTD :: Handle keyboard backlight
    Optional arguments:
        -t TIME       Time in MS to turn LED on after keypress
        -c            Device class
        -i            Device id
        -b            Brightness level

## Compile and install

    $ make
    $ sudo make install
    $ sudo systemctl enable kblightd.service
    $ sudo systemctl start kblightd.service
