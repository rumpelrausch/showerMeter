#include <Tiny4kOLED.h>

#define CENT_PER_KWH 36
#define HEATER_KW 21
#define DAYS_PER_YEAR 160

#define OFF_AFTER_SECONDS 3
#define FINISHED_AFTER_OFF_SECONDS 10
#define ANALOG_THRESHOLD 65

#define PAGE_SWITCH_EVERY_SECONDS 4
#define SECONDS_UNTIL_UNHAPPY 360
#define SECONDS_UNTIL_WARN 420

#define GREET_MILLISECONDS 1000
#define SLOW_POLL_MILLISECONDS 2003 // use a prime to avoid continously tracking zero crossings
#define FAST_POLL_MILLISECONDS 307

// ###############################################
// ###############################################
// ###############################################

// #define TEST_ANALOG_IN
#define ANALOG_PIN 3
#define PAGE_COST 1
#define PAGE_TIME 2
#define CENTICENT_PER_SECOND (CENT_PER_KWH * HEATER_KW) / 36

#define STATE_IDLE 1
#define STATE_ON 2
#define STATE_WAIT_FOR_OFF 3
#define STATE_WAIT_FOR_FINISH 4
#define STATE_TO_IDLE 5

volatile uint8_t state = STATE_IDLE;
uint8_t currentPage;
volatile uint8_t timer1value;
volatile uint16_t seconds = 0;
volatile uint16_t secondsOn;
volatile uint16_t centiCents;
volatile uint16_t timerOffDetection;      // > 0: Timer is running
volatile uint16_t timerFinishedDetection; // > 0: Timer is running
volatile uint8_t blinkState;
volatile bool displayIsDirty;
volatile uint8_t updateCount = 0;

void setup()
{
  reset();
  state = STATE_TO_IDLE;
  pinMode(ANALOG_PIN, INPUT);

  oled.begin(64, 48, sizeof(tiny4koled_init_64x48), tiny4koled_init_64x48);
  oled.setRotation(1);
  oled.setFont(FONT8X16);
  oled.clear();
  oled.on();
  greet();
  oled.clear();

#ifndef TEST_ANALOG_IN
  setNextPage();
  initTimer();
#endif
}

void loop()
{
#ifdef TEST_ANALOG_IN
  int analog = analogRead(ANALOG_PIN);
  static char outString[] = "        ";
  itoa(analog, outString, 10);
  oled.setCursor(0, 0);
  oled.print(outString);
  oled.clearToEOL();

  oled.setCursor(0, 2);
  if (analog > ANALOG_THRESHOLD)
  {
    oled.print(F("ON "));
  }
  else
  {
    oled.print(F("OFF"));
  }
  delay(500);
#else
  state = determinePhase();
  updateDisplay();
  debugOnOff();
  delay(state == STATE_IDLE ? SLOW_POLL_MILLISECONDS : FAST_POLL_MILLISECONDS); // slow poll if off
#endif
}

void reset()
{
  currentPage = PAGE_COST;
  timer1value = 0;
  secondsOn = 0;
  centiCents = 0;
  timerOffDetection = 0;      // > 0: Timer is running
  timerFinishedDetection = 0; // > 0: Timer is running
  blinkState = 0;
  displayIsDirty = true;
}

void greet()
{
  oled.setContrast(255);
  oled.setCursor(0, 1);
  oled.print(F(" Shower "));
  oled.setCursor(0, 4);
  oled.print(F("  Check "));
#ifndef TEST_ANALOG_IN
  delay(GREET_MILLISECONDS);
#endif
}

#ifndef TEST_ANALOG_IN
// 16 Hz
void initTimer()
{
  TCNT1 = 0;
  TCCR1 = 0;

  OCR1A = 244;
  TCCR1 = 0x8D;
  TCCR1 |= (1 << CTC1);
  TIMSK |= (1 << OCIE1A);
}
#endif

#ifndef TEST_ANALOG_IN
ISR(TIMER1_COMPA_vect)
{
  noInterrupts();
  timer1value++;
  if (timer1value % 8 == 0)
  {
    tickBlink();
  }
  if (timer1value >= 16)
  {
    timer1value = 0;
    tickSecond();
  }
  interrupts();
}
#endif

void tickSecond()
{
  seconds++;
  displayIsDirty = true;

  if (seconds % PAGE_SWITCH_EVERY_SECONDS == 0)
  {
    setNextPage();
  }

  if (state == STATE_ON || state == STATE_WAIT_FOR_OFF)
  {
    secondsOn++;
    centiCents += CENTICENT_PER_SECOND;
  }

  if (state == STATE_WAIT_FOR_OFF)
  {
    timerOffDetection++;
  }

  if (state == STATE_WAIT_FOR_FINISH)
  {
    timerFinishedDetection++;
  }
}

uint8_t determinePhase()
{
  int analog = analogRead(ANALOG_PIN);

  if (analog > ANALOG_THRESHOLD)
  {
    timerOffDetection = 0;
    timerFinishedDetection = 0;
    return STATE_ON;
  }

  // analog is below threshold

  if (state == STATE_ON)
  {
    timerOffDetection = 0;
    return STATE_WAIT_FOR_OFF;
  }

  if (state == STATE_WAIT_FOR_OFF && timerOffDetection >= OFF_AFTER_SECONDS)
  {
    timerFinishedDetection = 0;
    return STATE_WAIT_FOR_FINISH;
  }

  if (state == STATE_WAIT_FOR_FINISH && timerFinishedDetection > FINISHED_AFTER_OFF_SECONDS)
  {
    reset();
    return STATE_TO_IDLE;
  }

  // state has not changed
  return state;
}

