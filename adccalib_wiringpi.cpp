#include "adcio.cpp"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>
#include <signal.h>

// This code shows an example of how to use the val3208
// routine in adcio.cpp to read and display a single value
// from each channel of the ADC connected to CS0
// change cs=0 below to cs=1 to read the adc on CS1

// the function gpioGetHWRev has to be called once before
// calling either val3208 or read3208 functions to set
// the gpio base register address according to the model
// Raspberry Pi you are using.

// Compile with g++ -Wall -o adccalib adccalib_wiringpi.cpp -lrt -lm

// Signal handling
static bool keepRunning = true;

void intHandler(int sig_num) {
    signal(SIGINT, intHandler);
    keepRunning = false;
}

int main(int argc, char **argv)
{

    signal(SIGINT, intHandler);

    if(argc != 2) {
        printf("Usage: %s voltage\n", argv[0]);
        exit(1);
    }

    float Voltage = atof(argv[1]);
    if (Voltage < 0 || Voltage > 25) {
        printf("Measured voltage must be between 0 and 25 V.");
    }

    float VIn;
    int cs = 0;
    int a2dChannel;
    int adcval;

    gpioGetHWRev();

    // Print info
    printf("Channel calibration started for %1.3f V. Ctrl-C to end.\n",Voltage);


    // Main calibration loop
    printf("\e[?25l"); //hide cursor
    while(keepRunning)
    {
       	for (a2dChannel=0; a2dChannel<=7; a2dChannel++)
       	{
  	        adcval = val3208(cs,a2dChannel);
	        VIn = (double) (adcval/4096.0)*Voltage;
	        printf("CH%d=%1.3f ",a2dChannel,VIn);
        }
        printf("\r");
        sleep (0.1);
        fflush(stdout);
    }
    printf("\e[?25h"); //show cursor
    printf("\nCalibration ended.\n");
    return 0;
}
