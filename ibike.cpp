// This samples ADC channels on BCM3208 and speed on one of GPIO pins
// Compile with g++ -Wall -o ibike ibike.cpp -lrt -lm -lbcm2835

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sched.h>
#include <signal.h>
#include <bcm2835.h>
#include <sys/sendfile.h>
#include <linux/reboot.h>
#include <sys/reboot.h>

const int GPIOPIN = 4; // GPIO pin for speed sensor
const int STARTSWITCH = 18; // GPIO pin for start switch
const int STARTLED = 23; // GPIO pin for start LED (white)
const int STOPSWITCH = 24; // GPIO pin for stop switch
const int STOPLED = 25; // GPIO pin for stop LED (red)
const float WHEELCIRC = 2.255; // wheel circumference in m for speed calculation
//const float WHEELCIRC = 2.027; // MTB 47-559
//const float WHEELCIRC = 2.250; // marathon+ tour 42-622
//const float WHEELCIRC = 2.255; // marathon+ 47-622
//const float WHEELCIRC = 1.491; // pony 47-406

const float MULTIPLIER[8] = {12.5, 12.5, 12.5, 12.5, 12.5, 12.5, 12.5, 12.5}; // calibration multipliers for individual channels
//const float MULTIPLIER[8] = {1, 1, 1, 1, 1, 1, 1, 1}; // calibration multipliers for individual channels
const int CALSAMPLES = 2000; // number of samples for calibration average

float VIn;
int a2dChannel;
int adcval;
float offset[7];
int sampling_interval = 2; //length of sampling interval in ms

int sensVal = 0;
int sensStr = 1;
float speed = 0;
float pSpeed = 0;
float dist = 0;
long revs = 0;
unsigned long lastFall = 0;
unsigned long lastInt = 160000000;
unsigned long fade = 0;
unsigned long smooth = 0;


// Signal handling
static bool keepRunning = true;


void intHandler(int sig_num) {
	signal(SIGINT, intHandler);
	keepRunning = false;
}


void checkForPoweroff() {
	int timeout=0;
	while (bcm2835_gpio_lev(STOPSWITCH) == 0) {
		sleep (1);
		timeout++;
		if (timeout == 5) {
			printf ("Stop switch pushed for 5 seconds. Push both buttons now to power off.\n");
			for (int blinks = 0; blinks<=15; blinks++) {
				bcm2835_gpio_write(STOPLED, LOW);
				bcm2835_gpio_write(STARTLED, LOW);
				usleep(200000);
				bcm2835_gpio_write(STOPLED, HIGH);
				bcm2835_gpio_write(STARTLED, HIGH);
				usleep(200000);
				if (bcm2835_gpio_lev(STARTSWITCH) == 0 && bcm2835_gpio_lev(STOPSWITCH) == 0) {
					printf("Both buttons pressed, powering off now.\n");
					fflush(stdout);
					bcm2835_gpio_write(STOPLED, LOW);
					bcm2835_gpio_write(STARTLED, LOW);
					sync();
					sync();
					//reboot(LINUX_REBOOT_CMD_HALT); //native, flaky
					system("sudo poweroff");
					exit(0);
				}
			}
		}
	}
	printf("Poweroff cancelled.\n");
	fflush(stdout);
	bcm2835_gpio_write(STOPLED, LOW);
	bcm2835_gpio_write(STARTLED, LOW);
}


int readadc(char adc_channel);

int readadc(char adc_channel){
	if(adc_channel>7||adc_channel<0){
		printf("ERROR: Invalid Channel. Valid Channels are %d through %d\n", 0, 7);
		return -1;
	}
	//mcp3208 single ended
	char b1 = 6 + (adc_channel >> 2);
	char b2 = (adc_channel & 3) << 6;
	char buf[] = { b1, b2, 0 };
	bcm2835_spi_transfern(buf, sizeof(buf));
	int adcout = (((buf[1] & 15) << 8) + buf[2]);
	return adcout;
}

