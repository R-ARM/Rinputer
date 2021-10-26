#include <stdio.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#define ANALPATH "/dev/input/by-path/platform-analog-event-joystick"
#define KEYPATH "/dev/input/by-path/platform-gpio-keys-event-joystick"
#define PWRPATH "/dev/input/by-path/platform-ff180000.i2c-platform-rk805-pwrkey-event"
#define JACKPATH "/dev/input/by-path/platform-rk817-sound-event"

int outfd;
int keyfd;
int analfd;
int pwrfd;
int jackfd;

#define SPK 0
#define HP 1

int currentDevice = SPK;

pthread_t handleAnal;
pthread_t handlePwr;
pthread_t handleJack;

struct stateful
{
	signed int hpVolume;
	signed int spVolume;
	signed int brightness;
};

struct stateful settings;

struct keys
{
	int volume_up;
	int volume_down;

};

struct keys keybinds = {
	.volume_up = BTN_TRIGGER_HAPPY1,
	.volume_down = BTN_TRIGGER_HAPPY2,
};

void signalHandler(int sig)
{
	if (sig == SIGINT)
	{
		FILE *settingsFile = fopen("/etc/rinputer.dat", "w");
		fwrite(&settings, sizeof(struct stateful), 1, settingsFile);
		fclose(settingsFile);
		exit(0);
	}
}

// yoinked directly from kernel documentation
void emit(int fd, int type, int code, int val)
{
	struct input_event ie;

	ie.type = type;
	ie.code = code;
	ie.value = val;
	
	/* timestamp values below are ignored */
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;

	write(fd, &ie, sizeof(ie));
}

static void setup_abs(int fd, int fd2, int unsigned chan)
{
	if (ioctl(fd, UI_SET_ABSBIT, chan))
		perror("UI_SET_ABSBIT");

	struct uinput_abs_setup s =
	{
		.code = chan,
		.absinfo = { .minimum = 0,  .maximum = 0 },
	};
	
	ioctl(fd2, EVIOCGABS(chan), &s.absinfo);

	if (ioctl(fd, UI_ABS_SETUP, &s))
		perror("UI_ABS_SETUP");
}


//unsigned int doneWaiting = 1;

//void *waitTwoSeconds(void *unused)
//{
//	doneWaiting = 0;
//	sleep(2);
//	doneWaiting = 1;
//}

//void do_poweroff()
//{
//	printf("POWEROFF\n");
//}

void do_suspend()
{
	printf("SUSPEND\n");
}

void *pwrHandler(void *unused)
{
	int rd, i;
	struct input_event ev[4];
//	pthread_t waiter;
	while(1)
	{
		rd = read(pwrfd, ev, sizeof(struct input_event) * 4);
		if (rd > 0)
		{
			for (i = 0; i < rd / sizeof(struct input_event) * 4; i++)
			{
				if (ev[i].type == EV_KEY && ev[i].code == KEY_POWER)
				{
					if (ev[i].value == 0)
					{
						close(pwrfd);
//						if (doneWaiting == 1)
//							do_poweroff();
//						else
							do_suspend();
						pwrfd = open(PWRPATH, O_RDONLY);
					}
//					else if (ev[i].value == 1)
//					{
//						if (doneWaiting == 1)
//						{
//							pthread_create(&waiter, NULL, waitTwoSeconds, NULL);
//						}
//					}
				}
			}
		}
	}
}

void updateBrightness()
{
	int max;
	FILE *fmax;
	FILE *fbl;

	fbl = fopen("/sys/class/backlight/backlight/brightness", "w");
	fmax = fopen("/sys/class/backlight/backlight/max_brightness", "r");
	fscanf(fmax, "%d", &max);

	int tmp = settings.brightness * (max/100);

	fprintf(fbl, "%d", tmp);
	printf("1 %d\n", tmp);
	printf("2 %d\n", settings.brightness);

	fclose(fmax);
	fclose(fbl);
}

void setSound(int volume, char* mux, int changeMux)
{
	if (changeMux)
	{
		char muxbuffer[64];
		sprintf(muxbuffer, "/usr/bin/amixer sset \"Playback Mux\" %s", mux);
		system(muxbuffer);
	}

	char volbuffer[64];
	sprintf(volbuffer, "/usr/bin/amixer sset Master %d%%", volume);
	printf("%s\n", volbuffer);
	system(volbuffer);

}

void enableHeadphones()
{
	printf("HEADPHONES\n");
	currentDevice = HP;
	setSound(settings.hpVolume, "HP", 1);
}

void enableSpeakers()
{
	printf("SPEAKERS\n");
	currentDevice = SPK;
	setSound(settings.spVolume, "SPK", 1);
}

void *jackHandler(void *unused)
{
	int rd;
	int i = 0;
	struct input_event ev[4];

	// if user boots up with headphones inserted
	ioctl(jackfd, EVIOCGSW(sizeof(i)), &i);
	if(i & (1<<SW_HEADPHONE_INSERT) > 0)
		enableHeadphones();
	else
		enableSpeakers();

	while(1)
	{
		rd = read(jackfd, ev, sizeof(struct input_event) * 4);
		if (rd > 0)
		{
			for (i = 0; i < rd / sizeof(struct input_event) * 4; i++)
			{
				if (ev[i].type == EV_SW && ev[i].code == SW_HEADPHONE_INSERT)
				{
					if (ev[i].value == 1) // jack inserted
					{
						enableHeadphones();
					}
					else // jack removed
					{
						enableSpeakers();
					}
				}
			}
		}
	}
}

