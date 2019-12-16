//sudo stty -F /dev/rfcomm26 921600 cs8 -icrnl -ixon -ixoff -opost -isig -icanon -echo ;

//sudo stty -F /dev/rfcomm1 921600 -icrnl -ixon -ixoff -opost -isig -icanon -echo;
//sudo cat /dev/rfcomm1 |  aplay -f S16_LE -c 2 -r 22050

#define DEBUG
#define PROTOCOLVERSION     1
#define BITS                32 // 16 or 32
#define FS                  16000
#define BLOCK_SIZE          64 // in samples for one channel!!! (max value: 64 (for 32 Bit), 128 (for 16 Bit)

#include <Arduino.h>   // required before wiring_private.h
#include "wiring_private.h" // pinPeripheral() function
#include "SparkFunbc127.h"
#include <I2S.h>
#include "FlashAsEEPROM.h"
#include "StRingBuffer.h"
Uart Serial2 (&sercom1, 11, 10, SERCOM_RX_PAD_0, UART_TX_PAD_2);
void SERCOM1_Handler() {
  Serial2.IrqHandler();
}

enum connectionState {UNDEF, INITIALIZING_BC127, INITIALIZED_BC127, WAIT_FOR_CONFIG_MODE, CONFIG_MODE, START_ACTIVE_MODE, ACTIVE_MODE_WAIT_FOR_CONNECTION, ACTIVE_MODE, RECONNECT, IDLE};
connectionState currConnectionState = UNDEF;

bool LED_on = LOW;
#define serialPort  Serial2
#ifdef DEBUG
#define debugPort  Serial
#endif
int ledRed = 6;
int ledGreen = 12;
int ledBlue = 21;
int resetPin = 20;

BC127 BTModu(&serialPort);
StRingBuffer serialIn = StRingBuffer(50);

const int additionalBytesCount = 12;

union {
  byte bytes[BLOCK_SIZE * BITS / 4];
#if BITS == 32
  int32_t ints[sizeof(bytes) / sizeof(bytes[0]) / 4];
#else
  int16_t ints[sizeof(bytes) / sizeof(bytes[0]) / 2];
#endif
} sample_buffer_receive;
union {
  byte bytes[BLOCK_SIZE * 4 + additionalBytesCount];
  int16_t ints[sizeof(bytes) / sizeof(bytes[0]) / 2];
} sample_buffer_send;
typedef struct {
  char value[13];
} MAC;
FlashStorage(stored_BluetoothDevice_MAC, MAC);

unsigned long resetTime = millis();
unsigned long lastConnectionAttempt = millis();
unsigned long waitingForConnection = millis();

void setup()
{
#ifdef DEBUG
  debugPort.begin(9600);
#endif
  pinMode(ledBlue, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledRed, OUTPUT);
  pinMode(resetPin, OUTPUT);
  if (!I2S.begin(I2S_PHILIPS_MODE, FS, BITS))
  {
    debug("Failed to initialize I2S!");
    while (1)
      LED_blink(ledRed, 200, 500); // do nothing; red error blink
  }
}

