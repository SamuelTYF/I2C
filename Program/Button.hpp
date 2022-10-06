//按钮
class Button
{
public:
    //GUI
    int X;
    int Y;
    int Width;
    int Height;
    const char* Name;
    //回调
    void (*Func)();
    //文字偏移量
    int NameOffset_X;
    int NameOffset_Y;
    //前景色
    COLOR FrontColor;
    Button(int x,int y,int width,int height,const char* name,void (*func)(),int no_x,int no_y)
    {
        X=x;
        Y=y;
        Width=width;
        Height=height;
        Name=name;
        Func=func;
        NameOffset_X=no_x;
        NameOffset_Y=no_y;
        FrontColor=WHITE;
    }
    //命中测试
    void ClickTest(int x,int y)
    {
        if(X<=x&&Y<=y&&x<=(X+Width)&&y<=(Y+Height))
        {
            Serial.println(Name);
            Func();
        }
    }
};

