
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include <linux/rtc.h>

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#define SLIGHT_TIMER_FREQ 64

int
main()
{
	struct timeval t0, t1, tdiff;
	int retval;
        unsigned long data;
	int m_rtc;
	int i;

	m_rtc = open( "/dev/rtc", O_RDONLY );

	if( m_rtc == -1 )
	{
		perror("Can't open the real-time clock.");
		exit(1);
	}

	// set the timer rate for 64 Hz
	// TODO: maybe try for something higher if root?
	retval = ioctl( m_rtc, RTC_IRQP_SET, SLIGHT_TIMER_FREQ );
	if( retval == -1 )
	{
		perror("/dev/rtc(64Hz)");
		exit(1);
	}

	retval = ioctl( m_rtc, RTC_PIE_ON, 0 );
	if( retval == -1 )
	{
		perror("Error enabling periodic interrupts.");
		exit(1);
	}

	gettimeofday(&t0, NULL);
	for(i = 0; i < 50; i++) {
		retval = read( m_rtc, &data, sizeof( unsigned long ) );

	}
	gettimeofday(&t1, NULL);

	timeval_subtract(&tdiff, &t1, &t0);
	printf("50 interrupts in %d.%06d seconds\n", tdiff.tv_sec,tdiff.tv_usec);

	close(m_rtc);
}



  
int
timeval_subtract (result, x, y)
struct timeval *result, *x, *y;
{
       /* Perform the carry for the later subtraction by updating y. */
       if (x->tv_usec < y->tv_usec) {
         int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
         y->tv_usec -= 1000000 * nsec;
         y->tv_sec += nsec;
       }
       if (x->tv_usec - y->tv_usec > 1000000) {
         int nsec = (x->tv_usec - y->tv_usec) / 1000000;
         y->tv_usec += 1000000 * nsec;
         y->tv_sec -= nsec;
       }
     
       /* Compute the time remaining to wait.
          tv_usec is certainly positive. */
       result->tv_sec = x->tv_sec - y->tv_sec;
       result->tv_usec = x->tv_usec - y->tv_usec;
     
       /* Return 1 if result is negative. */
       return x->tv_sec < y->tv_sec;
}
