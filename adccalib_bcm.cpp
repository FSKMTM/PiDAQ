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

// This code shows an example of how to use the val3208
// routine in adcio.cpp to read and display a single value
// from each channel of the ADC connected to CS0
// change cs=0 below to cs=1 to read the adc on CS1

// the function gpioGetHWRev has to be called once before
// calling either val3208 or read3208 functions to set
// the gpio base register address according to the model
// Raspberry Pi you are using.

// Compile with g++ -Wall -o adccalib adccalib_bcm.cpp -lrt -lm

// Signal handling
static bool keepRunning = true;

void intHandler(int sig_num) {
    signal(SIGINT, intHandler);
    keepRunning = false;
}

int readadc(char adc_channel);

int readadc(char adc_channel){
    if(adc_channel>7||adc_channel<0){
        printf("ERROR: Invalid Channel. Valid Channels are %d through %d\n", 0, 7);
        return -1;
    }
	//mcp3208 single ended
	char b1 = 6 + (adc_channel>>2);
	char b2 = (adc_channel & 3)<<6;
    char buf[] = { b1, b2, 0 };
    bcm2835_spi_transfern(buf, sizeof(buf));
    int adcout = (((buf[1]&15)<<8) + buf[2]);
    return adcout;
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
    int a2dChannel;
    int adcval;

    if (!bcm2835_init()){
        return 1;
    }

    // Setup SPI bcm2835
    bcm2835_spi_begin();
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST); //default
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0); //default
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_128);
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);


    // Print info
    printf("Channel calibration started for %1.3f V. Ctrl-C to end.\n",Voltage);


    // Main calibration loop
    printf("\e[?25l"); //hide cursor
    while(keepRunning)
    {
       	for (a2dChannel=0; a2dChannel<=7; a2dChannel++)
       	{
  	        adcval = readadc(a2dChannel);
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