int main(int argc, char **argv) {

	// Disable swapping and set prioity
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = sched_get_priority_max(SCHED_RR);
	sched_setscheduler(0, SCHED_RR, &sp);
	setpriority(PRIO_PROCESS, 0, -20);
	mlockall(MCL_CURRENT | MCL_FUTURE);

	signal(SIGINT, intHandler);

	// Init bcm2835
	//bcm2835_set_debug(1); //debuk
	if (!bcm2835_init()){
		return 1;
	}

	// Setup GPIO bcm2835
	bcm2835_gpio_fsel(GPIOPIN, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(STOPSWITCH, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(STARTSWITCH, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(STOPLED, BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_fsel(STARTLED, BCM2835_GPIO_FSEL_OUTP);

	// Setup SPI bcm2835
	bcm2835_spi_begin();
	bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST); //default
	bcm2835_spi_setDataMode(BCM2835_SPI_MODE0); //default
	bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_128); //won't work under 128
	bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
	bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);


	// Turn off both LEDs for now
	bcm2835_gpio_write(STOPLED, LOW);
	bcm2835_gpio_write(STARTLED, LOW);

	// Print time for log
	time_t now;
	time(&now);
	printf ("%s started on %s\n", argv[0], ctime(&now));

	// Exit if both buttons are pressed
	if (bcm2835_gpio_lev(STARTSWITCH) == 0 && bcm2835_gpio_lev(STOPSWITCH) == 0) {
		printf("Both buttons pressed, exiting.\n");
		fflush(stdout);
		bcm2835_gpio_write(STOPLED, LOW);
		bcm2835_gpio_write(STARTLED, LOW);
		exit(2);
	}

	// Check command line parameters
	if(argc > 2) {
		printf("Usage: %s [last channel]\n", argv[0]);
		exit(1);
	}
	int noChannels=7;
	if (argc > 1) {
		if(atoi(argv[1]) >= 0 && atoi(argv[1]) <= 7) {
			noChannels=atoi(argv[1]);
		}
		else {
			printf("Last channel must be between 0 and 7\n");
			exit(1);
		}
	}

	// Setup initial prompt
	printf("Waiting for start switch...\n");
	fflush(stdout);
	bcm2835_gpio_write(STOPLED, LOW);
	bcm2835_gpio_write(STARTLED, HIGH);
	while (bcm2835_gpio_lev(STARTSWITCH) == 1) {
		if (!keepRunning) {
			bcm2835_gpio_write(STOPLED, LOW);
			bcm2835_gpio_write(STARTLED, LOW);
			printf("\n");
			bcm2835_close();
			exit(0);
		}
		usleep(10000);
		if (bcm2835_gpio_lev(STOPSWITCH) == 0) {
			checkForPoweroff();
			printf("Waiting for start switch...\n");
			fflush(stdout);
			bcm2835_gpio_write(STOPLED, LOW);
			bcm2835_gpio_write(STARTLED, HIGH);
		}
	}


	// Main loop
	while (keepRunning) {

		// Reset distance and speed
		revs = 0;
		speed = 0;
		pSpeed = 0;
		dist = 0;
		lastFall = 0;
		lastInt = 160000000;
		fade = 0;
		smooth = 0;

		// Calibrate and zero all channels
		printf("Calibrating and zeroing all sensors, do not move the device...\n");
		bcm2835_gpio_write(STOPLED, HIGH);
		bcm2835_gpio_write(STARTLED, HIGH);
		for (a2dChannel=0; a2dChannel<=7; a2dChannel++) {
			for (int i = 0; i < CALSAMPLES; i++) {
				adcval = readadc(a2dChannel);
				VIn = (double) (adcval/4096.0)*5.0;
				offset[a2dChannel]+=VIn;
			}
			offset[a2dChannel]=(double) offset[a2dChannel]/CALSAMPLES;
			//offset[a2dChannel]=0.; //debuk
			printf("offset CH%d=%1.3fV\n", a2dChannel, offset[a2dChannel]); //debuk
			fflush(stdout);
		}

		bcm2835_delay(200);

		// Compose file name from current time
		time_t rawtime;
		struct tm * timeinfo;
		char inbuffer [80]="/mnt/ram/tmp.txt";
		char outbuffer [80];
		time (&rawtime);
		timeinfo = localtime (&rawtime);
		char *infname = inbuffer;
		strftime (outbuffer,80,"./ib_%Y%m%d-%H%M%S.txt",timeinfo);
		char *outfname = outbuffer;


		// Open file for writing
		FILE *fi = fopen(infname, "w");
		if (fi == NULL) {
			printf("Error opening file for writing\n");
			bcm2835_gpio_write(STOPLED, HIGH);
			bcm2835_gpio_write(STARTLED, HIGH);
			exit(1);
		}

		// Write file header
		time_t now;
		time(&now);
		fprintf (fi, "File %s started on %s\n", infname, ctime(&now));
		fprintf (fi, "time [ms]");
		for (int i=0; i<=noChannels; i+=1) {
			fprintf(fi, "\tACH%d", i);
		}
		fprintf(fi, "\tspeed [m/s]\tdist [m]\n");

		// Flush and delay after file header write
		fflush(fi);
		bcm2835_delay(600);

		// Print file and channel info
		printf("Sampling channels 0-%d into %s. Push stop switch to end.\n", noChannels, infname);
		fflush(stdout);
		struct timeb timer_msec;
		long long int timestamp_msec; // timestamp in ms
		long long int inittime_msec; // initial time in ms
		long long int next_timestamp_msec = sampling_interval; // sampling interval

		if (!ftime(&timer_msec)) {
			inittime_msec = ((long long int) timer_msec.time) * 1000ll + (long long int) timer_msec.millitm;
		}
		else {
			inittime_msec = -1;
		}

		// Main DAQ loop
		bcm2835_gpio_write(STOPLED, HIGH);
		bcm2835_gpio_write(STARTLED, LOW);
		while(bcm2835_gpio_lev(STOPSWITCH) == 1 && keepRunning) {
			if (!ftime(&timer_msec)) {
				timestamp_msec = ((long long int) timer_msec.time) * 1000ll + (long long int) timer_msec.millitm-inittime_msec;
			}
			else {
				timestamp_msec = -1;
			}


			// Calculate speed and distance
			sensVal = bcm2835_gpio_lev(GPIOPIN);

			if (sensStr != sensVal && sensVal == 0) {
				fade = 0;
				revs++;
				lastInt = timestamp_msec - lastFall;
				lastFall = timestamp_msec;
			}

			sensStr = sensVal;
			dist = revs * WHEELCIRC;

			pSpeed +=  1000 * WHEELCIRC / (float)lastInt; //partial speed

			if (smooth == 100) {
				smooth = 0;
				speed = pSpeed / 100; //average speed over 100 samples
				pSpeed = 0;
			}

			fade++;
			smooth++;

			if (fade == 4000) { //if there is no rev in 4000 cycles, set speed to 0
				lastInt = 160000000;
				fade = 0;
			}

			// Write data to file
			if (timestamp_msec >= next_timestamp_msec) { // don't write if interval not reached yet
				fprintf(fi, "%llu", timestamp_msec);
				for (a2dChannel=0; a2dChannel<=noChannels; a2dChannel++) {
					adcval = readadc(a2dChannel);
					VIn = ((double) (adcval/4096.0) * 5.0 - offset[a2dChannel]) * MULTIPLIER[a2dChannel];
					fprintf(fi,"\t%1.3f",VIn);
				}
				fprintf(fi, "\t%1.3f\t%1.3f\n", speed, dist);
				bcm2835_delay(1);
				//printf("%lu\t%1.3f\t%1.3f\n", lastInt, dist, speed); //debuk
				next_timestamp_msec = timestamp_msec + sampling_interval;
			}
		}

		// Close file and set LEDs
		fclose(fi);
		bcm2835_gpio_write(STOPLED, LOW);
		bcm2835_gpio_write(STARTLED, HIGH);

		// Move temp file to cwd
		char cpcmd[80];
		strcpy (cpcmd,"mv ");
		strcat (cpcmd, infname);
		strcat (cpcmd, " ");
		strcat (cpcmd, outfname);
		int i = system(cpcmd);
		// int i = rename(infname, outfname); // native cmd (doesn't work cross-FS)

		// Setup closing prompt and provide opportunity to poweroff
		printf("Sampling ended. ");
		if (i != 0) {
			printf("Could not move file from %s to %s.\n", infname, outfname);
			exit(1);
		}
		printf("Output file moved to %s.\n", outfname);

		sleep (1);

		printf("Push start switch to start new file.\n");
		fflush(stdout);
		while (bcm2835_gpio_lev(STARTSWITCH) == 1) {
			if (!keepRunning) {
				bcm2835_gpio_write(STOPLED, LOW);
				bcm2835_gpio_write(STARTLED, LOW);
				printf("\n");
				exit(0);
			}
			usleep(10000);
			if (bcm2835_gpio_lev(STOPSWITCH) == 0) {
				checkForPoweroff();
			}
		}

	}
	bcm2835_close();
	return 0;
}