void loop()
{
  String value = "";
  String UART_CONFIG;
  MAC tmp_MAC;
  int sizeOfIntArray = sizeof(sample_buffer_send.ints) / sizeof(sample_buffer_send.ints[0]);
  int16_t volume = 0;
  if (BITS == 16)
    volume = 16;
  int16_t blockCounter = 0;
  bool adaptiveBitShift = false;
  String BluetoothDevice_MAC = "";
  int checksum = 0;
  long reconnectionPause = 0;
  while (true)
  {
    switch (currConnectionState)
    {
      case UNDEF:
        resetTimer();
        blue();
        resetBC127();
        serialPort.begin(921600);
        pinPeripheral(10, PIO_SERCOM);
        pinPeripheral(11, PIO_SERCOM);
        while (!serialPort) {}
        BTModu.restore();
        serialPort.end();
        debug("state = INITIALIZING_BC127");
        currConnectionState = INITIALIZING_BC127;
        break;
      case INITIALIZING_BC127:
        if (millis() - resetTime > 120000) // 2 minutes
          resetSystem();
        else
        {
          serialPort.begin(9600);
          pinPeripheral(10, PIO_SERCOM);
          pinPeripheral(11, PIO_SERCOM);
          while (!serialPort) {}

          BTModu.stdGetParam("UART_CONFIG", &value);
          debug("GET UART_CONFIG -> " + value);
          if (value != "")
          {
            UART_CONFIG = "921600 OFF 0";
            debug("SET UART_CONFIG = " + UART_CONFIG);
            BTModu.stdSetParam("UART_CONFIG", UART_CONFIG);
            serialPort.flush();
            serialPort.end();
            serialPort.begin(921600);
            pinPeripheral(10, PIO_SERCOM);
            pinPeripheral(11, PIO_SERCOM);
            while (!serialPort) {}
            BTModu.stdSetParam("PROFILES", "0 0 0 0 0 0 1 0 0 0 0 0");
            BTModu.stdSetParam("ENABLE_LED", "OFF");
            BTModu.stdSetParam("GPIO_CONFIG", "OFF 0 254");
            BTModu.stdSetParam("HFP_CONFIG", "OFF OFF OFF OFF OFF");
            BTModu.writeConfig();
            BTModu.reset();
            delay(500);
            debug("state = INITIALIZED_BC127");
            currConnectionState = INITIALIZED_BC127;
            resetTimer();
          }
          else
          {
            LedsOff();
            restoreBC127();
          }
        }
        break;
      case INITIALIZED_BC127:
        BTModu.stdGetParam("UART_CONFIG", &value);
        debug("GET UART_CONFIG -> " + value);
        if (value == UART_CONFIG)
        {
          BTModu.stdSetParam("ENABLE_BATT_IND", "OFF");
          BTModu.stdSetParam("ENABLE_LED", "OFF");
          BTModu.stdSetParam("HIGH_SPEED", "ON OFF");
          //BTModu.stdSetParam("DISCOVERABLE", "ON");
          BTModu.stdGetConfig(&value);
          debug(value);
          resetTimer();
          debug("waiting for SPP...");
          debug("state = WAIT_FOR_CONFIG_MODE");
          currConnectionState = WAIT_FOR_CONFIG_MODE;
          resetTimer();
          value = "";
        }
        else
        {
          debug("state = INITIALIZING_BC127");
          currConnectionState = INITIALIZING_BC127;
          resetTimer();
        }
        break;
      case WAIT_FOR_CONFIG_MODE:
        if (millis() - resetTime > 7500) // 7.5 seconds
        {
          resetTimer();
          debug("state = START_ACTIVE_MODE");
          currConnectionState = START_ACTIVE_MODE;
        }
        else
        {
          LED_blink(ledBlue, 100, 500);
          value = "";
          BTModu.stdGetCommand("STATUS", "SPP", &value);
          if (value.indexOf("CONNECTED SPP") >= 0)
          {
            BluetoothDevice_MAC = value.substring(14, value.indexOf("COMMAND") - 1);
            BluetoothDevice_MAC.trim();
            debug("SPP connected!");
            debug("ENTER_DATA_MODE 15...");
            BTModu.stdCmd("ENTER_DATA_MODE 15");
            serialPort.flush();
            while (serialPort.available())
              serialPort.read();
            serialIn.clear();
            debug("state = CONFIG_MODE");
            currConnectionState = CONFIG_MODE;
            resetTimer();
          }
        }
        break;
      case CONFIG_MODE:
        LED_blink(ledBlue, 100, 200);
        if (millis() - resetTime > 10000) // 10 seconds
        {
          resetTimer();
          debug("state = START_ACTIVE_MODE");
          currConnectionState = START_ACTIVE_MODE;
        }
        else if (serialPort.available())
        {
          value = serialIn.addChar(serialPort.read());
          if (value.substring(value.length() - 8) == "STOREMAC")
          {
            BluetoothDevice_MAC.toCharArray(tmp_MAC.value, 13);
            stored_BluetoothDevice_MAC.write(tmp_MAC);
            debug("'" + BluetoothDevice_MAC + "' stored in Flash Storage");
            for (int count = 0; count < 5; count++)
            {
              green(); delay(250);
              blue(); delay(250);
            }
            BTModu.exitDataMode();
            debug("state = START_ACTIVE_MODE");
            currConnectionState = START_ACTIVE_MODE;
            resetTimer();
          }
        }
        break;
      case START_ACTIVE_MODE:
        tmp_MAC = stored_BluetoothDevice_MAC.read();
        BluetoothDevice_MAC = String(tmp_MAC.value);
        if (!BluetoothDevice_MAC || BluetoothDevice_MAC == "")
        {
          debug("\n!!! BluetoothDevice_MAC is epmty !!!");
          debug("!!!   ACTIVE MODE NOT POSSIBLE   !!!");
          while (1)
            LED_blink(ledRed, 200, 1000); // do nothing; red error blink
        }
        debug("state = ACTIVE_MODE_WAIT_FOR_CONNECTION");
        currConnectionState = ACTIVE_MODE_WAIT_FOR_CONNECTION;
        waitingForConnection = millis();
        debug("Attempt to connect: " + BluetoothDevice_MAC);
        resetTimer();
        value = "";
        break;
      case ACTIVE_MODE_WAIT_FOR_CONNECTION:
        if (millis() - waitingForConnection > 5 * 60000) // 5 Minutes
        {
          reconnectionPause = 10000;
          LED_blink(ledGreen, 100, 10000);
        }
        else
        {
          reconnectionPause = 1000;
          LED_blink(ledGreen, 100, 5000);
        }
        if (millis() - lastConnectionAttempt > reconnectionPause) // 1 seconds
        {
          lastConnectionAttempt = millis();
          serialPort.print("OPEN " + BluetoothDevice_MAC + " SPP\r");
          if (serialPort.available())
          {
            while (serialPort.available())
              value += char(serialPort.read());
            if (value.indexOf("OK 15 SPP") >= 0)
            {
              green();
              debug("ENTER_DATA_MODE 15...");
              BTModu.stdCmd("ENTER_DATA_MODE 15");
              delay(200);
              serialPort.flush();
              while (serialPort.available())
                serialPort.read();
              serialIn.clear();
              delay(800);
              debug("state = ACTIVE_MODE");
              currConnectionState = ACTIVE_MODE;
              blockCounter = 0;
              resetTimer();
            }
          }
          else
          {
            debug("state = INITIALIZING_BC127");
            currConnectionState = INITIALIZING_BC127;
          }
        }
        break;
      case ACTIVE_MODE:
        if (I2S.available() >= BLOCK_SIZE * BITS / 4)
        {
          I2S.read(sample_buffer_receive.bytes, BLOCK_SIZE * BITS / 4);
          if (serialPort.available())
          {
            value = serialIn.addChar(serialPort.read());
            if (value.substring(value.length() - 15) == "CLOSE_OK 15 SPP" || value.substring(value.length() - 25, value.length() - 20) == "ERROR")
            {
              String tmp =  String(value); tmp.trim();
              debug(String("Connection lost: ") + tmp);
              debug("state = ACTIVE_MODE_WAIT_FOR_CONNECTION");
              currConnectionState = ACTIVE_MODE_WAIT_FOR_CONNECTION;
              waitingForConnection = millis();
              resetTimer();
            }
            else if (value.charAt(value.length() - 3) == 'V' &&  isDigit(value.charAt(value.length() - 1)))
            {
              int16_t newVol = int16_t(value.charAt(value.length() - 1) - '0');
              if (value.charAt(value.length() - 2) == '+')
                volume = newVol;
              else if (value.charAt(value.length() - 2) == '-')
                volume = -newVol;
              debug("Volume = " + String(volume));
            }
            else if (value.charAt(value.length() - 2) == 'B' &&  isDigit(value.charAt(value.length() - 1)))
            {
              adaptiveBitShift = bool(value.charAt(value.length() - 1) - '0');
              debug("Adaptive Bit Shift = " + String(adaptiveBitShift));
              if (adaptiveBitShift == false)
                volume = 0;
            }
          }
          if (adaptiveBitShift)
          {
            int maxVal = 1;
            for (int count = 0; count < BLOCK_SIZE * 2; count++)
              if (sample_buffer_receive.ints[count] != 0)
                maxVal = max(maxVal, abs(sample_buffer_receive.ints[count]));
            volume = 30 - (int)log2(maxVal);
          }
          checksum = 0;
          sample_buffer_send.ints[0] = 0xFF << 8 | 0x7F;
          for (int count = 0; count < BLOCK_SIZE * 2; count++)
          {
            sample_buffer_send.ints[count + 1] = int16_t(sample_buffer_receive.ints[count] >> min(max(0, 16 - volume), 31));
            checksum ^= sample_buffer_send.ints[count + 1];
          }
          sample_buffer_send.ints[sizeOfIntArray - 5] = volume;
          sample_buffer_send.ints[sizeOfIntArray - 4] = ++blockCounter;
          sample_buffer_send.ints[sizeOfIntArray - 3] =  PROTOCOLVERSION;
          sample_buffer_send.ints[sizeOfIntArray - 2] = checksum;
          sample_buffer_send.ints[sizeOfIntArray - 1] =  0x00 << 8 | 0x80;
          for (int count = -5; count <= -3; count ++)
            checksum ^= sample_buffer_send.ints[sizeOfIntArray + count];
          serialPort.write(sample_buffer_send.bytes, sizeOfIntArray * 2);
        }
        break;
    }
  }
}

