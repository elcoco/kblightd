#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#include <linux/input.h>

#include "utils.h"

// Kernel Input API
// doc:     https://www.kernel.org/doc/html/v4.17/input/input.html
// api:     https://www.kernel.org/doc/html/latest/driver-api/input.html
// example: https://github.com/freedesktop-unofficial-mirror/evtest


// defaults
#define LED_DEV_DIR "/sys/class/leds"
#define INP_DEV_DIR "/dev/input"
#define INP_DEV_DISCOVER_PATH "/proc/bus/input/devices"

#define TIME_LED_ON 15
#define LED_DEV_ID "tpacpi::kbd_backlight"
#define LED_DEV_BRIGHTNESS 1
#define LED_DEV_OFF 0

#define THREAD_PAUSE_MS 500

// end program if true
// global for signal handler to signal program exit
int do_stop = 0;

// thread that turns LED off when no input has occured for N seconds
// global so we can join threads from cleanup() on SIGTERM
pthread_t t_thread_id;

// is passed to thread to check led state
struct State {
    // seconds since last keypress
    time_t t_press;

    // seconds since led was turned on
    time_t t_led_on;
    
    int led_state;

    // is true when error in thread occured
    int thread_err;

    char inp_dev_path[256];
    char led_dev_path[256];

    int led_brightness;
};


int get_kb_inp_dev(char *buf, char *inp_dev_dir, char *dev_discover_path)
{
    /* Search for keyboard devices */
    // Why is KB identifier 120013?? => https://unix.stackexchange.com/questions/74903/explain-ev-in-proc-bus-input-devices-data
    char dev_str[8192] = "";
    char dev_id[] = "120013";

    FILE *fd = fopen(dev_discover_path, "r");

    if (fd == NULL) {
        error("Failed to open path: %s\n", dev_discover_path);
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
        error("Input device not found\n");
        return -1;
    }

    char *start;
    char *end;

    if ((start = strstr(handlers, "event"))) {
        if ((end = strstr(start, " "))) {
            start[end-start] = '\0';
            sprintf(buf, "%s/%s", inp_dev_dir, start); 
            return 0;
        }
    }

    error("Malformed input device name\n");
    return -1;
}

int get_led_dev(char *buf, char* search_dir)
{
    /* Find keyboard LED device in search dir */
    struct dirent *e;

    DIR *dir = opendir(search_dir);
    if (dir == NULL) {
        error("Failed to open directory: %s\n", search_dir);
        return -1;
    }

    while ((e = readdir(dir)) != NULL) {
        if (strstr(e->d_name, "kbd_backlight")) {
            sprintf(buf, "%s/%s/brightness", search_dir, e->d_name);
            break;
        }
    }

    closedir(dir);

    if (e == NULL) {
        error("Failed to find kb led device in: %s\n", search_dir);
        return -1;
    }

    return 0;
}

int set_led(char *path, int value)
{
    info("Setting LED to %d\n", value);

    FILE *fd = fopen(path, "w");
    if (fd == NULL) {
        error("Failed to open path: %s\n", path);
        return -1;
    }
    fprintf(fd, "%d", value);

    fclose(fd);
    return 0;
}

void* watch_thread(void* arg)
{
    /* Checks if n seconds have passed without keypress */
    struct State *s = arg;
    while (!do_stop) {
        time_t t_diff = time(NULL) - s->t_press;
        if (s->led_state && t_diff > s->t_led_on) {
            s->led_state = 0;

            if (set_led(s->led_dev_path, LED_DEV_OFF) < 0) {
                s->thread_err = 1;
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
           "    -l            LED device path\n"
           "    -i            Input device path\n"
           "    -b            Brightness level\n"
           "    -D            Debugging\n");
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


int handle_keypress(struct State *s)
{
    /* Decide if LED should be turned on after keypress */
    s->t_press = time(NULL);
    if (!s->led_state) {
        s->led_state = 1;
        return set_led(s->led_dev_path, s->led_brightness);
    }
    return 0;
}

struct State state_init()
{
    struct State s = { .t_press = -1,
                              .led_state = 0,
                              .t_led_on = TIME_LED_ON,
                              .thread_err = 0 };

    strcpy(s.led_dev_path, "");
    strcpy(s.inp_dev_path, "");

    s.led_brightness = LED_DEV_BRIGHTNESS;

    return s;
}

int parse_args(struct State *s, int argc, char **argv)
{
    int option;

    while((option = getopt(argc, argv, "l:i:t:b:hD")) != -1) {
        switch (option) {
            case 'l':
                strcpy(s->led_dev_path, optarg);
                break;
            case 'i':
                strcpy(s->inp_dev_path, optarg);
                break;
            case 't':
                if ((s->t_led_on = err_stoi(optarg)) < 0) {
                    error("ERROR: %s is not a number!\n", optarg);
                    return -1;
                }
                break;
            case 'b':
                if ((s->led_brightness = err_stoi(optarg)) < 0) {
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

int inp_dev_loop(struct State *s)
{
    /* Start looking for input device events */
    FILE *fd;
    if ((fd = fopen(s->inp_dev_path, "r")) == NULL) {
        error("Error opening device file: %s\n", s->inp_dev_path);
        return -1;
    }

    if (errno == EACCES && getuid() != 0) {
        error("You do not have access to %s. Try running as root instead.\n", s->inp_dev_path);
        return -1;
    }

    info("Start scanning for keyboard events...\n");
    while (!do_stop) {

        // blocking fd read
        get_keypress(fd);

        if (handle_keypress(s) < 0)
            return -1;
    }

    // check if we had error in thread
    if (s->thread_err)
        return -1;

    return 0;
}

int main(int argc, char **argv) {
    signal(SIGINT, intHandler);
    signal(SIGTERM, intHandler);

    struct State s = state_init();

    if (parse_args(&s, argc, argv) < 0)
        return 1;

    // auto discover device paths if not given as cli args
    if (strlen(s.inp_dev_path) == 0) {
        if (get_kb_inp_dev(s.inp_dev_path, INP_DEV_DIR, INP_DEV_DISCOVER_PATH) < 0) {
            error("Failed to find keyboard device\n");
            return 1;
        }
    }
    info("Using keyboard device: %s\n", s.inp_dev_path);

    if (strlen(s.led_dev_path) == 0) {
        if (get_led_dev(s.led_dev_path, LED_DEV_DIR) < 0) {
            error("Failed to find keyboard LED device\n");
            return 1;
        }
    }
    info("Using keyboard LED device path: %s\n", s.led_dev_path);

    // start led in off state
    if (set_led(s.led_dev_path, LED_DEV_OFF) < 0)
        return 1;

    // create watch thread that turns off LED after n seconds
    pthread_create(&t_thread_id, NULL, &watch_thread, &s);

    // start checking for input device events
    if (inp_dev_loop(&s) < 0) {
        cleanup();
        return 1;
    }

    cleanup();
    return 0;
}