void *analHandler(void *unused)
{

	int rd, i;
	struct input_event ev[4];

	while(1)
	{
		rd = read(analfd, ev, sizeof(struct input_event) * 4);
		if (rd > 0)
		{
			for (i = 0; i < rd / sizeof(struct input_event) * 4; i++)
			{
				emit(outfd, ev[i].type, ev[i].code, ev[i].value);
			}
		}
	}
}

#define min(X, Y) (((X) < (Y)) ? (X) : (Y))
#define max(X, Y) (((X) > (Y)) ? (X) : (Y))

int main(void)
{
	outfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	keyfd = open(KEYPATH, O_RDONLY);
	analfd = open(ANALPATH, O_RDONLY);
	pwrfd = open(PWRPATH, O_RDONLY);
	jackfd = open(JACKPATH, O_RDONLY);

	struct uinput_setup usetup;

	ioctl(outfd, UI_SET_EVBIT, EV_KEY);

	ioctl(outfd, UI_SET_KEYBIT, BTN_DPAD_UP);	// dpad up
	ioctl(outfd, UI_SET_KEYBIT, BTN_DPAD_DOWN);	// dpad down
	ioctl(outfd, UI_SET_KEYBIT, BTN_DPAD_LEFT);	// dpad left
	ioctl(outfd, UI_SET_KEYBIT, BTN_DPAD_RIGHT);	// dpad right

	ioctl(outfd, UI_SET_KEYBIT, BTN_NORTH);		// x
	ioctl(outfd, UI_SET_KEYBIT, BTN_SOUTH);		// b
	ioctl(outfd, UI_SET_KEYBIT, BTN_WEST);		// y
	ioctl(outfd, UI_SET_KEYBIT, BTN_EAST);		// a

	ioctl(outfd, UI_SET_KEYBIT, BTN_TL);		// L1
	ioctl(outfd, UI_SET_KEYBIT, BTN_TR);		// R1
	
	ioctl(outfd, UI_SET_KEYBIT, BTN_TR2);		// L2
	ioctl(outfd, UI_SET_KEYBIT, BTN_TL2);		// R2

	ioctl(outfd, UI_SET_KEYBIT, BTN_SELECT);
	ioctl(outfd, UI_SET_KEYBIT, BTN_START);

	ioctl(outfd, UI_SET_EVBIT, EV_ABS);
	setup_abs(outfd, analfd, ABS_X);
	setup_abs(outfd, analfd, ABS_Y);
	setup_abs(outfd, analfd, ABS_RX);
	setup_abs(outfd, analfd, ABS_RY);

	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor = 0x1234;
	usetup.id.product = 0x5678;
	strcpy(usetup.name, "Rinputer");

	ioctl(outfd, UI_DEV_SETUP, &usetup);
	ioctl(outfd, UI_DEV_CREATE);
	
	ioctl(keyfd, EVIOCGRAB, 1);
	ioctl(analfd, EVIOCGRAB, 1);
	ioctl(pwrfd, EVIOCGRAB, 1);
	ioctl(jackfd, EVIOCGRAB, 1);

	signal(SIGINT, signalHandler);
	FILE *settingsFile = fopen("/etc/rinputer.dat", "r");
	if (settingsFile == NULL)
	{
		settings.hpVolume = 10;
		settings.spVolume = 10;
		settings.brightness = 90;
	}
	else
	{
		fread(&settings, sizeof(struct stateful), 1, settingsFile);
		fclose(settingsFile);
		updateBrightness();
	}


	pthread_create(&handleJack, NULL, jackHandler, NULL);
	pthread_create(&handleAnal, NULL, analHandler, NULL);
	pthread_create(&handlePwr, NULL, pwrHandler, NULL);

	int rd, i;
	struct input_event ev[4];
	int vd_held = 0;
	int vu_held = 0;

	while(1)
	{
		rd = read(keyfd, ev, sizeof(struct input_event) * 4);
		if (rd > 0)
		{
			for (i = 0; i < rd / sizeof(struct input_event) * 4; i++)
			{
				if (ev[i].type == EV_KEY && ev[i].code == keybinds.volume_down && ev[i].value == 1)
				{
					if (vu_held == 0)
					{
						if (currentDevice == SPK)
						{
							settings.spVolume -= 5;
							settings.spVolume = max(settings.spVolume, 0);
							setSound(settings.spVolume, "", 0);
						}
						else
						{
							settings.hpVolume -= 5;
							settings.hpVolume = max(settings.hpVolume, 0);
							setSound(settings.hpVolume, "", 0);
						}
							
					}
					else
					{
						settings.brightness -= 5;
						settings.brightness = max(settings.brightness, 10);
						updateBrightness();
					}
					vd_held = 1;
				}
				else if (ev[i].type == EV_KEY && ev[i].code == keybinds.volume_up && ev[i].value == 1)
				{
					if (vd_held == 0)
					{
						settings.brightness += 5;
						settings.brightness = min(settings.brightness, 100);
						updateBrightness();
					}
					else
					{
						if (currentDevice == SPK)
						{
							settings.spVolume += 5;
							settings.spVolume = min(settings.spVolume, 100);
							setSound(settings.spVolume, "", 0);
						}
						else
						{
							settings.hpVolume -= 5;
							settings.hpVolume = min(settings.hpVolume, 100);
							setSound(settings.hpVolume, "", 0);
						}
					}

					vu_held = 1;
				}
				else if (ev[i].type == EV_KEY && ev[i].code == keybinds.volume_down && ev[i].value == 0)
					vd_held = 0;
				else if (ev[i].type == EV_KEY && ev[i].code == keybinds.volume_up && ev[i].value == 0)
					vu_held = 0;
				else
					emit(outfd, ev[i].type, ev[i].code, ev[i].value);
			}
		}
	}
}
