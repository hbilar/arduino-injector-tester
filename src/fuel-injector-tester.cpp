/*

Fuel injector tester front-end.

The LCD driving routines were found at https://electropeak.com/learn/, from a
tutorial by Saeed Hosseini @ Electropeak


Pin-outs on my mega2560 (clone):

Pin 22: Fuel pump relay (HIGH = pump off)
Pin 50 - 53: Injectors 


*/
#include <Arduino.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
//LCD pin to Arduino
const int pin_RS = 8; 
const int pin_EN = 9; 
const int pin_d4 = 4; 
const int pin_d5 = 5; 
const int pin_d6 = 6; 
const int pin_d7 = 7; 
const int pin_BL = 10; 


// fuel pump relay pin - mega pin 40
const uint8_t pin_FUEL_PUMP_RELAY = 22;
// injector pins - mega pins 50 through to 53
const uint8_t pin_INJECTOR_1_MASK = B00001000;  // pin 50
const uint8_t pin_INJECTOR_2_MASK = B00000100;  // pin 51
const uint8_t pin_INJECTOR_3_MASK = B00000010;  // pin 52
const uint8_t pin_INJECTOR_4_MASK = B00000001;  // pin 53

const uint8_t dir_INJECTORS_OUT = B00001111;  // Make PORTB pins 50 - 53 outputs

#include <string.h>
#include <stdio.h>

typedef struct leak_test_params {
  int seconds;
  int max_seconds;
  int min_seconds;
  int second_step;
} leak_test_params;

typedef struct {
  int seconds;
  int max_seconds;
  int min_seconds;
  int second_step;
  short duty;
  int duty_step;
  int min_duty;
  int max_duty;
  int rpm;
  int rpm_step;
  int min_rpm;
  int max_rpm;
} rpm_mode_params;

typedef struct {
  int seconds;
  int max_seconds;
  int min_seconds;
  int second_step;
} full_flow_params;

typedef struct {
  int pulses;
  int max_pulses;
  int min_pulses;
  int pulse_step;

  long long microseconds;
  long long max_microseconds;
  long long min_microseconds;
  long long microsecond_step;
} pwm_params;

typedef enum {
  LEAK_TEST,
  RPM_MODE,
  FULL_FLOW_MODE,
  PWM_MODE,
  NO_MODE
} operation_t;

typedef enum {
  NORMAL_MODE,
  IN_MENU_MODE,
  NO_STATE
} state_t;

typedef enum button_t {
  NO_BUTTON,
  LEFT,
  RIGHT,
  UP,
  DOWN,
  SELECT
} button_t;

button_t LAST_BUTTON = NO_BUTTON;
operation_t CURRENT_MODE = LEAK_TEST;
operation_t LAST_MODE = NO_MODE;
int PARAM_NUM = 0;  /* Which param we're currently modifying */
unsigned long last_button_press_time = 0; /* when a button was last pressed */

leak_test_params LEAK_TEST_PARAMS = { .seconds = 60, 
                                      .max_seconds = 300, 
                                      .min_seconds = 10, 
                                       .second_step = 10 };
rpm_mode_params RPM_MODE_PARAMS = { .seconds = 15, 
                                    .max_seconds=60, 
                                    .min_seconds = 5, 
                                    .second_step = 1, 
                                    .duty = 50, 
                                    .duty_step = 1, 
                                    .min_duty=1, 
                                    .max_duty=99, 
                                    .rpm = 1000, 
                                    .rpm_step = 200, 
                                    .min_rpm = 600, 
                                    .max_rpm = 6000 };
full_flow_params FULL_FLOW_PARAMS = { .seconds = 10, 
                                      .max_seconds = 30, 
                                      .min_seconds = 1, 
                                      .second_step = 1 };
pwm_params PWM_PARAMS = { .pulses = 30, 
                          .max_pulses = 100,
                          .min_pulses = 1,
                          .pulse_step = 1,
                          .microseconds = 1000, 
                          .max_microseconds = 1000000,  // 1s
                          .min_microseconds = 100, // 0.1ms
                          .microsecond_step = 10 // 0.01ms
                          }; /* 100 pulses @ 5ms */