void red()
{
  LedsOff();
  digitalWrite(ledRed, LED_on);
  delay(10);
}

void blue()
{
  LedsOff();
  digitalWrite(ledBlue, LED_on);
  delay(10);
}

void green()
{
  LedsOff();
  digitalWrite(ledGreen, LED_on);
  delay(10);
}

void LedsOff()
{
  digitalWrite(ledBlue, !LED_on);
  digitalWrite(ledRed, !LED_on);
  digitalWrite(ledGreen, !LED_on);
  delay(10);
}

unsigned long blinkTime = millis();
bool changeState = false;

void LED_blink(int intLed, int blinkTime_on, int blinkTime_intervall)
{
  unsigned long delta = millis() - blinkTime;
  if (delta > blinkTime_intervall)
  {
    changeState = true;
    blinkTime = millis();
    digitalWrite(intLed, LED_on);
  }
  else if (delta > blinkTime_on && changeState == true)
  {
    digitalWrite(intLed, !LED_on);
    changeState = false;
  }
}

void debug(String strMessage)
{
#ifdef DEBUG
  if (strMessage != "")
  {
    debugPort.println(strMessage);
    delay(10);
  }
#endif
}

void restoreBC127()
{
  String value = "";
  debug("reset and restore...");
  resetBC127();
  long Baudrates[] = {9600, 921600}; //{9600, 115200, 460800, 921600};
  for (int i = 0; i < 2; i++)
  {
    if (serialPort)
      serialPort.end();
    serialPort.begin(Baudrates[i]);
    pinPeripheral(10, PIO_SERCOM);
    pinPeripheral(11, PIO_SERCOM);
    while (!serialPort) {}
    BTModu.exitDataMode();
    BTModu.restore();
  }
  if (serialPort)
    serialPort.end();
  serialIn.addChar(' ');
}

void resetBC127()
{
  digitalWrite(resetPin, LOW);
  delay(100);
  digitalWrite(resetPin, HIGH);
  delay(1000);
}

void resetSystem()
{
  debug("!!! COMPLETE SYSTEM RESET !!!");
  resetBC127();
  NVIC_SystemReset();
}

void resetTimer()
{
  resetTime = millis();
  LedsOff();
}
