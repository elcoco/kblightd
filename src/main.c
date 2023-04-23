#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>

#include <X11/XKBlib.h>
#include <X11/extensions/record.h>

#include <systemd/sd-bus.h>

// https://stackoverflow.com/questions/22749444/listening-to-keyboard-events-without-consuming-them-in-x11-keyboard-hooking
// doc: https://www.x.org/releases/X11R7.7/doc/libXtst/recordlib.html
// example: https://github.com/TheRealHex/hecs-JWK/blob/97ba761b5461d5cf9050c75fa6cbaabac340c0c5/scan-x11.c

#define TIME_LED_ON 5
#define LED_VAL 1

#define DEV_CLASS "leds"
#define DEV_ID "tpacpi::kbd_backlight"
#define THREAD_PAUSE_MS 500

// watch thread
pthread_t t_thread_id;

// X11 display
Display *dpy;

// is passed to thread to check led state
struct State {
    time_t t_press;
    int led_state;
    int t_led_on;
    int is_stopped;
};

struct State s = { .t_press = -1,
                   .led_state = 1,
                   .t_led_on = TIME_LED_ON,
                   .is_stopped = 0};

struct Device {
	char class[64];
	char id[64];
};

struct Device dev;

void cleanup()
{
    printf("Exitting ...\n");

    s.is_stopped = 1;
    pthread_join(t_thread_id, NULL);

    //XCloseDisplay(dpy);
    exit(0);
}

int logind_set_led(struct Device *d, unsigned int cur)
{
    /* Systemd function stolen from brightnessctl */
	sd_bus *bus = NULL;
	int r = sd_bus_default_system(&bus);
	if (r < 0) {
		fprintf(stderr, "Can't connect to system bus: %s\n", strerror(-r));
		return -1;
	}

	r = sd_bus_call_method(bus,
			       "org.freedesktop.login1",
			       "/org/freedesktop/login1/session/auto",
			       "org.freedesktop.login1.Session",
			       "SetBrightness",
			       NULL,
			       NULL,
			       "ssu",
			       d->class,
			       d->id,
			       cur);

	sd_bus_unref(bus);

	if (r < 0) {
		fprintf(stderr, "Failed to set brightness: %s\n", strerror(-r));
        cleanup();
    }

	return r >= 0;
}

void set_lights_on()
{
    // triggered by key_pressed_cb callback
    s.led_state = 1;
    printf("Turning light on!\n");
    logind_set_led(&dev, LED_VAL);
}

void set_lights_off(struct State *s)
{
    // triggered by thread
    s->led_state = 0;
    printf("Turning light off!\n");
    logind_set_led(&dev, 0);
}

void key_pressed_cb(XPointer arg, XRecordInterceptData *d) {
    if (d->category != XRecordFromServer)
        return;
    
    //int key = ((unsigned char*) d->data)[1];
    int type = ((unsigned char*) d->data)[0] & 0x7F;
    int repeat = d->data[2] & 1;

    if(!repeat && type == KeyPress) {
        s.t_press = time(NULL);
        if (!s.led_state)
            set_lights_on();
    }
    XRecordFreeData(d);
}

void* watch_thread(void* arg)
{
    /* Checks if n seconds have passed without keypress */
    struct State *s = arg;
    while (!s->is_stopped) {
        time_t t_diff = time(NULL) - s->t_press;
        if (s->led_state && t_diff > s->t_led_on)
            set_lights_off(s);
        usleep(THREAD_PAUSE_MS*1000);
    }
    return NULL;
}


void intHandler(int)
{
    /* Handle ctrl-C */
    cleanup();
}

void show_help()
{
    printf("KBLIGHTD :: Handle keyboard backlight\n"
           "Optional arguments:\n"
           "    -t TIME       Time in MS to turn LED on after keypress\n"
           "    -c            Device class!\n"
           "    -i            Device id!\n");
}

int err_stoi(char *str)
{
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

int parse_args(struct Device* dev, struct State *s, int argc, char **argv)
{
    int option;

    while((option = getopt(argc, argv, "c:i:t:h")) != -1){ //get option from the getopt() method
        switch (option) {
            case 'c':
                strcpy(dev->class, optarg);
                break;
            case 'i':
                strcpy(dev->id, optarg);
                break;
            case 't':
                int t = err_stoi(optarg);
                if (t < 0) {
                    printf("ERROR: %s is not a number!\n", optarg);
                    return -1;
                }
                s->t_led_on = t;
                break;
            case ':': 
                printf("Option needs a value\n"); 
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


int main(int argc, char **argv) {
    // listen for ctrl-C
    signal(SIGINT, intHandler);

    strcpy(dev.class, DEV_CLASS);
    strcpy(dev.id, DEV_ID);

    if (parse_args(&dev, &s, argc, argv) < 0)
        exit(1);




    // create thread that turns off LED after n seconds
    pthread_create(&t_thread_id, NULL, &watch_thread, &s);

    XRecordRange* rr;
    XRecordClientSpec rcs;
    XRecordContext rc;

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL) {
        printf("Failed to open connection to X server\n");
        cleanup();
    }

    printf("Start scanning...\n");

    rr = XRecordAllocRange();
    rr->device_events.first = KeyPress;
    rr->device_events.last = ButtonReleaseMask;
    rcs = XRecordAllClients;
    rc = XRecordCreateContext (dpy, 0, &rcs, 1, &rr, 1);
    XFree (rr);
    XRecordEnableContext(dpy, rc, key_pressed_cb, NULL);

    return 0;
}