/* User interface:
 *   
 *   
 * Test modes:
 *    Leak test:
 *      Run pump for x seconds (or until cancelled)    
 *   
 *    RPM mode:
 *      Simulate X RPM. Open injectors for nn% of duty cycle
 *      
 *    Full flow mode:
 *      Keep injectors fully open for x seconds
 *    
 *    PWM mode:
 *      Open injectors for x.y milliseconds, n times.
 *      
 *      
 *      
 * Status line:      ________________
 *    Leak test:     xx s          12
 *    
 *    RPM mode:      10s@4000@50%  12
 *    
 *    Full flow:     xx s          12
 *    
 *    PWM mode:      10s@1.2ms     12
 *    
 */


LiquidCrystal lcd( pin_RS,  pin_EN,  pin_d4,  pin_d5,  pin_d6,  pin_d7);

/* display the top line */
void set_top_line(operation_t mode, bool running) 
{
  char buf[17];
  switch(mode) {
    case LEAK_TEST:
      snprintf(buf, sizeof(buf),"Leak Test Mode  ");
      break;
      ;;
    case RPM_MODE:
      snprintf(buf, sizeof(buf),"RPM Mode        ");
      break;
      ;;
    case FULL_FLOW_MODE:
      snprintf(buf, sizeof(buf),"Full Flow Mode  ");
      break;
      ;;
    case PWM_MODE:
      snprintf(buf, sizeof(buf),"PWM Mode        ");   
      break;
      ;;
    default:
      snprintf(buf, sizeof(buf),"Unknown mode    "); 
  }

  // Add running flag
  if (running) {
    buf[15] = '*';
  }

  lcd.setCursor(0,0);
  lcd.print(buf);
}


void set_bottom_line(operation_t mode, button_t button)
{
    /* Print bottom line */
    char buf[17];
    const char *p0_marker = PARAM_NUM == 0 ? ">" : " ";
    const char *p1_marker = PARAM_NUM == 1 ? ">" : " ";
    const char *p2_marker = PARAM_NUM == 2 ? ">" : " ";
    switch (mode) {
      case LEAK_TEST:
        snprintf(buf, sizeof(buf), ">%d seconds      ", 
                 LEAK_TEST_PARAMS.seconds); 
        break;
        ;;
      case FULL_FLOW_MODE:
        snprintf(buf, sizeof(buf), ">%d seconds      ", 
                 FULL_FLOW_PARAMS.seconds); 
        break;
        ;;
      case RPM_MODE:
        snprintf(buf, sizeof(buf), "%s%ds%s%drpm%s%d%%     ", 
                 p0_marker, RPM_MODE_PARAMS.seconds, p1_marker, 
                 RPM_MODE_PARAMS.rpm, p2_marker, RPM_MODE_PARAMS.duty); 
        break;
        ;;
      case PWM_MODE:
        long long us = PWM_PARAMS.microseconds;

        long us_big = (long)((long long)us / (long long)1000);
        long us_small = (us % 1000) / 10;

        snprintf(buf, sizeof(buf), "%s%dp %s%d.%02dms     ", 
                 p0_marker, PWM_PARAMS.pulses,
                 p1_marker, (int)us_big, (int)us_small);
        break;
        ;;
      default:
        snprintf(buf, sizeof(buf), "b %s%d  p %s%l         ", 
                 p0_marker, button, p1_marker, PARAM_NUM); 
        ;;
    }
    lcd.setCursor(0,1);
    lcd.print(buf);
}


