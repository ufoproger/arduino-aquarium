#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

//#define DISABLE_EEPROM_IO
//#define DISABLE_TEMPERATURE_SENSOR

#define RELAY_ON  0
#define RELAY_OFF 1

/* Структура для обработки нажатия кнопки через прерывание. */
struct ButtonHandler
{
  // Минимальное время нажатия кнопки в миллисекундах.
  const int pressShortInterval = 1000;
  const int pressLongInterval = 2000;

  bool isPressed; 
  int pressMillis;
  int pin;
  void (*shortCallback)(void);
  void (*longCallback)(void);

  ButtonHandler(int pin_, void (*shortCallback_)(void), void (*longCallback_)(void)):
    pin(pin_),
    shortCallback(shortCallback_),
    longCallback(longCallback_),
    pressMillis(0),
    isPressed(false)
  {
  }

  // Возвращает истину, если нажатие кнопки сработало.
  void loop()
  {
    if (digitalRead(pin) == HIGH)
    {
      if (!isPressed)
      {
        pressMillis = millis();
        isPressed = true;
      }
      
      return;
    }
       
    if (!isPressed)
      return;

    isPressed = false;
    
    int interval = millis() - pressMillis;
    
    if (interval < pressShortInterval)
      return;

//    if (constrain(interval, pressShortInterval, pressLongInterval))
//      shortCallback();

    if (interval > pressLongInterval)
    {
      longCallback();
      return;
    }

    if (interval > pressShortInterval)
    {
      shortCallback();
      return;
    }
  }
};

/* Класс для отображения текущего состояния реле (либо работает, либо сколько времени не работает). */
class RelayTimer
{    
  const unsigned long SLEEPING_MILLIS = 3600000;
  
  private:
    int pin;
    unsigned long startMillis;
    unsigned long startSleepingMillis;
    bool isWorking;
    bool isSleeping;
    
    String leadingZeroes(unsigned long number, int cols = 2)
    {
      String result(number);

      while (result.length() < cols)
        result = "0" + result;

      return result;
    }
    
  public:
    RelayTimer(int pin_):
      pin(pin_),
      startMillis(0),
      startSleepingMillis(0),
      isWorking(false),
      isSleeping(false)
    {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, RELAY_OFF);
    }

    bool getSleeping()
    {
      return isSleeping;
    }

    void setSleeping(bool value)
    {
      if (!isSleeping && value)
      {
        startSleepingMillis = millis();
      }
      
      isSleeping = value;
    }

    // Метод, который вызывается из глобальной функции loop().
    void loop()
    {
      bool currentWorking = (digitalRead(pin) == RELAY_ON);

      if (isWorking && !currentWorking)
        startMillis = (millis() / 1000) * 1000;

      isWorking = currentWorking;

      if (isSleeping && (startSleepingMillis + SLEEPING_MILLIS < millis()))
        isSleeping = false;
    }

    // Метод для формирования строки со статусом реле.
    String show()
    {
      if (isSleeping)
        return "SLEEPING";
        
      if (isWorking)
        return "WORKING ";
 
      unsigned long seconds = (millis() - startMillis) / 1000;
      unsigned long minutes = (seconds / 60) % 60;
      unsigned long hours = (seconds / 60 / 60);

      seconds %= 60;
      
      String result = leadingZeroes(hours) + ":" + leadingZeroes(minutes) + ":" + leadingZeroes(seconds);

      if (result.length() > 8)
        result = "--:--:--";

      // На всякий случай.
      while (result.length() < 8)
        result += ".";
        
      return result;
    }
};

/* Дисплей 16x2:
 *  RS - 6 пин;
 *  E - 7 пин;
 *  D4 - 8 пин;
 *  D5 - 9 пин;
 *  D6 - 10 пин;
 *  D7 - 11 пин.
 */
LiquidCrystal lcd(6, 7, 8, 9, 10, 11);

/* Датчик температуры - 12 пин */
OneWire oneWire(12);
DallasTemperature sensor(&oneWire);
/* Пины реле охлаждения и обогревателя соответственно. */
int relayPins[2] = { 4, 5 };

RelayTimer timers[2] = { RelayTimer(relayPins[0]), RelayTimer(relayPins[1]) };


void toggleSleeping(int index)
{
  bool value = timers[index].getSleeping();
  
  timers[index].setSleeping(!value);
}

void buttonUpLong()
{
  toggleSleeping(0);
}

void buttonDownLong()
{
  toggleSleeping(1);
}

void buttonUpShort()
{
  changeTargetTemperature(true);
}

void buttonDownShort()
{
  changeTargetTemperature(false);
}

/* Кнопки установки температуры:
 *  Кнопка вверх, 2 пин, 0 прерывание;
 *  Кнопка вниз, 3 пин, 1 прерывание.
 */
ButtonHandler buttons[2] = {
  ButtonHandler(2, buttonUpShort, buttonUpLong), 
  ButtonHandler(3, buttonDownShort, buttonDownLong) 
};
 
