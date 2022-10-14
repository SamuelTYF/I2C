#ifndef DATA_HPP
#define DATA_HPP

//数据帧
class DataFrame_I2C
{
public:
    //使用Char存储数据
    char *Data;
    //数据长度
    int Length;
    //当前指针
    char *P;
    //结束指针
    char *End;
    DataFrame_I2C(int length)
    {
        Data=new char[length];
        Length=length;
        P=Data;
        End=Data+Length;
    }
    //测试代码
    void Random()
    {
        for(int i=0;i<Length;i++)
            Set(i);
    }
    //设置当前数据，并移动
    void Set(char x)
    {
        *(P++)=x;
    }
    //获取当前数据，并移动
    char Get()
    {
        return *(P++);
    }
    //将指针移动到offset位置
    void Peek(int offset)
    {
        P=Data+offset;
    }
};

//采样信息
class SampleInfo
{
public:
    //数据结构大小
    size_t Size;
    //采样长度
    unsigned long Length;
    //内存大小
    unsigned long Total;
    //采样间隔s
    double Duration;
    //采样总时间s
    double Time;
    SampleInfo(size_t size,unsigned long  length,double duration)
    {
        Size=size;
        Length=length;
        Total=size*length;
        Duration=duration;
        Time=length*duration;
    }
};

//根据Arduino内存以及数据结构大小，构造采样信息
SampleInfo *GetSampleInfo(size_t size,unsigned long total)
{
    unsigned long length=total/size;
    while(length!=(length&(-length)))length-=(length&(-length));
    double duration=(OCR4A+1)/16000000.0;
    return new SampleInfo(size,length,duration);
}

//数据单元
class Data
{
public:
  //数据
  unsigned char Value;
  //应答位
  bool ACK;
  //起始位置（下降沿）
  int Start;
  //终止位置（下降沿）
  int End;
};

//数据包
class Package
{
public:
  //传输地址
  char Address;
  //读写
  bool RW;
  //应答
  bool ACK;
  //数据单元个数
  int Count;
  //数据单元
  Data *Values;
  //地址起始位置（下降沿）
  int Start;
  //地址起始位置（下降沿）
  int End;
  Package(char address,bool rw,bool ack,int count,int start,int end)
  {
    Address=address;
    RW=rw;
    ACK=ack;
    Count=count;
    Values=new Data[count];
    Start=start;
    End=end;
  }
};

#endif