/* save settings to eeprom */
void save_settings(bool immediate)
{
  if (! immediate) {
    lcd.setCursor(0,0);
    lcd.print("Saving settings ");
    lcd.setCursor(0,1);
    lcd.print("                ");
    delay(5000);
  }

  /* order:
      int         leak_test.seconds
      int         rpm_mode.seconds
      short       rpm_mode.duty
      int         rpm_mode.rpm
      int         full_flow.seconds
      int         pwm_mode.pulses
      long long   pwm_mode.microseconds
  */

  int eadr = 0;

  /* leak test */
  EEPROM.put(eadr, LEAK_TEST_PARAMS.seconds);
  eadr += sizeof(LEAK_TEST_PARAMS.seconds);

  /* rpm mode */
  EEPROM.put(eadr, RPM_MODE_PARAMS.seconds);
  eadr += sizeof(RPM_MODE_PARAMS.seconds);
  EEPROM.put(eadr, RPM_MODE_PARAMS.duty);
  eadr += sizeof(RPM_MODE_PARAMS.duty);
  EEPROM.put(eadr, RPM_MODE_PARAMS.rpm);
  eadr += sizeof(RPM_MODE_PARAMS.rpm);

  /* full flow mode */
  EEPROM.put(eadr, FULL_FLOW_PARAMS.seconds);
  eadr += sizeof(FULL_FLOW_PARAMS.seconds);

  /* pwm mode */
  EEPROM.put(eadr, PWM_PARAMS.pulses);
  eadr += sizeof(PWM_PARAMS.pulses);
  EEPROM.put(eadr, PWM_PARAMS.microseconds);
  eadr += sizeof(PWM_PARAMS.microseconds);
  
}

/* load settings from eeprom */
void load_settings()
{
  lcd.setCursor(0,0);
  lcd.print("Loading settings ");
  lcd.setCursor(0,1);
  lcd.print("                ");
  delay(1000);

  /* order:
      int         leak_test.seconds
      int         rpm_mode.seconds
      short       rpm_mode.duty
      int         rpm_mode.rpm
      int         full_flow.seconds
      int         pwm_mode.pulses
      long long   pwm_mode.microseconds
  */

  int eadr = 0;

  /* leak test */
  EEPROM.get(eadr, LEAK_TEST_PARAMS.seconds);
  eadr += sizeof(LEAK_TEST_PARAMS.seconds);

  /* rpm mode */
  EEPROM.get(eadr, RPM_MODE_PARAMS.seconds);
  eadr += sizeof(RPM_MODE_PARAMS.seconds);
  EEPROM.get(eadr, RPM_MODE_PARAMS.duty);
  eadr += sizeof(RPM_MODE_PARAMS.duty);
  EEPROM.get(eadr, RPM_MODE_PARAMS.rpm);
  eadr += sizeof(RPM_MODE_PARAMS.rpm);

  /* full flow mode */
  EEPROM.get(eadr, FULL_FLOW_PARAMS.seconds);
  eadr += sizeof(FULL_FLOW_PARAMS.seconds);

  /* pwm mode */
  EEPROM.get(eadr, PWM_PARAMS.pulses);
  eadr += sizeof(PWM_PARAMS.pulses);
  EEPROM.get(eadr, PWM_PARAMS.microseconds);
  eadr += sizeof(PWM_PARAMS.microseconds);
  
}

/* Get the button pressed */
int get_button() 
{
  int x = analogRead (0);
  if (x < 60) {
    return RIGHT;
  }
  else if (x < 200) {
    return UP;
  }
  else if (x < 400){
    return DOWN;
  }
  else if (x < 600){
    return LEFT;
  }
  else if (x < 800){
    return SELECT;
  }
  return NO_BUTTON;
}


void leak_test_change_param(int p, bool increase)
{
  int modifier = increase ? 1 : -1;

  LEAK_TEST_PARAMS.seconds += modifier * LEAK_TEST_PARAMS.second_step;
  LEAK_TEST_PARAMS.seconds = LEAK_TEST_PARAMS.seconds > LEAK_TEST_PARAMS.max_seconds ? LEAK_TEST_PARAMS.max_seconds : LEAK_TEST_PARAMS.seconds;
  LEAK_TEST_PARAMS.seconds = LEAK_TEST_PARAMS.seconds < LEAK_TEST_PARAMS.min_seconds ? LEAK_TEST_PARAMS.min_seconds : LEAK_TEST_PARAMS.seconds;
}