void updateDisplay()
{
  static uint8_t pageBefore;

  // displayIsDirty is set once per second in tickSecond().
  if (!displayIsDirty || state == STATE_IDLE)
  {
    return;
  }
  displayIsDirty = false;
  updateCount++;

  if (state == STATE_TO_IDLE)
  {
    oled.clear();
    oled.setCursor(0, 0);
    oled.print(F("."));
    oled.setContrast(0);
    state = STATE_IDLE;
    return;
  }

  noInterrupts();

  if (currentPage != pageBefore)
  {
    switch (currentPage)
    {
    case PAGE_COST:
      beginCostPage();
      break;
    case PAGE_TIME:
      beginTimePage();
    }
  }
  pageBefore = currentPage;

  switch (currentPage)
  {
  case PAGE_COST:
    updateCostPage();
    break;
  case PAGE_TIME:
    updateTimePage();
  }
  debugOnOff();
  interrupts();
}

void debugOnOff()
{
  oled.setCursor(56, 5);
  oled.startData();
  oled.sendData(state == STATE_ON || state == STATE_TO_IDLE ? 0b10000000 : 0);
  oled.endData();
}

void debugPhase()
{
  oled.setCursor(56, 4);
  char s[] = "  ";
  itoa(state, s, 10);
  oled.print(s);
}

void debugSecondsOn()
{
  oled.setCursor(48, 4);
  char s[] = "    ";
  itoa(secondsOn, s, 10);
  oled.print(s);
}

void debugUpdateCount()
{
  oled.setCursor(48, 4);
  char s[] = "    ";
  itoa(updateCount, s, 10);
  oled.print(s);
}

void tickBlink()
{
  if (blinkState < 1)
  {
    return;
  }
  if (blinkState == 1)
  {
    oled.setContrast(0);
    blinkState++;
    return;
  }
  if (blinkState == 2)
  {
    oled.setContrast(255);
    blinkState = 1;
  }
}

void blinkOn()
{
  if (blinkState == 0)
  {
    blinkState = 1;
  }
}

void setNextPage()
{
  switch (currentPage)
  {
  case PAGE_COST:
    currentPage = PAGE_TIME;
    break;
  case PAGE_TIME:
    currentPage = PAGE_COST;
  }
}

void updateCostPage()
{
  oled.setCursor(12, 0);
  oled.print(getEuroString(centiCents / 100));
  oled.print(F(","));
  oled.print(getCentString(centiCents / 100));
  oled.clearToEOL();

  oled.setCursor(12, 4);
  oled.print(getEuroString((centiCents / 100) * DAYS_PER_YEAR));
  oled.clearToEOL();
}

char *getEuroString(uint16_t cents)
{
  static char euroString[] = "...";
  itoa(cents / 100, euroString, 10);
  return euroString;
}

char *getCentString(uint16_t cents)
{
  static char centString[] = "   ";

  if (cents < 10)
  {
    itoa(cents, &(centString[1]), 10);
    centString[0] = '0';
  }
  else
  {
    itoa(cents, centString, 10);
  }
  return centString;
}

void beginCostPage()
{
  oled.setContrast(255);
  oled.clear();
  drawEuro(0, 0);
  oled.setCursor(0, 2);
  oled.print(F("Jahr:"));
  drawEuro(0, 4);
}

void beginTimePage()
{
  oled.clear();
  // oled.setContrast(255);
  oled.setCursor(0, 0);
}

void updateTimePage()
{
  oled.setCursor(4, 1);
  oled.setSpacing(3);
  oled.print(getHoursString());
  oled.print(F(":"));
  oled.print(getSecondsString());
  oled.setSpacing(0);
  oled.setCursor(10, 4);
  if (secondsOn < SECONDS_UNTIL_UNHAPPY)
  {
    oled.print(F(":-)"));
  }
  else if (secondsOn < SECONDS_UNTIL_WARN)
  {
    oled.print(F(":-|"));
  }
  else
  {
    oled.print(F(":-("));
    blinkOn();
  }
}

char *getHoursString()
{
  static char hoursString[] = "   ";

  uint16_t clockHours = secondsOn / 60;
  if (clockHours < 10)
  {
    itoa(clockHours, &(hoursString[1]), 10);
    hoursString[0] = '0';
  }
  else
  {
    itoa(clockHours, hoursString, 10);
  }
  return hoursString;
}

char *getSecondsString()
{
  static char secondsString[] = "   ";

  uint8_t clockSeconds = secondsOn % 60;
  if (clockSeconds < 10)
  {
    itoa(clockSeconds, &(secondsString[1]), 10);
    secondsString[0] = '0';
  }
  else
  {
    itoa(clockSeconds, secondsString, 10);
  }
  return secondsString;
}

void drawEuro(uint8_t x, uint8_t y)
{
  oled.setCursor(x, y);
  oled.startData();
  oled.sendData(0x40);
  oled.sendData(0xf0);
  oled.sendData(0xf8);
  oled.sendData(0x5c);
  oled.sendData(0x4c);
  oled.sendData(0x4c);
  oled.sendData(0x08);
  oled.endData();
  oled.setCursor(x, y + 1);
  oled.startData();
  oled.sendData(0x01);
  oled.sendData(0x0f);
  oled.sendData(0x1f);
  oled.sendData(0x39);
  oled.sendData(0x31);
  oled.sendData(0x30);
  oled.sendData(0x10);
  oled.endData();
}