int targetTemperature;
int targetTemperatureRange[2] = { 20, 30 };

// Расположение ячейки EEPROM с последней температурой
int targetTemperatureCell = 0;
int currentTemperatureRange[2] = { 10, 50 };
bool lastDisplayState = 1;

void setup()
{
  Serial.begin(9600);
  
#ifndef DISABLE_EEPROM_IO
  uint8_t savedTargetTemperature = EEPROM.read(targetTemperatureCell);

  if (savedTargetTemperature == 255)
    targetTemperature = (targetTemperatureRange[0] + targetTemperatureRange[1]) / 2;
  else
    targetTemperature = (int)savedTargetTemperature;
#else
  targetTemperature = 24;
#endif

  for (int i = 0, count = sizeof(buttons) / sizeof(ButtonHandler); i < count; ++i)
  {
    pinMode(buttons[i].pin, INPUT);
//    attachInterrupt(i, buttons[i].callback, CHANGE); 
  }

  sensor.begin();
  lcd.begin(16, 2);
}

#ifdef DISABLE_TEMPERATURE_SENSOR
float cT = 25.5;
#endif

void loop()
{
  for (int i = 0, count = sizeof(buttons) / sizeof(ButtonHandler); i < count; ++i)
    buttons[i].loop();
  
  if (Serial.available())
  {
    switch (Serial.read())
    {
      // char "+"
      case 43:
        changeTargetTemperature(true);
        break;

      // char "-"
      case 45:
        changeTargetTemperature(false);
        break;
#ifdef DISABLE_TEMPERATURE_SENSOR
      // char "<"
      case 60:
        cT -= 0.1;
        break;

      // char ">"
      case 62:
        cT += 0.1;
        break;
#endif
    }
  }
  
  sensor.requestTemperatures();
  
#ifdef DISABLE_TEMPERATURE_SENSOR
  float currentTemperature = cT;
#else
  float currentTemperature = sensor.getTempCByIndex(0);
#endif

  if (constrain(currentTemperature, currentTemperatureRange[0], currentTemperatureRange[1]) != currentTemperature)
  {
    if (lastDisplayState != 1)
    {
      lastDisplayState = 1;
      lcd.clear();
    }
    
    lcd.clear();
    lcd.print("    INVALID");
    lcd.setCursor(0, 1);
    lcd.print("  TEMPERATURE");
    
    for (int i = 0; i < 2; ++i)
      digitalWrite(relayPins[i], HIGH);
      
    return;
  }

  if (lastDisplayState != 0)
  {
    lastDisplayState = 0;
    
    lcd.clear();
    lcd.setCursor(6, 0);
    lcd.print("F");
    lcd.setCursor(6, 1);
    lcd.print("H");
  }

  for (int i = 0, count = sizeof(timers) / sizeof(RelayTimer); i < count; ++i)
    timers[i].loop();
    
  for (int i = 0; i < 2; ++i)
  {
    lcd.setCursor(8, i);
    lcd.print(timers[i].show());
  }
    
  lcd.setCursor(0, 0);
  lcd.print(currentTemperature, 1);
  lcd.print("\xDF");
  lcd.setCursor(0, 1);
  lcd.print(targetTemperature);
  lcd.print("\xDF");
  lcd.setCursor(4, 1);

  int isFan = RELAY_OFF;
  int isHeater = RELAY_OFF;
  int t = (int)(currentTemperature + 0.01);

  if (t == targetTemperature)
  {
    int isFanWorking = digitalRead(relayPins[0]);
    int isHeaterWorking = digitalRead(relayPins[1]);

    // Если температура с датчика в пределах желаемой, но какое-нибудь включено, то продолжаем приводить температуру ближе к центру желаемой
    if ((isFanWorking == RELAY_ON || isHeaterWorking == RELAY_ON) && (currentTemperature != constrain(currentTemperature, t + 0.3, t + 0.7)))
    {
      isFan = isFanWorking;
      isHeater = isHeaterWorking;
      lcd.print("-");
    }    
    else
      lcd.print("=");
  }
  else
  {
    if (t < targetTemperature)
    {
      lcd.print("<");
      isHeater = RELAY_ON;
    }
    else
    {
      lcd.print(">");
      isFan = RELAY_ON;
    }
  }

  if (timers[0].getSleeping())
    isFan = RELAY_OFF;// &= !timers[0].getSleeping();

  if (timers[1].getSleeping())
    isHeater = RELAY_OFF;
  //isHeater &= !timers[1].getSleeping();
  
  digitalWrite(relayPins[0], (int)isFan);
  digitalWrite(relayPins[1], (int)isHeater);  
}

void changeTargetTemperature(bool isChangeToUp)
{
  targetTemperature += (isChangeToUp) ? (1) : (-1);
  targetTemperature = constrain(targetTemperature, targetTemperatureRange[0], targetTemperatureRange[1]);
  
#ifndef DISABLE_EEPROM_IO
  EEPROM.write(targetTemperatureCell, (uint8_t)targetTemperature);
#endif  
}