void full_flow_mode_change_param(int p, bool increase)
{
  int modifier = increase ? 1 : -1;

  FULL_FLOW_PARAMS.seconds += modifier * FULL_FLOW_PARAMS.second_step;
  FULL_FLOW_PARAMS.seconds = FULL_FLOW_PARAMS.seconds > FULL_FLOW_PARAMS.max_seconds ? FULL_FLOW_PARAMS.max_seconds : FULL_FLOW_PARAMS.seconds;  
  FULL_FLOW_PARAMS.seconds = FULL_FLOW_PARAMS.seconds < FULL_FLOW_PARAMS.min_seconds ? FULL_FLOW_PARAMS.min_seconds : FULL_FLOW_PARAMS.seconds;  
}

void rpm_mode_change_param(int p, bool increase)
{
  int modifier = increase ? 1 : -1;

  switch (p) {
    case 0:
      // seconds
      RPM_MODE_PARAMS.seconds += modifier * RPM_MODE_PARAMS.second_step;
      RPM_MODE_PARAMS.seconds = RPM_MODE_PARAMS.seconds > RPM_MODE_PARAMS.max_seconds ? RPM_MODE_PARAMS.max_seconds : RPM_MODE_PARAMS.seconds;  
      RPM_MODE_PARAMS.seconds = RPM_MODE_PARAMS.seconds < RPM_MODE_PARAMS.min_seconds ? RPM_MODE_PARAMS.min_seconds : RPM_MODE_PARAMS.seconds;  
      break;
      ;;
    case 1:
      // RPM
      RPM_MODE_PARAMS.rpm += modifier * RPM_MODE_PARAMS.rpm_step;
      RPM_MODE_PARAMS.rpm = RPM_MODE_PARAMS.rpm > RPM_MODE_PARAMS.max_rpm ? RPM_MODE_PARAMS.max_rpm : RPM_MODE_PARAMS.rpm;  
      RPM_MODE_PARAMS.rpm = RPM_MODE_PARAMS.rpm < RPM_MODE_PARAMS.min_rpm ? RPM_MODE_PARAMS.min_rpm : RPM_MODE_PARAMS.rpm;  
      break;
      ;;
    case 2:
      // duty
      RPM_MODE_PARAMS.duty += modifier * RPM_MODE_PARAMS.duty_step;
      RPM_MODE_PARAMS.duty = RPM_MODE_PARAMS.duty > RPM_MODE_PARAMS.max_duty ? RPM_MODE_PARAMS.max_duty : RPM_MODE_PARAMS.duty;  
      RPM_MODE_PARAMS.duty = RPM_MODE_PARAMS.duty < RPM_MODE_PARAMS.min_duty ? RPM_MODE_PARAMS.min_duty : RPM_MODE_PARAMS.duty;  
      break;
      ;;
  }
}

void pwm_mode_change_param(int p, bool increase)
{
  switch (p) {
    case 0:
      // pulses
      PWM_PARAMS.pulses += (increase ? 1 : -1 ) * PWM_PARAMS.pulse_step;
      PWM_PARAMS.pulses = PWM_PARAMS.pulses > PWM_PARAMS.max_pulses ? 
                            PWM_PARAMS.max_pulses : PWM_PARAMS.pulses;  
      PWM_PARAMS.pulses = PWM_PARAMS.pulses < PWM_PARAMS.min_pulses ? 
                            PWM_PARAMS.min_pulses : PWM_PARAMS.pulses;  
      break;
      ;;
    case 1:
      // microseconds
      PWM_PARAMS.microseconds += (increase ? 1 : -1 ) * PWM_PARAMS.microsecond_step;
      PWM_PARAMS.microseconds = PWM_PARAMS.microseconds > PWM_PARAMS.max_microseconds ? PWM_PARAMS.max_microseconds : PWM_PARAMS.microseconds;  
      PWM_PARAMS.microseconds = PWM_PARAMS.microseconds < PWM_PARAMS.min_microseconds ? 
                                PWM_PARAMS.min_microseconds : PWM_PARAMS.microseconds;  
      break;
      ;;
  }
}


