/******************************************************************************
**************************Hardware interface layer*****************************
* | file          :   DEV_Config.
* | version     :   V1.0
* | date        :   2017-12-11
* | function    :
    Provide the hardware underlying interface
******************************************************************************/
#include "DEV_Config.h"
#include <SPI.h>

/********************************************************************************
  function:    System Init and exit
  note:
    Initialize the communication method
********************************************************************************/
uint8_t System_Init(void)
{
  //set pin
  pinMode(LCD_CS, OUTPUT);
  pinMode(LCD_RST, OUTPUT);
  pinMode(LCD_DC, OUTPUT);
  pinMode(TP_CS, OUTPUT);
  pinMode(TP_IRQ, INPUT);
  digitalWrite(TP_IRQ, HIGH);
  pinMode(LCD_BL, OUTPUT);

  //set Serial
  Serial.begin(921600);
  
  SPI.beginTransaction(SPISettings(F_CPU/2, MSBFIRST, SPI_MODE0));
  SPI.begin();

  return 0;
}

void PWM_SetValue(uint16_t value)
{
  analogWrite(LCD_BL, value);
}

/********************************************************************************
  function:    Delay function
  note:
    Driver_Delay_ms(xms) : Delay x ms
    Driver_Delay_us(xus) : Delay x us
********************************************************************************/
void Driver_Delay_ms(unsigned long xms)
{
  delay(xms);
}

void Driver_Delay_us(int xus)
{
  delayMicroseconds(xus);
}
