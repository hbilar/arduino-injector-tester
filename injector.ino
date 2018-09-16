
/*
    Pulse an injector on / off.

    Injector pin = pin 13 or 12.

    Uses A0 as an "RPM Input" - 5V = 8000 RPM, 0V = 600RPM.

    Calculates the injector pulse time relative to the RPM,
    and then pulses the injector for 1 interval, and sleeps
    for 3 (to simulate a 4 cylinder engine, where the injector
    is kept open for the entire duration of the intake stroke.
*/


#include <string.h>

const int INJECTOR_PIN = 13;
const int MAX_RPM = 8000; // At A0 reading 5V
const int MIN_RPM = 600;  // At A0 reading 0V

void setup() {
  Serial.begin(9600);

  DDRB = B11000000; // 13 + 12 are now outputs
}

/* Convert the current voltage on A0 into an RPM (0V = MIN_RPM rpm, 5V = MAX_RPM) */
int get_current_rpm()
{
  int sensorValue = analogRead(A0);
  int range = MAX_RPM - MIN_RPM;

  // sensorValue goes from 0 to 1023 (0 -> 5V).
  int rpm = MIN_RPM + (int)(range * (float)sensorValue / 1023.0);
    
  return rpm;
}


/* Calculate how long an injector should be "on" (to be on for 100% of the intake stroke).
 * Use floats for accuracy and return a long int of microseconds as the interval. */
long int calculate_sleep_time(int rpm)
{
  float rps = rpm / 60.0;
  float num720revs = rps / 2.0;
  float interval = (1 / (4.0 * num720revs ));   /* there are 4 different 'modes' in the 720 degree cycle */

  return (long int)(1000000.0 * interval);
}


/* Sleep for longer than 16384 microseconds... Will blow the stack up at long delays (because no
 * tail recursion) */
void do_longer_delay(long int microseconds) 
{
  if (microseconds > 16383) {
    delayMicroseconds(16383);
    do_longer_delay(microseconds - 16383);
  } else {
    delayMicroseconds(microseconds);
  }
}


static long int injector_interval = 100000;  // microseconds
static int old_rpm = 0;

void loop() {
  int rpm = get_current_rpm();

  if (rpm != old_rpm) {
    old_rpm = rpm;
    injector_interval = calculate_sleep_time(rpm);

    /* This code causes the arduino to stutter because it's so slow to transfer the
     * data over serial. Enable for debugging only...
     */
    /*
    char buf[255];
    snprintf(buf, sizeof(buf), "new RPM %d, new interval %ld\n", rpm, injector_interval);    
    Serial.write(buf);
    */
  }

  /* open for the first interval */
  PORTB = B11000000; // turn on led
  do_longer_delay(injector_interval);

  PORTB = B00000000;  // turn off
  for (int other_cyl = 1; other_cyl < 5; other_cyl ++) {
    /* looks crazy to do this as a for loop, but at least it avoids overflowing the longs... */
    do_longer_delay(injector_interval);
  }
}