/********************************************/

/* Calculate how many microseconds a 720 cycle lasts at a certain RPM */ 
long calculate_720_time_us(int rpm)
{
  float t = 60.0 / ( (float)rpm / 2);  // how long one 720 degree cycle lasts

  return (long)(t * 1000000L);
}


/* Calculate how many microseconds the injector should be open to achieve a particular
   duty cycle at a particular RPM */
long calculate_injector_open_time_us(int rpm, int duty)
{
  return (duty * calculate_720_time_us(rpm))/100;
}


/* Sleep for longer than 16384 microseconds... Will blow the stack up at long delays (because no
 * tail recursion) */
void do_longer_delay(long long microseconds) 
{
  if (microseconds > 16383) {
    delayMicroseconds(16383);
    do_longer_delay(microseconds - 16383);
  } else {
    delayMicroseconds(microseconds);
  }
}


void do_constant_rpm_mode()
{
  int rpm = RPM_MODE_PARAMS.rpm;
  int duty = RPM_MODE_PARAMS.duty;
  int seconds = RPM_MODE_PARAMS.seconds;

  long cycle_720_time  = calculate_720_time_us(rpm);
  long injector_open_time = calculate_injector_open_time_us(rpm, duty);
  long injector_close_time = cycle_720_time - injector_open_time;

  char buf[100];

  snprintf(buf, sizeof(buf), "cycle_720_time: %ld,  open_time = %ld,  close_time = %ld, "
          "rpm = %d,   duty = %d", cycle_720_time, injector_open_time, injector_close_time, rpm, duty);
  Serial.println(buf);

  snprintf(buf, sizeof(buf), "IPW: %ld.%ldms        ", injector_open_time / 1000L, (long)(injector_open_time % 1000L));
  Serial.println(buf);

  snprintf(buf, 17, "IPW: %ld.%ldms        ", injector_open_time / 1000L, (long)(injector_open_time % 1000L));
  lcd.setCursor(0,0);
  lcd.print(buf);

  /* Turn on fuel pump */
  digitalWrite(pin_FUEL_PUMP_RELAY, LOW);
  delay(2000); // wait for 2s for stuff to stabilize

  /* open for the first interval */
  long start_time = micros();
  long end_time = start_time + (long)(seconds * 1000000L);

  snprintf(buf, sizeof(buf), "start time: %ld,   end_time = %ld", start_time, end_time);
  Serial.println(buf);

  Serial.println("waiting");
  /* Do the actual injector pulsing */ 
  uint8_t pin_ALL_INJECTORS_MASK = pin_INJECTOR_1_MASK | pin_INJECTOR_2_MASK | 
                                   pin_INJECTOR_3_MASK | pin_INJECTOR_4_MASK;
  do {
    /* turn on */
    PORTB = PORTB | pin_ALL_INJECTORS_MASK;
    do_longer_delay(injector_open_time);

    /* turn off */
    PORTB = PORTB & (~pin_ALL_INJECTORS_MASK);
    do_longer_delay(injector_close_time);
  } while (end_time > micros()); // FIXME: What if end_time overflowed? This will run for a long time then...
  Serial.println("done");

  /* Turn off fuel pump */
  digitalWrite(pin_FUEL_PUMP_RELAY, HIGH);
}


/* In Leak test mode, we simply run the fuel pump for n seconds */
void do_leak_test_mode()
{
  int seconds = LEAK_TEST_PARAMS.seconds;

  char buf[100];
  snprintf(buf, sizeof(buf), "Leak test mode: Running pump for %d seconds", seconds);
  Serial.println(buf);

  /* Record start and end-times. Should probably check for overflow in the end_time variable
    and handle that case */
  // FIXME: Handle overflowing end_time
  long start_time = micros();
  long end_time = start_time + (long)(seconds * 1000000L);

  /* Turn on fuel pump */
  digitalWrite(pin_FUEL_PUMP_RELAY, LOW);

  /* Now wait, and update the display every second */
  do {
    snprintf(buf, sizeof(buf), "%ds left               ", (end_time - micros())/1000000L);

    lcd.setCursor(0,1);
    lcd.print(buf);
  } while (end_time > micros());

  /* Turn off fuel pump */
  digitalWrite(pin_FUEL_PUMP_RELAY, HIGH);
}


