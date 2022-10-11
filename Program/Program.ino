#include "DEV_Config.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include "LCD_Touch.h"
#include <Wire.h>
#include "Data.hpp"
#include "Button.hpp"

//数据帧
DataFrame_I2C *dataFrame;
//采样信息
SampleInfo *sampleInfo;

//转义字符
char transcode[]={'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
//sprintf临时字符串
char *s=new char[128];
//dtostrf临时字符串（Arduino中的sprintf不支持double）
char *ts=new char[16];

//GUI偏移量
#define Offset_L 50
#define Offset_U 30

//绘制间隔
#define WidthCount 6
int Widths[]{1,2,4,5,10,20,40};
int WIndex=3;
int Width=5;
int Time=80;
//是否绘制纵向辅助线
bool Column=false;

//按钮
Button *Button_Redraw;
Button *Button_Adjust;
Button *Button_I2C;
Button *Button_Search;
Button *Button_Add;
Button *Button_Reduce;
Button *Button_Line;

char* Names[3]{"SCL","SDA","Bit"};

//采样数据绘制起始地址
int Offset = 0;

//解析结果绘制起始地址
int DOffset=0;

//设置Timer4
void SetTimer4(int x)
{
  //16,000,000/x hz
  noInterrupts();
  TCCR4A = 0;
  TCCR4B = 0;
  TCNT4 = 0;
  OCR4A = x;
  TCCR4B |= (1 << WGM42);
  // 设置1预分频
  TCCR4B |= (1 << CS10);
  TIMSK4 |= (1 << OCIE4A);
  interrupts();
}

//设置Timer1
void SetTimer()
{
  //1hz
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 15624;
  TCCR1B |= (1 << WGM12);
  // 设置1024预分频
  TCCR1B |= (1 << CS12) | (1 << CS10);
  TIMSK1 |= (1 << OCIE1A);
  interrupts();
}

int Mode;

// 绘制采样标识
int _COLOR=0;

void ClearTip()
{
  GUI_DrawRectangle(300,300,400,320,BLACK,DRAW_FULL,DOT_PIXEL_1X1);
}
void SetTip(int color)
{
  GUI_DisString_EN(300,300, "Sampling", &Font16, BLACK, color);
}

//1hz定时器，若在采样中，定时闪烁采样标识
ISR(TIMER1_COMPA_vect)
{
  if(Mode!=0)
  {
    if(_COLOR==1)
    {
      _COLOR=0;
      SetTip(BLACK);
    }
    else
    {
      _COLOR=1;
      SetTip(RED);
    }
  }
}

//10khz定时器，对信号进行采样
int last;

ISR(TIMER4_COMPA_vect)
{
  //针对SCL,SDA针脚进行采样
  int x=*portInputRegister(4)&3;
  if(Mode==1)
  {
    last=x;
    Mode=2;
  }
  else if(Mode==2)
  {
    //若信号发生改变，则进行采样
    if(x!=last)
    {
      Mode=3;
      Serial.println(last);
      Serial.println(x);
      dataFrame->Peek(0);
      dataFrame->Set(last);
      dataFrame->Set(x);
    }
  }
  else if(Mode==3)
  {
    dataFrame->Set(x);
    //采样完成后，进行后续处理
    if(dataFrame->P>=dataFrame->End){
      Mode=0;
      Offset=0;
      dataFrame->Peek(0);
      while(dataFrame->P<dataFrame->End)
        Serial.println((int)dataFrame->Get());
      SetTimer4(1599);
      Button_I2C->FrontColor=RED;
      DrawButton(Button_I2C);
      Parse();
      ClearTip();
      DrawChannels();
    }
  }
}

//同步后的比特数据
bool Bits[512];
//比特对应的下降沿位置
int Indexs[512];
int BIndex=0;

//状态机
int mode=0;
void SetMode(int x,int next)
{
  //串口调试
  Serial.print(mode);
  Serial.print(",");
  Serial.print(x&1);
  Serial.print(",");
  Serial.print(x&2);
  Serial.print(",");
  Serial.println(next);
  mode=next;
}

//使用状态机进行同步
void Parse()
{
  BIndex=0;
  mode=1;
  dataFrame->Peek(Offset);
  while(dataFrame->P<dataFrame->End)
  {
    int x=dataFrame->Get();
    if(mode==1)
    {
      if(x==1)SetMode(x,2);
    }
    else if(mode==2)
    {
      if(x==0)
      {
        SetMode(x,3);
        Bits[BIndex]=0;
        Indexs[BIndex++]=dataFrame->P-dataFrame->Data;
        Serial.println(0);
      }
      else if(x==2)
      {
        SetMode(x,5);
        Bits[BIndex]=0;
        Indexs[BIndex++]=dataFrame->P-dataFrame->Data;
        Serial.println(0);
      }
      else if(x==3)
      {
        SetMode(x,1);
        Serial.println("STOP");
        ParseBit();
        return;
      }
    }
    else if(mode==3)
    {
      if(x==1)SetMode(x,2);
      else if(x==2)SetMode(x,5);
    }
    else if(mode==4)
    {
      if(x==2)
      {
        SetMode(x,5);
        Bits[BIndex]=1;
        Indexs[BIndex++]=dataFrame->P-dataFrame->Data;
        Serial.println(1);
      }
      else if(x==0)
      {
        SetMode(x,3);
        Bits[BIndex]=1;
        Indexs[BIndex++]=dataFrame->P-dataFrame->Data;
        Serial.println(1);
      }
    }
    else if(mode=5)
    {
      if(x==3)SetMode(x,4);
      else if(x==0)SetMode(x,3);
    }
  }
}

//解析结果
bool Error;
Package *Result;

unsigned char Read(int offset)
{
  unsigned char r=0;
  bool *p=Bits+offset;
  for(int i=7;i>=0;i--,p++)
    if(*p)
      r|=(1<<i);
  return r;
}

//对比特流进行解析
void ParseBit()
{
  //比特信息长度必须为9n+10
  if(BIndex%9!=1)
  {
    Serial.println("Error");
    Error=true;
    return;
  }
  Error=false;
  char address=Read(0);
  Serial.print("Address=");
  Serial.print("0x");
  Serial.print(transcode[address>>4]);
  Serial.println(transcode[address&15]);
  Serial.print("R/W=");
  bool rw=Bits[8];
  Serial.println(rw?"R":"W");
  bool ack=Bits[9];
  Serial.print("ACK=");
  Serial.println(ack?"NACK":"ACK");
  Serial.print("Count=");
  int count=(BIndex-10)/9;
  Serial.println(count);
  Result=new Package(address,rw,ack,count,Indexs[0],Indexs[9]);
  for(int i=0;i<count;i++)
  {
    Data *data=Result->Values+i;
    data->Value=Read(i*9+10);
    Serial.print("Data[");
    Serial.print(i);
    Serial.print("]=0x");
    Serial.print(transcode[data->Value>>4]);
    Serial.println(transcode[data->Value&15]);
    Serial.println((int)data->Value);
    data->ACK=Bits[i*9+10+8];
    Serial.print("ACK=");
    Serial.println(data->ACK?"NACK":"ACK");
    data->Start=Indexs[i*9+10];
    data->End=Indexs[i*9+10+8];
  }
  PrintResult();
}

//初始化
void setup()
{

  //开启I2C Slaver
  Wire.begin(0x10);
  Wire.onReceive(receive);
  Serial.begin(921600);

  //LCD初始化
  System_Init();

  SetTimer();
  SetTimer4(1599);
  sampleInfo = GetSampleInfo(sizeof(char), 4096);
  dataFrame = new DataFrame_I2C(sampleInfo->Length);
  dataFrame->Random();

  //触摸屏初始化
  LCD_SCAN_DIR Lcd_ScanDir = SCAN_DIR_DFT;
  LCD_Init(Lcd_ScanDir, 100);
  TP_Init(Lcd_ScanDir);
  TP_GetAdFac();

  Button_Redraw = new Button(10, 5, 80, 20, "Redraw", Redraw, 8, 4);
  Button_Adjust = new Button(380, 5, 80, 20, "Adjust", Adjust, 8, 4);
  Button_I2C = new Button(160, 5, 40, 20, "I2C", I2C, 3, 4);
  Button_Search = new Button(300, 5, 80, 20, "Search", Search, 8, 4);
  Button_Add = new Button(180, 300, 20, 20, "+", AddCount, 5, 4);
  Button_Reduce = new Button(20, 300, 20, 20, "-", ReduceCount, 5, 4);
  Button_Line = new Button(200, 300, 60, 20, "Line", ChangeLine, 8, 4);

  // GUI_DrawLine(0, 10, LCD_WIDTH, 10, RED, LINE_SOLID, DOT_PIXEL_2X2);

  // GUI_DrawRectangle(0, Offset_L, LCD_WIDTH, Offset_L+1, RED, DRAW_FULL, DOT_PIXEL_2X2);

  LCD_Clear(BLACK);

  //绘制GUI
  DrawChannels(); 
  PrintInfo();
  DrawButtons();
  DrawCount();

  //TP_Adjust();
}

//I2C Slaver
void receive(int len)
{
  Wire.read();
}

//Main Loop 处理触摸事件
void loop()
{
  if(Mode!=0)
  {
    delay(1);
    return;
  }
  //扫描按键事件
  TP_EVENT *event = Scan();
  if (event != nullptr)
  {
    event->Lock();
    // GUI_DrawRectangle(0,Offset_L0,300,300,RED,DRAW_FULL,DOT_PIXEL_1X1);
    // GUI_DisNum(0,Offset_L0,x,&Font16,RED,BLACK);
    // GUI_DisNum(0,Offset_U+50,y,&Font16,RED,BLACK);
    Serial.print(event->Start);
    Serial.print(",");
    Serial.print(event->XPoint0);
    Serial.print(",");
    Serial.print(event->YPoint0);
    Serial.print(",");
    Serial.print(event->End);
    Serial.print(",");
    Serial.print(event->XPoint);
    Serial.print(",");
    Serial.print(event->YPoint);
    Serial.print(",");
    Serial.print(event->ScanCount);
    Serial.print(",");
    if (event->Type == None)
      Serial.println("NONE");
    else if (event->Type == Click)
      Serial.println("Click");
    else if (event->Type == Move_L)
      Serial.println("Move_L");
    else if (event->Type == Move_R)
      Serial.println("Move_R");
    else if (event->Type == Move_D)
      Serial.println("Move_D");
    else if (event->Type == Move_U)
      Serial.println("Move_U");
    if (event->Type == Click)
    {
      Button_Redraw->ClickTest(event->XPoint, event->YPoint);
      Button_Adjust->ClickTest(event->XPoint, event->YPoint);
      Button_I2C->ClickTest(event->XPoint, event->YPoint);
      Button_Search->ClickTest(event->XPoint, event->YPoint);
      Button_Add->ClickTest(event->XPoint, event->YPoint);
      Button_Reduce->ClickTest(event->XPoint, event->YPoint);
      Button_Line->ClickTest(event->XPoint, event->YPoint);
    }
    else if (event->Type == Move_R)
    {
      //向右滑动，Offset增加Time
      Offset = min(Offset+Time,sampleInfo->Length-Time);
      DrawChannels();
    }
    else if (event->Type == Move_L)
    {
      //想做滑动Offset减少Time
      Offset = max(0, Offset - Time);
      DrawChannels();
    }
    else if(event->Type == Move_U)
    {
      //向上滑动，且解析成功后
      if(Result!=nullptr)
      {
        //解析数据向上滑动4
        DOffset=max(0,DOffset-4);
        PrintDatas();
      }
    }
    else if(event->Type == Move_D)
    {
      //向下滑动，且解析成功后
      if(Result!=nullptr)
      {
        //解析数据向下滑动4
        DOffset=min(max(0,Result->Count-4),DOffset+4);
        PrintDatas();
      }
    }
  }
  delay(10);
}

//绘制坐标系
void PrintChannelLines(int count)
{

  int h = count * 30 + 40;

  int r = Time * Width + 10;

  GUI_DrawRectangle(Offset_L,Offset_U,r+Offset_L,h+Offset_U,LCD_BACKGROUND,DRAW_FULL,DOT_PIXEL_1X1);

  if(Column)
    for (int i = 0; i < Time; i++)
      GUI_DrawLine(i * Width + Offset_L + Width, Offset_U, i * Width + Offset_L + Width, h + Offset_U, GRAY, LINE_DOTTED, DOT_PIXEL_1X1);

  for (int i = 0; i < count; i++)
  {
    GUI_DrawLine(Offset_L, 30 * i + Offset_U + 16, Offset_L + r + 5, 30 * i + Offset_U + 16, GREEN, LINE_DOTTED, DOT_PIXEL_1X1);
    GUI_DrawLine(Offset_L, 30 * i + Offset_U + 34, Offset_L + r + 5, 30 * i + Offset_U + 34, RED, LINE_DOTTED, DOT_PIXEL_1X1);
    //GUI_DisNum(9, 30 * i + Offset_U + 20, i, &Font16, LCD_BACKGROUND, BLUE);
    GUI_DisString_EN(9,30*i+Offset_U+20,Names[i],&Font16,LCD_BACKGROUND, BLUE);
  }

  GUI_DisString_EN(9,60+Offset_U+20,Names[2],&Font16,LCD_BACKGROUND, BLUE);

  GUI_DrawRectangle(Offset_L - 1, Offset_U, Offset_L + 1, h + Offset_U, WHITE, DRAW_FULL, DOT_PIXEL_2X2);

  GUI_DrawLine(Offset_L - 4, Offset_U + 5, Offset_L, Offset_U, WHITE, LINE_SOLID, DOT_PIXEL_1X1);

  GUI_DrawLine(Offset_L + 4, Offset_U + 5, Offset_L, Offset_U, WHITE, LINE_SOLID, DOT_PIXEL_1X1);

  GUI_DrawRectangle(Offset_L, h + Offset_U - 1, Offset_L + r + 5, h + Offset_U + 1, WHITE, DRAW_FULL, DOT_PIXEL_2X2);

  GUI_DrawLine(Offset_L + r, h + Offset_U + 5, Offset_L + r + 5, h + Offset_U, WHITE, LINE_SOLID, DOT_PIXEL_1X1);

  GUI_DrawLine(Offset_L + r, h + Offset_U - 5, Offset_L + r + 5, h + Offset_U, WHITE, LINE_SOLID, DOT_PIXEL_1X1);

  DrawOffset();
}

//打印偏移量
void DrawOffset()
{
  GUI_DrawRectangle(Offset_L, 70 + 60 + 24, 400, 70 + 60 + 36, BLACK, DRAW_FULL, DOT_PIXEL_1X1);

  sprintf(s, "Offset : %d", Offset);

  GUI_DisString_EN(Offset_L, 70 + 60 + 20 + 4, s, &Font16, BLACK, WHITE);
}

//打印采样信息
void PrintInfo()
{
  int infoh = 70 + 60;

  GUI_DrawRectangle(Offset_L-20, infoh, 450, infoh + 40, WHITE, DRAW_EMPTY, DOT_PIXEL_1X1);

  sprintf(s, "Length : %ld", sampleInfo->Length);

  GUI_DisString_EN(Offset_L, infoh + 4, s, &Font16, BLACK, WHITE);

  dtostrf(sampleInfo->Time, 6, 4, ts);

  sprintf(s, "Time : %s s", ts);

  GUI_DisString_EN(LCD_WIDTH/2, infoh + 4, s, &Font16, BLACK, WHITE);
}

//打印数据单元
void PrintDatas()
{
  GUI_DrawRectangle(Offset_L, 70 + 140, 400, 70 + 140 + 76, BLACK, DRAW_FULL, DOT_PIXEL_1X1);
  for(int i=0;i<4;i++)
  {
    int offset=DOffset+i;
    if(0<=offset&&offset<Result->Count)
    {
      Serial.print("PrintData:");
      Serial.print(offset);
      Serial.print(",");
      Serial.println(Result->Values[offset].Value);
      int h=70+140+i*20+4;
      sprintf(s, " Data[%d] : 0x%c%c", offset, transcode[Result->Values[offset].Value>>4],transcode[Result->Values[offset].Value&15]);
      GUI_DisString_EN(Offset_L, h, s, &Font16, BLACK, WHITE);
    }
  }
}

//打印数据包
void PrintResult()
{
  int infoh = 70 + 100;
  
  GUI_DrawRectangle(Offset_L-20, infoh, 450, infoh + 120, BLACK, DRAW_FULL, DOT_PIXEL_1X1);

  GUI_DrawRectangle(Offset_L-20, infoh, 450, infoh + 120, WHITE, DRAW_EMPTY, DOT_PIXEL_1X1);

  sprintf(s, "  Offset : %d", Offset);

  GUI_DisString_EN(Offset_L, infoh + 4, s, &Font16, BLACK, WHITE);

  sprintf(s, " Address : 0x%c%c", transcode[Result->Address>>4],transcode[Result->Address&15]);

  GUI_DisString_EN(Offset_L, infoh + 24, s, &Font16, BLACK, WHITE);

  sprintf(s, "Count : %d", Result->Count);

  GUI_DisString_EN(LCD_WIDTH/2, infoh + 4, s, &Font16, BLACK, WHITE);

  sprintf(s, "  R/W : %s", Result->RW?"Read":"Write");

  GUI_DisString_EN(LCD_WIDTH/2, infoh + 24, s, &Font16, BLACK, WHITE);

  DOffset=0;

  PrintDatas();
}

//绘制按钮
void DrawButtons()
{
  DrawButton(Button_Redraw);
  DrawButton(Button_Adjust);
  DrawButton(Button_I2C);
  DrawButton(Button_Search);
  DrawButton(Button_Add);
  DrawButton(Button_Reduce);
  DrawButton(Button_Line);
}

//绘制数据帧
void PrintDataFrame(int channel, int offset, int mask, int length)
{
  if (offset > dataFrame->Length)
    offset = dataFrame->Length;
  if (offset + length > dataFrame->Length)
    length = dataFrame->Length - offset;
  if (length == 0)
    return;
  int h = 30 * channel + Offset_U + 16;
  int l = 30 * channel + Offset_U + 34;
  int b = (dataFrame->Data[offset] & mask) ? h : l;
  dataFrame->Peek(offset);
  for (int i = 0; i < length; i++)
  {
    int t = (dataFrame->Get() & mask) ? h : l;
    GUI_DrawRectangle(i * Width + Offset_L, t - 1, i * Width + Offset_L + Width, t + 1, YELLOW, DRAW_FULL, DOT_PIXEL_1X1);
    if (b != t)
    {
      if (b < t)
        GUI_DrawRectangle(i * Width + Offset_L, b, i * Width + Offset_L + 1, t, YELLOW, DRAW_FULL, DOT_PIXEL_1X1);
      else
        GUI_DrawRectangle(i * Width + Offset_L, t, i * Width + Offset_L + 1, b, YELLOW, DRAW_FULL, DOT_PIXEL_1X1);
      b = t;
    }
  }
}

//绘制按钮
void DrawButton(Button *button)
{
  GUI_DrawRectangle(button->X, button->Y, button->X + button->Width, button->Y + button->Height, LCD_BACKGROUND, DRAW_FULL, DOT_PIXEL_1X1);
  GUI_DrawRectangle(button->X, button->Y, button->X + button->Width, button->Y + button->Height, button->FrontColor, DRAW_EMPTY, DOT_PIXEL_2X2);
  GUI_DisString_EN(button->X + button->NameOffset_X, button->Y + button->NameOffset_Y, button->Name, &Font16, LCD_BACKGROUND, button->FrontColor);
}

//打印地址
void PrintAddress()
{
  if(Result->End<Offset)return;
  if(Result->Start>Offset+Time)return;
  int start=max(Offset,Result->Start)-Offset;
  int end=min(Offset+Time,Result->End)-Offset;

  //进行嗅探操作，找到大小不超过绘制长度的合法显示
  sprintf(s,"Address : 0x%c%c",transcode[Result->Address>>4],transcode[Result->Address&15]);

  if((end-start)*Width>strlen(s)*12)
  {
    int o=((end-start)*Width-strlen(s)*12)/2;
    GUI_DisString_EN(Offset_L+start*Width+o, 30 * 2 + Offset_U + 18, s, &Font16, LCD_BACKGROUND, WHITE);
  }
  else
  {
    sprintf(s,"0x%c%c",transcode[Result->Address>>4],transcode[Result->Address&15]);
    if((end-start)*Width>strlen(s)*12)
    {
      int o=((end-start)*Width-strlen(s)*12)/2;
      GUI_DisString_EN(Offset_L+start*Width+o, 30 * 2 + Offset_U + 18, s, &Font16, LCD_BACKGROUND, WHITE);
    }
    else
    {
      sprintf(s,"%c%c",transcode[Result->Address>>4],transcode[Result->Address&15]);
      if((end-start)*Width>strlen(s)*12)
      {
        int o=((end-start)*Width-strlen(s)*12)/2;
        GUI_DisString_EN(Offset_L+start*Width+o, 30 * 2 + Offset_U + 18, s, &Font16, LCD_BACKGROUND, WHITE);
      }
    }
  }
  
  GUI_DrawRectangle(Offset_L+start*Width, 30 * 2 + Offset_U + 16, Offset_L + end * Width, 30 * 2 + Offset_U + 34, RED, DRAW_EMPTY, DOT_PIXEL_1X1);
}

//打印数据单元
void PrintDataRange(Data* data)
{
  if(data->End<Offset)return;
  if(data->Start>Offset+Time)return;
  int start=max(Offset,data->Start)-Offset;
  int end=min(Offset+Time,data->End)-Offset;

  sprintf(s,"Data[%d] : 0x%c%c",data-Result->Values,transcode[data->Value>>4],transcode[data->Value&15]);

  if((end-start)*Width>strlen(s)*12)
  {
    int o=((end-start)*Width-strlen(s)*12)/2;
    GUI_DisString_EN(Offset_L+start*Width+o, 30 * 2 + Offset_U + 18, s, &Font16, LCD_BACKGROUND, WHITE);
  }
  else
  {
    sprintf(s,"0x%c%c",transcode[data->Value>>4],transcode[data->Value&15]);
    if((end-start)*Width>strlen(s)*12)
    {
      int o=((end-start)*Width-strlen(s)*12)/2;
      GUI_DisString_EN(Offset_L+start*Width+o, 30 * 2 + Offset_U + 18, s, &Font16, LCD_BACKGROUND, WHITE);
    }
    else
    {
      sprintf(s,"%c%c",transcode[data->Value>>4],transcode[data->Value&15]);
      if((end-start)*Width>strlen(s)*12)
      {
        int o=((end-start)*Width-strlen(s)*12)/2;
        GUI_DisString_EN(Offset_L+start*Width+o, 30 * 2 + Offset_U + 18, s, &Font16, LCD_BACKGROUND, WHITE);
      }
    }
  }

  GUI_DrawRectangle(Offset_L+start*Width, 30 * 2 + Offset_U + 16, Offset_L + end * Width, 30 * 2 + Offset_U + 34, WHITE, DRAW_EMPTY, DOT_PIXEL_1X1);
}

//打印所有数据
void PrintDataRanges()
{
  int r = Time * Width + 10;
  //GUI_DrawRectangle(Offset_L, 30 * 2 + Offset_U + 4, Offset_L + r + 5, 30 * 2 + Offset_U + 16, BLACK, DRAW_FULL, DOT_PIXEL_1X1);
  if(Result!=nullptr)
  {
    PrintAddress();
    for(int i=0;i<Result->Count;i++)
      PrintDataRange(Result->Values+i);
  }
}

//绘制数据
void DrawChannels()
{
  PrintChannelLines(2);

  PrintDataFrame(0, Offset, _BV(0), Time);
  PrintDataFrame(1, Offset, _BV(1), Time);

  PrintDataRanges();
}

//刷新GUI
void Redraw()
{
  DrawChannels(); 
  PrintInfo();
  DrawButtons();
  DrawCount();
}

//触摸屏调整
void Adjust()
{
  TP_Adjust();
  Redraw();
}

//I2C采样
void I2C()
{
  Result=nullptr;
  Offset=0;
  dataFrame->Peek(0);
  SetTimer4(1599);
  Mode=1;
}

//对I2C进行搜索，在采样多条数据时，可依次搜索并转跳到多条数据
void Search()
{
  Offset=min(sampleInfo->Length,Offset+1);
  dataFrame->Peek(Offset);
  int last=dataFrame->Get();
  bool success=false;
  while(dataFrame->P<dataFrame->End)
  {
    int x=dataFrame->Get();
    //last = 3 SCL=1 SDA=1
    //   x = 1 SCL=1 SDA=0
    if(last==3&&x==1)
    {
      Offset=dataFrame->P-dataFrame->Data-1;
      success=true;
      break;
    }
    last=x;
  }
  if(success)
  {
    Serial.print("Search Success:");
    Serial.println(Offset);
  }
  else
  {
    Serial.println("Search Error");
    Offset=0;
  }
  Parse();
  DrawChannels();
}

//绘制长度
void DrawCount()
{
  GUI_DrawRectangle(50,300,170,320,BLACK,DRAW_FULL,DOT_PIXEL_1X1);

  sprintf(s, "Count : %d", Time);

  GUI_DisString_EN(50, 304, s, &Font16, BLACK, WHITE);
}

//增加长度
void AddCount()
{
  int i=max(0,WIndex-1);
  if(WIndex==i)return;
  WIndex=i;
  Width=Widths[WIndex];
  Time=400/Width;
  DrawCount();
  DrawChannels();
}

//减少长度
void ReduceCount()
{  
  int i=min(WidthCount-1,WIndex+1);
  if(WIndex==i)return;
  WIndex=i;
  Width=Widths[WIndex];
  Time=400/Width;
  DrawCount();
  DrawChannels();
}

//设置Column
void ChangeLine()
{
  if(Column)
  {
    Column=false;
    Button_Line->FrontColor=WHITE;
  }
  else
  {
    Column=true;
    Button_Line->FrontColor=RED;
  }
  DrawChannels();
  DrawButton(Button_Line);
}
