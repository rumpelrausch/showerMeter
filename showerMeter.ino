#include <Tiny4kOLED.h>

#define PAGE_SWITCH_EVERY_SECONDS 5
#define SECONDS_UNTIL_UNHAPPY 360
#define SECONDS_UNTIL_WARN 420
#define CENT_PER_KWH 36
#define HEATER_KW 21
#define DAYS_PER_YEAR 160
#define OFF_AFTER_SECONDS 4
#define FINISHED_AFTER_OFF_SECONDS 60
#define ANALOG_THRESHOLD 80

// ###############################################
// ###############################################
// ###############################################

// #define TEST_ANALOG_IN
#define ANALOG_PIN 3
#define PAGE_COST 1
#define PAGE_TIME 2
#define CENTICENT_PER_SECOND (CENT_PER_KWH * HEATER_KW) / 36

uint8_t currentPage = PAGE_COST;
unsigned long lastMillis = 0;
volatile uint16_t milliseconds = 0;
volatile uint16_t seconds = 0;
volatile uint16_t secondsOn = 0;
volatile uint16_t timerOffDetection = 0;      // > 0: Timer is running
volatile uint16_t timerFinishedDetection = 0; // > 0: Timer is running
volatile uint8_t blinkState = 0;
volatile uint16_t centiCents = 0;

void setup()
{
  pinMode(ANALOG_PIN, INPUT);

  oled.begin(64, 48, sizeof(tiny4koled_init_64x48), tiny4koled_init_64x48);
  oled.setRotation(1);
  oled.setFont(FONT8X16);
  oled.clear();
  oled.on();
  greet();

  setNextPage();
  initTimer();
}

void greet()
{
  oled.setContrast(255);
  oled.setCursor(0, 1);
  oled.print(F(" Shower "));
  oled.setCursor(0, 4);
  oled.print(F("  Check "));
#ifndef TEST_ANALOG_IN
  delay(5000);
#endif
}

void loop()
{
  int analog = checkOnOff();
#ifdef TEST_ANALOG_IN
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
  delay(timerFinishedDetection == 0 ? 2000 : 100); // slow poll if off
#endif
}

void initTimer()
{
  noInterrupts();
  // Clear registers
  TCNT1 = 0;
  TCCR1 = 0;

  // 1000 Hz (16000000/((124+1)*128))
  OCR1C = 124;
  // interrupt COMPA
  OCR1A = OCR1C;
  // CTC
  TCCR1 |= (1 << CTC1);
  // Prescaler 128
  TCCR1 |= (1 << CS13);
  // Output Compare Match A Interrupt Enable
  TIMSK |= (1 << OCIE1A);
  interrupts();
}

#ifndef TEST_ANALOG_IN
ISR(TIMER1_COMPA_vect)
{
  milliseconds++;
  if (milliseconds == 1000)
  {
    milliseconds = 0;
    seconds++;
    if (timerOffDetection > 0)
    {
      timerOffDetection++;
      secondsOn++;
      centiCents += CENTICENT_PER_SECOND;
    }
    if (timerFinishedDetection > 0)
    {
      timerFinishedDetection++;
    }
    updateDisplay();
  }
  if (milliseconds % 150 == 0)
  {
    blink();
  }
}
#endif

int checkOnOff()
{
  int analog = analogRead(ANALOG_PIN);
  if (analog > ANALOG_THRESHOLD)
  {
    timerOffDetection = 1;
    return analog;
  }

  if (timerOffDetection >= OFF_AFTER_SECONDS)
  {
    timerOffDetection = 0;
    timerFinishedDetection = 1;
  }

  if (timerFinishedDetection > FINISHED_AFTER_OFF_SECONDS)
  {
    timerFinishedDetection = 0;
    secondsOn = 0;
    seconds = 0;
    centiCents = 0;
  }
  return analog;
}

void updateDisplay()
{
  static bool wasOn = true;
  if (secondsOn == 0)
  {
    if (wasOn)
    {
      wasOn = false;
      oled.clear();
      oled.setCursor(0, 0);
      oled.print(F("."));
      oled.setContrast(0);
    }
    return;
  }

  if (!wasOn && secondsOn > 0)
  {
    wasOn = true;
    oled.clear();
    oled.setContrast(255);
    currentPage = PAGE_COST;
    seconds = 0;
  }

  if (seconds % PAGE_SWITCH_EVERY_SECONDS == 0)
  {
    setNextPage();
  }
  switch (currentPage)
  {
  case PAGE_COST:
    updateCostPage();
    break;
  case PAGE_TIME:
    updateTimePage();
  }

  oled.setCursor(56, 5);
  // oled.print(timerOffDetection > 0 ? F("o") : F(" "));
  oled.startData();
  oled.sendData(timerOffDetection > 0 ? 0b10000000 : 0);
  oled.endData();
}

void blink()
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
    beginTimePage();
    break;
  case PAGE_TIME:
    currentPage = PAGE_COST;
    beginCostPage();
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
  oled.clear();
  drawEuro(0, 0);
  oled.setCursor(0, 2);
  oled.print(F("Jahr:"));
  drawEuro(0, 4);
}

void beginTimePage()
{
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