/* In full flow mode, we run the pump and open the injectors completely for n seconds */
void do_full_flow_mode()
{
  int seconds = FULL_FLOW_PARAMS.seconds;

  char buf[100];
  snprintf(buf, sizeof(buf), "Full flow mode: full flow for %d seconds", seconds);
  Serial.println(buf);

  /* Turn on fuel pump */
  digitalWrite(pin_FUEL_PUMP_RELAY, LOW);
  delay(2000); // wait for 2 seconds for fuel pressure to stabilize 

  /* Record start and end-times. Should probably check for overflow in the end_time variable
    and handle that case */
  // FIXME: Handle overflowing end_time
  long start_time = micros();
  long end_time = start_time + (long)(seconds * 1000000L);

  /* Turn on injectors */
  uint8_t pin_ALL_INJECTORS_MASK = pin_INJECTOR_1_MASK | pin_INJECTOR_2_MASK | 
                                   pin_INJECTOR_3_MASK | pin_INJECTOR_4_MASK;
  PORTB = PORTB | pin_ALL_INJECTORS_MASK;

  /* Now wait, and update the display every second */
  do {
    snprintf(buf, sizeof(buf), "%ds left               ", (end_time - micros())/1000000L);

    lcd.setCursor(0,1);
    lcd.print(buf);
  } while (end_time > micros());

  /* Turn off fuel pump and injectors */
  digitalWrite(pin_FUEL_PUMP_RELAY, HIGH);
  PORTB = PORTB & (~pin_ALL_INJECTORS_MASK);
}

void do_pwm_mode()
{
  long long int pulsewidth = PWM_PARAMS.microseconds;
  long pulses = PWM_PARAMS.pulses;

  char buf[100];
  snprintf(buf, sizeof(buf), "PWM mode:  pulsewidth = %ld us,  number of pulses %d", pulsewidth, pulses);
  Serial.println(buf);

  /* Turn on fuel pump */
  digitalWrite(pin_FUEL_PUMP_RELAY, LOW);
  delay(2000); // wait for 2s for stuff to stabilize

  Serial.println("after fuel pump");

  /* Do the actual injector pulsing */ 
  uint8_t pin_ALL_INJECTORS_MASK = pin_INJECTOR_1_MASK | pin_INJECTOR_2_MASK | 
                                   pin_INJECTOR_3_MASK | pin_INJECTOR_4_MASK;
  /* pulse injectors */
  for (int p = 0; p < pulses; p++) {

    /* idea is:  turn injector on, sleep for some time, turn off. Then delay some time until starting again */

    /* turn on */
    PORTB = PORTB | pin_ALL_INJECTORS_MASK;
    delayMicroseconds(pulsewidth);

    /* turn off */
    PORTB = PORTB & (~pin_ALL_INJECTORS_MASK);

    /* tell user how many to go */
    snprintf(buf, 17, "pulses left %d     ", pulses - p);
    lcd.setCursor(0, 0);
    lcd.println(buf);

    delay(500);
  }

  /* Turn off fuel pump */
  digitalWrite(pin_FUEL_PUMP_RELAY, HIGH);
}

