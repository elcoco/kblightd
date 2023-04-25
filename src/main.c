#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include <stdarg.h>     // va_list
#include <errno.h>
#include <string.h>

#include <linux/input.h>
#include <systemd/sd-bus.h>

// https://stackoverflow.com/questions/22749444/listening-to-keyboard-events-without-consuming-them-in-x11-keyboard-hooking
// doc: https://www.x.org/releases/X11R7.7/doc/libXtst/recordlib.html
// example: https://github.com/TheRealHex/hecs-JWK/blob/97ba761b5461d5cf9050c75fa6cbaabac340c0c5/scan-x11.c
//

// Kernel Input API
// doc:     https://www.kernel.org/doc/html/v4.17/input/input.html
// api:     https://www.kernel.org/doc/html/latest/driver-api/input.html
// example: https://github.com/freedesktop-unofficial-mirror/evtest


#define INP_DEV_DIR "/dev/input"
#define INP_DEV_DISCOVER_PATH "/proc/bus/input/devices"

#define THREAD_PAUSE_MS 500

// defaults
#define TIME_LED_ON 15
#define LED_DEV_CLASS "leds"
#define LED_DEV_ID "tpacpi::kbd_backlight"
#define LED_DEV_BRIGHTNESS 1
#define LED_DEV_OFF 0

int do_debug = 0;

// end program if true
// must be global for signal handler to function
int do_stop = 0;

// watch thread
pthread_t t_thread_id;


// device info for keyboard backlight
// should be something like /sys/class/<class>/<id>/brightness
struct Device {
	char class[64];
	char id[64];
    int brightness;
    char path[256];
};

// is passed to thread to check led state
struct ThreadState {
    // seconds since last keypress
    time_t t_press;

    // seconds since led was turned on
    time_t t_led_on;
    int led_state;

    // is true when error in thread occured
    int thread_err;

    struct Device led_dev;
};

void debug(char* fmt, ...)
{
    if (do_debug) {
        va_list ptr;
        va_start(ptr, fmt);
        vfprintf(stdout, fmt, ptr);
        va_end(ptr);
    }
}

void info(char* fmt, ...)
{
    va_list ptr;
    va_start(ptr, fmt);
    vfprintf(stdout, fmt, ptr);
    va_end(ptr);
}

void error(char* fmt, ...)
{
    va_list ptr;
    va_start(ptr, fmt);
    vfprintf(stderr, fmt, ptr);
    va_end(ptr);
}

int get_dev_path(char *buf)
{
    /* Search for keyboard devices */
    // Why is KB identifier 120013?? => https://unix.stackexchange.com/questions/74903/explain-ev-in-proc-bus-input-devices-data
    char dev_str[8192] = "";
    char dev_id[] = "120013";

    FILE *fd = fopen(INP_DEV_DISCOVER_PATH, "r");

    if (fd == NULL) {
        error("Failed to open path: %s\n", INP_DEV_DISCOVER_PATH);
        return -1;
    }
    int rd = fread(&dev_str, 1, sizeof(dev_str)-1, fd);

    dev_str[rd] = '\0';

    char *handlers = dev_str;
    char *evs = dev_str;

    do {
        handlers = strstr(evs, "Handlers=");

        if (handlers)
            evs = strstr(handlers, "EV=");
    }
    while (evs && handlers && strncmp(evs+3, dev_id, strlen(dev_id)) != 0);

    if (!evs || !handlers) {
        info("Device not found\n");
        return -1;
    }

    char *start;
    char *end;

    if ((start = strstr(handlers, "event"))) {
        if ((end = strstr(start, " "))) {
            start[end-start] = '\0';
            sprintf(buf, "%s/%s", INP_DEV_DIR, start); 
            return 0;
        }
    }

    error("Malformed input device name\n");
    return -1;
}

int logind_set_led(struct Device *led_dev, int brightness)
{
    /* Systemd function stolen from brightnessctl */
	sd_bus *bus = NULL;
	int r = sd_bus_default_system(&bus);
	if (r < 0) {
		error("Can't connect to system bus: %s\n", strerror(-r));
		return -1;
	}

    info("Setting LED to %d\n", brightness);

	r = sd_bus_call_method(bus,
			       "org.freedesktop.login1",
			       "/org/freedesktop/login1/session/auto",
			       "org.freedesktop.login1.Session",
			       "SetBrightness",
			       NULL,
			       NULL,
			       "ssu",
			       led_dev->class,
			       led_dev->id,
			       brightness);

	sd_bus_unref(bus);

	if (r < 0) {
		error("Failed to set brightness: %s\n", strerror(-r));
        return -1;
    }

	return 0;
}

int set_led(struct Device *dev, int value)
{
    info("Setting LED to %d\n", value);

    FILE *fd = fopen(dev->path, "w");
    if (fd == NULL) {
        error("Failed to open path: %s\n", dev->path);
        return -1;
    }
    fprintf(fd, "%d", value);

    fclose(fd);
    return 0;
}

void* watch_thread(void* arg)
{
    /* Checks if n seconds have passed without keypress */
    struct ThreadState *ts = arg;
    while (!do_stop) {
        time_t t_diff = time(NULL) - ts->t_press;
        if (ts->led_state && t_diff > ts->t_led_on) {
            ts->led_state = 0;

            //if (logind_set_led(&(ts->led_dev), LED_DEV_OFF) < 0) {
            if (set_led(&(ts->led_dev), LED_DEV_OFF) < 0) {
                ts->thread_err = 1;
                do_stop = 1;
                break;
            }
        }
        usleep(THREAD_PAUSE_MS*1000);
    }
    return NULL;
}

