/*****************************************************************************
* | File      	:	LCD_Touch.h
* | Author      :   Waveshare team
* | Function    :	LCD Touch Pad Driver and Draw
* | Info        :
*   Image scanning
*      Please use progressive scanning to generate images or fonts
*----------------
* |	This version:   V1.0
* | Date        :   2017-08-16
* | Info        :   Basic version
*
******************************************************************************/
#ifndef __LCD_TOUCH_H_
#define __LCD_TOUCH_H_

#include "DEV_Config.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"

#define TP_PRESS_DOWN           0x80
#define TP_PRESSED              0x40
	
//Touch screen structure
typedef struct {
	POINT Xpoint0;
	POINT Ypoint0;
	POINT Xpoint;
	POINT Ypoint;
	unsigned char chStatus;
	unsigned char chType;
	int iXoff;
	int iYoff;
	float fXfac;
	float fYfac;
	//Select the coordinates of the XPT2046 touch
	//  screen relative to what scan direction
	LCD_SCAN_DIR TP_Scan_Dir;
}TP_DEV;

//Brush structure
typedef struct{
	POINT Xpoint;
	POINT Ypoint;
	COLOR Color;
	DOT_PIXEL DotPixel; 
}TP_DRAW;

//事件类型
typedef enum{
	None=0,
	Click=1,
	Move_U=2,
	Move_D=4,
	Move_L=8,
	Move_R=16
}TP_EVENT_Type;

//事件
class TP_EVENT
{
public:
  //起始
	uint32_t Start;
	POINT XPoint0;
	POINT YPoint0;
  //终止
	uint32_t End;
	POINT XPoint;
	POINT YPoint;
  //扫描周期数
	int ScanCount;
	TP_EVENT_Type Type;
	TP_EVENT(POINT x,POINT y)
	{
		Start=End=millis();
		XPoint0=XPoint=x;
		YPoint0=YPoint=y;
		ScanCount=1;
	}
	void Set(POINT x,POINT y)
	{
		End=millis();
		XPoint=x;
		YPoint=y;
		ScanCount++;
	}
  	//更新事件类型
	void Lock()
	{
		if(ScanCount<10)Type=Click;
		else if(abs(XPoint0-XPoint)+abs(YPoint0-YPoint)<50)Type=Click;
		else
		{
			int dx=
				XPoint>XPoint0+50?1:
					XPoint<XPoint0-50?-1:
						0;
			int dy=
				YPoint>YPoint0+50?1:
					YPoint<YPoint0-50?-1:
						0;
			// Serial.print(dx);
			// Serial.print(",");
			// Serial.println(dy);
			if(dx==1&&dy==0)Type=Move_R;
			else if(dx==-1&&dy==0)Type=Move_L;
			else if(dx==0&&dy==1)Type=Move_D;
			else if(dx==0&&dy==-1)Type=Move_U;
			else Type=None;
		}
	}
};


void TP_GetAdFac(void);
void TP_Adjust(void);
void TP_Dialog(void);
void TP_DrawBoard(void);
void TP_Init( LCD_SCAN_DIR Lcd_ScanDir );
TP_EVENT *Scan();
#endif