void setup() {

  lcd.begin(16, 2);

  /* Check if select button is held down when powering up.
     If it is, restore "factory defaults", otherwise load settings
     from EEPROM */
  int x = analogRead(0);
  if (x >= 600 && x < 800) {
    // pressed SELECT
    lcd.setCursor(0,0);
    lcd.print("RESETTING       ");
    lcd.setCursor(0,1);
    lcd.print("                ");
    delay(3000);
    save_settings(true);
  }
  load_settings();

  /* Enable serial port */
  Serial.begin(9600);

  /* Make fuel pump relay pin (22) an output and turn it HIGH (relay is off)  */ 
  pinMode(pin_FUEL_PUMP_RELAY, OUTPUT); 
  digitalWrite(pin_FUEL_PUMP_RELAY, HIGH);

  /* Make injector pins outputs */
  DDRB = DDRB | dir_INJECTORS_OUT;  

  set_top_line(CURRENT_MODE, false);
  set_bottom_line(CURRENT_MODE, NO_BUTTON);
}

void loop() {
  /* check for button presses */
  button_t button = (button_t)get_button();
  if (LAST_BUTTON == button) {
    
    if (button == UP || button == DOWN) {
      /* user holding up / down button down? */

      unsigned long now = micros();     // time now
      unsigned long threshold = 300000; // threshold to generate another key press
      if ((CURRENT_MODE == PWM_MODE && PARAM_NUM == 1) ||
          (CURRENT_MODE == RPM_MODE && PARAM_NUM == 2)) {
        /* in PWM mode, adjust the ms parameter quicker, and in RPM mode
           adjust the duty quicker */
        threshold = 100000;
      }
      if ((now - last_button_press_time) > threshold) {
        /* button timeout - fake that the user let go of the button
          by setting LAST_BUTTON to NO_BUTTON and re-running loop() */
        LAST_BUTTON = NO_BUTTON;
        return;
      }
    }
    else if (button == SELECT) {
      /* Save settings if select held for 1s continuously */
      unsigned long now = micros();     // time now
      unsigned long threshold = 1000000; // threshold to save settings

      if ((now - last_button_press_time) > threshold) {
        save_settings(false);
      }
    }
  }
  else if (LAST_BUTTON != button) {
    LAST_BUTTON = button;

    /* record timestamp of button being pressed */
    last_button_press_time = micros();

    if (button == SELECT) {
      /* Change mode */
      CURRENT_MODE = (operation_t)((int)CURRENT_MODE + 1);
      if (CURRENT_MODE == NO_MODE) {
        CURRENT_MODE = LEAK_TEST;
      }
      PARAM_NUM = 0;
    }
    else if (button == LEFT) {
      /* Set param number */

      switch(CURRENT_MODE) {
        case LEAK_TEST:
        case FULL_FLOW_MODE:
          PARAM_NUM = 0;
          break;
          ;;
        case RPM_MODE:
          PARAM_NUM = (PARAM_NUM + 1) % 3;
          break;
          ;;
        case PWM_MODE:
          PARAM_NUM = (PARAM_NUM + 1) % 2;
          break;
          ;;
        default:
          PARAM_NUM = 0;
      }
    }
    else if (button == UP || button == DOWN) {

      bool increase = button == UP;

      /* increase/decrease parameter PARAM_NUM */
      switch (CURRENT_MODE) {
        case LEAK_TEST:
          leak_test_change_param(PARAM_NUM, increase);
          break;
          ;;
        case FULL_FLOW_MODE:
          full_flow_mode_change_param(PARAM_NUM, increase);
          break;
          ;;
      case RPM_MODE:
          rpm_mode_change_param(PARAM_NUM, increase);
          break;
          ;;
      case PWM_MODE:
          pwm_mode_change_param(PARAM_NUM, increase);
          break;
          ;;
      }
    }
    else if (button == RIGHT) {
      /* Run the tests */

      switch (CURRENT_MODE) {

        case RPM_MODE:
          do_constant_rpm_mode();
          break;
          ;;
        case LEAK_TEST:
          do_leak_test_mode();
          break;
          ;;
        case FULL_FLOW_MODE:
          do_full_flow_mode();
          break;
          ;;

        case PWM_MODE:
          do_pwm_mode();
          break;
          ;;
      }
    }
    set_top_line(CURRENT_MODE, false);

    /* Finally, update the bottom status line */
    set_bottom_line(CURRENT_MODE, button);
  }
} 
