#include <stdio.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <string.h>

#define ANALPATH "/dev/input/by-path/platform-analog-event-joystick"
#define KEYPATH "/dev/input/by-path/platform-gpio-keys-event-joystick"
#define PWRPATH "/dev/input/by-path/platform-ff180000.i2c-platform-rk805-pwrkey-event"

int outfd;
int keyfd;
int analfd;
int pwrfd;
int jackfd;

pthread_t handleAnal;
pthread_t handlePwr;

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

int main(void)
{
	outfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	keyfd = open(KEYPATH, O_RDONLY);
	analfd = open(ANALPATH, O_RDONLY);
	pwrfd = open(PWRPATH, O_RDONLY);

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

	pthread_create(&handleAnal, NULL, analHandler, NULL);
	pthread_create(&handlePwr, NULL, pwrHandler, NULL);

	int rd, i;
	struct input_event ev[4];

	while(1)
	{
		rd = read(keyfd, ev, sizeof(struct input_event) * 4);
		if (rd > 0)
		{
			for (i = 0; i < rd / sizeof(struct input_event) * 4; i++)
			{
				emit(outfd, ev[i].type, ev[i].code, ev[i].value);
			}
		}
	}
}
