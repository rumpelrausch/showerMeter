#include <Tiny4kOLED.h>

#define CENT_PER_KWH 36
#define HEATER_KW 21
#define DAYS_PER_YEAR 160

#define OFF_AFTER_SECONDS 3
#define FINISHED_AFTER_OFF_SECONDS 60
#define ANALOG_THRESHOLD 20

#define PAGE_SWITCH_EVERY_SECONDS 4
#define SECONDS_UNTIL_UNHAPPY 360
#define SECONDS_UNTIL_WARN 420

#define GREET_MILLISECONDS 5000
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
volatile bool blinkState;
volatile bool displayIsDirty;
volatile bool shallBlink;

void setup()
{
  reset();
  state = STATE_TO_IDLE;
  pinMode(ANALOG_PIN, INPUT);

  oled.begin(64, 48, sizeof(tiny4koled_init_64x48), tiny4koled_init_64x48);
  oled.setRotation(1);
  oled.setFont(FONT8X16);
  oled.on();
  oled.clear();

#ifndef TEST_ANALOG_IN
  setNextPage();
  initTimer();
#endif
}

void loop()
{
#ifdef TEST_ANALOG_IN
  int analog = getAverageAnalog();
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
  delay(FAST_POLL_MILLISECONDS);
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
  displayIsDirty = true;
  blinkState = false;
  shallBlink = false;
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
  oled.clear();
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
  if (timer1value % 4 == 0)
  {
    shallBlink = true;
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

int getAverageAnalog()
{
  static uint8_t numOfPreReads = 0;
  static int values[4];
  uint8_t pos;
  int analog;

  // shift up
  for (pos = 3; pos > 0; pos--)
  {
    values[pos] = values[pos - 1];
  }

  values[0] = analogRead(ANALOG_PIN);
  if (numOfPreReads > 3)
  {
    analog = 0;
    for (pos = 0; pos < 4; pos++)
    {
      analog += values[pos];
    }
    return analog / 4;
  }

  numOfPreReads++;
  return 0;
}

uint8_t determinePhase()
{
  if (getAverageAnalog() > ANALOG_THRESHOLD)
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
  void(*pointedFunction)();

  noInterrupts();
  
  tickBlink();

  // displayIsDirty is set once per second in tickSecond().
  if (!displayIsDirty)
  {
    return;
  }
  displayIsDirty = false;

  if (state == STATE_IDLE)
  {
    oled.setContrast(0);
    oled.setCursor(63, 5);
    oled.startData();
    oled.sendData((uint8_t) seconds);
    oled.endData();
    return;
  }

  if (state == STATE_TO_IDLE)
  {
    greet();
    state = STATE_IDLE;
    return;
  }

  if (currentPage != pageBefore)
  {
    pointedFunction = currentPage == PAGE_COST ? &beginCostPage : &beginTimePage;
    pointedFunction();
  }
  pageBefore = currentPage;

  pointedFunction = currentPage == PAGE_COST ? &updateCostPage : &updateTimePage;
  pointedFunction();

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

void tickBlink()
{
  if (!blinkState || !shallBlink)
  {
    return;
  }
  oled.setContrast(blinkState ? 255 : 0);
  blinkState = !blinkState;
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
  currentPage = currentPage == PAGE_COST ? PAGE_TIME : PAGE_COST;
}

void updateCostPage()
{
  uint16_t totalCents = centiCents / 100;
  oled.setCursor(12, 0);
  oled.print(getEuroString(totalCents));
  oled.print(F(","));
  oled.print(getCentString(totalCents));
  oled.clearToEOL();

  oled.setCursor(12, 4);
  oled.print(getEuroString(totalCents * DAYS_PER_YEAR));
  oled.clearToEOL();
}

char *getEuroString(uint16_t totalCents)
{
  static char euroString[] = "...";
  itoa(totalCents / 100, euroString, 10);
  return euroString;
}

char *getCentString(uint16_t totalCents)
{
  uint8_t realCents = totalCents % 100;
  static char centString[] = "   ";

  if (realCents < 10)
  {
    itoa(realCents, &(centString[1]), 10);
    centString[0] = '0';
  }
  else
  {
    itoa(realCents, centString, 10);
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
  oled.setContrast(255);
  oled.clear();
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