void cleanup()
{
    /* Stop and join thread */
    debug("cleaning up\n");
    do_stop = 1;
    pthread_join(t_thread_id, NULL);
}

void intHandler(int _)
{
    /* Handle ctrl-C */
    cleanup();
    info("Exitting ...\n");
    exit(0);
}

void show_help()
{
    printf("KBLIGHTD :: Handle keyboard backlight\n"
           "Optional arguments:\n"
           "    -t TIME       Time in MS to turn LED on after keypress\n"
           "    -c            Device class\n"
           "    -i            Device id\n"
           "    -b            Brightness level\n");
}

int err_stoi(char *str)
{
    /* String to unsigned integer, return -1 on error */
    int ret = 0;
    char *c = str;
    for (int i=strlen(str)-1 ; i>=0 ; i--, c++) {
        if (*c >='0' && *c <= '9')
            ret += (*c-48) * pow(10, i);
        else
            return -1;
    }
    return ret;
}

int parse_args(struct Device* led_dev, struct ThreadState *ts, int argc, char **argv)
{
    int option;

    while((option = getopt(argc, argv, "c:i:t:b:hD")) != -1) {
        switch (option) {
            case 'c':
                strcpy(led_dev->class, optarg);
                break;
            case 'i':
                strcpy(led_dev->id, optarg);
                break;
            case 't':
                if ((ts->t_led_on = err_stoi(optarg)) < 0) {
                    error("ERROR: %s is not a number!\n", optarg);
                    return -1;
                }
                break;
            case 'b':
                if ((led_dev->brightness = err_stoi(optarg)) < 0) {
                    error("ERROR: %s is not a number!\n", optarg);
                    return -1;
                }
                break;
            case 'D': 
                do_debug = 1;
                break;
            case ':': 
                error("Option needs a value\n"); 
                return -1;
            case 'h': 
                show_help();
                return -1;
            case '?': 
                show_help();
                return -1;
       }
    }
    return 1;
}

int get_keypress(FILE *fd)
{
    /* Scan for keypress events
     * doc: https://www.kernel.org/doc/html/v4.17/input/event-codes.html#input-event-codes
     * event.type = event group
     * event.code = specific event eg: KEY_A etc
     * event.value: 1 = keypress
     * event.value: 0 = keyrelease
     */
    struct input_event ev;
    fread(&ev, sizeof(struct input_event), 1, fd);

    if (ev.type == EV_KEY && ev.value == 1) {
        debug("%d :: Keypress detected :: code=%d\n", ev.time, ev.code);
        return 1;
    }
    return 0;
}


int handle_keypress(struct ThreadState *ts)
{
    /* Decide if LED should be turned on after keypress */
    ts->t_press = time(NULL);
    if (!ts->led_state) {
        ts->led_state = 1;
        return set_led(&(ts->led_dev), ts->led_dev.brightness);
        //return logind_set_led(&(ts->led_dev), ts->led_dev.brightness);
    }
    return 0;
}

struct ThreadState init_state()
{
    struct ThreadState ts = { .t_press = -1,
                              .led_state = 0,
                              .t_led_on = TIME_LED_ON,
                              .thread_err = 0 };

    strcpy(ts.led_dev.class, LED_DEV_CLASS);
    strcpy(ts.led_dev.id, LED_DEV_ID);
    ts.led_dev.brightness = LED_DEV_BRIGHTNESS;

    return ts;
}

int main(int argc, char **argv) {
    signal(SIGINT, intHandler);
    signal(SIGTERM, intHandler);

    struct ThreadState ts = init_state();

    if (parse_args(&(ts.led_dev), &ts, argc, argv) < 0)
        return 1;

    char inp_dev[256] = "";
    if (get_dev_path(inp_dev) < 0) {
        error("Failed to find keyboard device\n");
        return 1;
    }

    debug("Found keyboard device: %s\n", inp_dev);

    sprintf(ts.led_dev.path, "/sys/class/%s/%s/brightness", ts.led_dev.class, ts.led_dev.id);

    FILE *fd;
    if ((fd = fopen(inp_dev, "r")) == NULL) {
        error("Error opening device file: %s\n", inp_dev);
        return 1;
    }

    if (errno == EACCES && getuid() != 0) {
        error("You do not have access to %s. Try running as root instead.\n", inp_dev);
        return 1;
    }

    // start led in off state
    //if (logind_set_led(&(ts.led_dev), LED_DEV_OFF) < 0)
    if (set_led(&(ts.led_dev), LED_DEV_OFF) < 0)
        return 1;

    // create watch thread that turns off LED after n seconds
    pthread_create(&t_thread_id, NULL, &watch_thread, &ts);

    info("Start scanning for keyboard events...\n");
    while (!do_stop) {

        // blocking fd read
        get_keypress(fd);

        if (handle_keypress(&ts) < 0)
            goto cleanup_on_err;

    }

    // check if we had error in thread
    if (ts.thread_err)
        goto cleanup_on_err;

    cleanup();
    return 0;

cleanup_on_err:
        cleanup();
        return 1;
}
