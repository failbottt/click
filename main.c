#include <windows.h>
#include <windowsx.h>
#include <timeapi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <hidusage.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

int bitmap_width = 1024;
int bitmap_height = 1024;
unsigned char ttf_buffer[1<<20];
unsigned char temp_bitmap[1024*1024];
stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs

#define SCREEN_WIDTH 500
#define SCREEN_HEIGHT 300

#define KB(x) ((x) << 11)
#define MB(x) ((x) << 20)
#define GB(x) ((x) << 30)
#define TB(x) ((u64)(x) << 40llu)
#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

#define Thousand(x) ((x)*1000)
#define Million(x)  ((x)*1000000llu)
#define Billion(x)  ((x)*1000000000llu)
#define Trillion(x) ((x)*1000000000000llu)

#define Min(a,b) (((a)<(b))?(a):(b))
#define Max(a,b) (((a)>(b))?(a):(b))
#define Clamp(a,x,b) (((x)<(a))?(a):\
((b)<(x))?(b):(x))
#define ClampTop(a,b) Min(a,b)
#define ClampBot(a,b) Max(a,b)

#define AlignUpPow2(x,p) (((x) + (p) - 1)&~((p) - 1))
#define AlignDownPow2(x,p) ((x)&~((p) - 1))
#define IsPow2OrZero(x) (((x)&((x)-1)) == 0)

#if BUILD_DEBUG
    #define Assert(e) if(!(e)) {*(int*)0 = 0;}
#else
    #define Assert(e)
#endif

#define internal static
#define local_persist static
#define global_variable static

typedef int8_t      s8;
typedef int16_t     s16;
typedef int32_t     s32;
typedef int64_t     s64;
typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;
typedef float       f32;
typedef double      f64;

global_variable s8  min_s8  = (s8) 0x80;
global_variable s16 min_s16 = (s16)0x8000;
global_variable s32 min_s32 = (s32)0x80000000;
global_variable s64 min_s64 = (s64)0x8000000000000000llu;

global_variable s8  max_s8  = (s8) 0x7f;
global_variable s16 max_s16 = (s16)0x7fff;
global_variable s32 max_s32 = (s32)0x7fffffff;
global_variable s64 max_s64 = (s64)0x7fffffffffffffffllu;

global_variable u8  max_u8  = 0xff;
global_variable u16 max_u16 = 0xffff;
global_variable u32 max_u32 = 0xffffffff;
global_variable u64 max_u64 = 0xffffffffffffffffllu;

typedef struct String {
    u64 length;
    u8  *str;
} String;

#define STRING(s) (String){strlen(s), s}

#define MOUSE_SENSITIVITY 2.0f

#define GAME_TIMER 10.f
typedef struct Game
{
    u64 mouse_x;
    u64 mouse_y;
    u8 clicked;
    f32 timer;
    u64 frame;
    u32 box_pos_x;
    u32 box_pos_y;
} Game;
Game game = {.mouse_x = 0, .mouse_y = 0, .clicked = 0, .timer = 30.f, .frame = 0};

typedef struct GameOffscreenBuffer
{
    // Pixels are always 32-bits wide, Memory Order BB GG RR XX
    void *Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
} GameOffscreenBuffer;

typedef struct GameMemory
{
    u8 IsInitialized;
    u64 PermenantStorageSize;
    void* PermenantStorage; // NOTE(spangler): REQUIRED to be cleared to zero at startup
                            //
    u64 ScratchStorageSize;
    void* ScratchStorage; // NOTE(spangler): REQUIRED to be cleared to zero at startup
} GameMemory;

typedef struct File {
    u64     Length;
    void    *Data;
} File;

global_variable u64 GlobalPerfCountFrequency;
global_variable int GlobalRunning;

typedef struct win32_window_dimension
{
    int Width;
    int Height;
} win32_window_dimension;

typedef struct game_button_state
{
    u64 HalfTransitionCount;
    u64 EndedDown;
} game_button_state;

typedef struct game_controller_input {
    union
    {
        game_button_state Buttons[5];
        struct {
            game_button_state Up;
            game_button_state Down;
            game_button_state Left;
            game_button_state Right;
            game_button_state Space;
        };
    };
} game_controller_input;

typedef struct GameInput
{
    game_controller_input Controller;
    f32 dt_for_frame;
} GameInput;

internal void
debug_platform_free_file_memory(void *Memory, u64 MemorySize)
{
    // win32 free file memory
    if(Memory && VirtualFree(Memory, MemorySize, MEM_DECOMMIT|MEM_RELEASE))
    {
        // freed
    }
    else
    {
        // unable to free
    }
}

internal void
Win32ProcessKeyboardMessage(game_button_state *NewState, u64 IsDown)
{
    Assert(NewState->EndedDown != IsDown);
    NewState->EndedDown = IsDown;
    ++NewState->HalfTransitionCount;
}

void
Win32ProcessPendingMessages(game_controller_input *Keyboard)
{
    MSG Message;
    while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
    {
        switch(Message.message)
        {
            case WM_QUIT:
            {
                // TODO: Handle this with a message to the user?
                GlobalRunning = 0;
            } break;

            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            {

                u32 VKCode = (u32) Message.wParam;
                int WasDown = ((Message.lParam & (1<<30)) != FALSE); // the previous key state
                int IsDown = ((Message.lParam & (1<<31)) == FALSE); // the previous key state

                if (WasDown != IsDown)
                {
                    if (VKCode == 'W')
                    {
                        Win32ProcessKeyboardMessage(&Keyboard->Up, IsDown);
                    }
                    else if (VKCode == 'A')
                    {
                        Win32ProcessKeyboardMessage(&Keyboard->Left, IsDown);
                    }
                    else if (VKCode == 'S')
                    {
                        Win32ProcessKeyboardMessage(&Keyboard->Down, IsDown);
                    }
                    else if (VKCode == 'D')
                    {
                        Win32ProcessKeyboardMessage(&Keyboard->Right, IsDown);
                    }
                    else if (VKCode == 'E')
                    {

                    }
                    else if (VKCode == 'Q')
                    {

                    }
                    else if (VKCode == VK_UP)
                    {

                    }
                    else if (VKCode == VK_LEFT)
                    {

                    }
                    else if (VKCode == VK_DOWN)
                    {

                    }
                    else if (VKCode == VK_RIGHT)
                    {

                    }
                    else if (VKCode == VK_ESCAPE)
                    {
                       GlobalRunning = FALSE;
                    }
                    else if (VKCode == VK_SPACE)
                    {

                    }
                }

                u32 AltKeyWasDown = (Message.lParam & (1 << 29));
                if((VKCode == VK_F4) && AltKeyWasDown)
                {
                    GlobalRunning = FALSE;
                }

            } break;

            default:
            {
                TranslateMessage(&Message);
                DispatchMessageA(&Message);
            } break;
        }
    }
}

internal File
debug_platform_read_entire_file(char *Filename)
{
    File Result = {0};

    HANDLE FileHandle = CreateFile(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    // DWORD LastError = GetLastError();

    if(FileHandle != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER FileSize;
        if(GetFileSizeEx(FileHandle, &FileSize)) {
            Assert(FileSize.QuadPart <= max_u64);
            u64 FileSize64 = FileSize.QuadPart;
            void *Buffer = VirtualAlloc(0, FileSize.QuadPart, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            if(Buffer)
            {
                LPDWORD BytesRead = 0;
                // C is required to execute these in order
                // A && B && C
                // will always be A THEN B THEN C
                if(ReadFile(FileHandle, Buffer, FileSize.QuadPart, BytesRead, 0) &&
                        // if we got the file size
                        // someone truncated the file
                        // we read the file
                        // and we didn't read back the expected size we know something went wrong
                        (FileSize64 == *BytesRead))
                {
                    // file read success
                    Result.Length = *BytesRead;
                    Result.Data = Buffer;
                }
                else
                {
                    // logging
                    debug_platform_free_file_memory(Result.Data, FileSize64);
                }
            }
            else
            {
                // logging
            }
        }
        else
        {
            // logging
        }

        CloseHandle(FileHandle);
    }
    else
    {
        // logging
    }

    return Result;
}
typedef struct Win32OffscreenBuffer
{
    // Pixels are always 32-bits wide, Memory Order BB GG RR XX
    BITMAPINFO Info;
    void *Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
} Win32OffscreenBuffer;
global_variable Win32OffscreenBuffer global_back_buffer;

win32_window_dimension
Win32GetWindowDimension(HWND Window)
{
    win32_window_dimension Result;

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return(Result);
}

internal void
Win32ResizeDIBSection(Win32OffscreenBuffer *Buffer, int Width, int Height)
{
    if(Buffer->Memory)
    {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;

    int BytesPerPixel = 4;
    Buffer->BytesPerPixel = BytesPerPixel;

    // When the biHeight field is negative, this is the clue to
    // Windows to treat this bitmap as top-down, not bottom-up, meaning that
    // the first three bytes of the image are the color for the top left pixel
    // in the bitmap, not the bottom left!
    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (Buffer->Width*Buffer->Height)*BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Width*BytesPerPixel;

    // TODO: Probably clear this to black
}

internal void
win32_display_buffer_in_window(HDC DeviceContext,
                           int WindowWidth, int WindowHeight,
                           Win32OffscreenBuffer Buffer)
{
    // TODO: Aspect ratio correction

    // TODO: Play with stretch modes
    StretchDIBits(DeviceContext,
                  /*
                  X, Y, Width, Height,
                  X, Y, Width, Height,
                  */
                  0, 0, WindowWidth, WindowHeight,
                  0, 0, Buffer.Width, Buffer.Height,
                  Buffer.Memory,
                  &Buffer.Info,
                  DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK
Win32MainWindowCallback(HWND Window,
                        UINT Message,
                        WPARAM WParam,
                        LPARAM LParam)
{
    LRESULT Result = 0;

    switch(Message)
    {
        case WM_CLOSE:
        {
            // TODO: Handle this with a message to the user?
            GlobalRunning = 0;
        } break;

        case WM_ACTIVATEAPP:
        {
            OutputDebugStringA("WM_ACTIVATEAPP\n");
        } break;

        case WM_DESTROY:
        {
            // TODO: Handle this as an error - recreate window?
            GlobalRunning = 0;
        } break;

        case WM_LBUTTONDOWN:
        {
            u64 posx = GET_X_LPARAM(LParam);
            u64 posy = GET_Y_LPARAM(LParam);
            game.mouse_x = posx;
            game.mouse_y = posy;
            game.clicked = TRUE;
            char buffer[256];
            _snprintf_s(
                    buffer,
                    sizeof(buffer),
                    256,
                    "x(%lld) y(%lld)\n",
                    posx,
                    posy);
            OutputDebugStringA(buffer);
        } break;

        case WM_MOUSEMOVE:
        {
            // u64 posx = GET_X_LPARAM(LParam);
            // u64 posy = GET_Y_LPARAM(LParam);

            // char buffer[256];
            // _snprintf_s(
            //         buffer,
            //         sizeof(buffer),
            //         256,
            //         "x(%lld) y(%lld)\n",
            //         posx,
            //         posy);
            // OutputDebugStringA(buffer);

            // game.mouse_x = posx;
            // game.mouse_y = posy;
        } break;

        case WM_SETCURSOR:
        {
            LPCTSTR cursor = IDC_ARROW;
            HCURSOR hCursor = LoadCursor(NULL, cursor);
            SetCursor(hCursor);

        } break;

        case WM_INPUT:
        {
            // UINT dwSize = sizeof(RAWINPUT);
            // static BYTE lpb[sizeof(RAWINPUT)];
            //
            // GetRawInputData((HRAWINPUT)LParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER));
            //
            // RAWINPUT* raw = (RAWINPUT*)lpb;
            //
            // if (raw->header.dwType == RIM_TYPEMOUSE)
            // {
            //     // click is inside the client area
            //     if (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
            //     {
            //         game.mouse_x = raw->data.mouse.lLastX;
            //         game.mouse_y = raw->data.mouse.lLastY;
            //         char buffer[256];
            //         _snprintf_s(
            //                 buffer,
            //                 sizeof(buffer),
            //                 256,
            //                 "x(%lld) y(%lld)\n",
            //                 game.mouse_x,
            //                 game.mouse_y);
            //         OutputDebugStringA(buffer);
            //     }
            //
            //     if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
            //     {
            //         game.clicked = 1;
            //         OutputDebugStringA("Clicked\n");
            //     }
            // }
        } break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            // Keyboard events don't appear to go through the
            // window callback function. They go through the PeekMessage loop.
            Assert(!"Keyboard input came through a non-dispatch message!");
        } break;

        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);
            win32_window_dimension Dimension = Win32GetWindowDimension(Window);
            win32_display_buffer_in_window(DeviceContext, Dimension.Width, Dimension.Height,
                                       global_back_buffer);
            EndPaint(Window, &Paint);
        } break;

        default:
        {
            Result = DefWindowProc(Window, Message, WParam, LParam);
        } break;
    }

    return(Result);
}

inline LARGE_INTEGER
win32_get_wall_clock()
{
    LARGE_INTEGER Result;
    QueryPerformanceCounter(&Result);
    return Result;
}

inline f32
win32_get_seconds_elapsed(LARGE_INTEGER Start, LARGE_INTEGER End)
{
        f32 Result = ((f32)(End.QuadPart - Start.QuadPart)/
                        (f32)GlobalPerfCountFrequency);
        return Result;
}

inline void
clear_screen(GameOffscreenBuffer *Buffer)
{
    u8 *Row = (u8 *)Buffer->Memory;
    for (int Y = 0; Y < Buffer->Height; Y++)
    {
        u32 *Pixel = (u32 *)Row;
        for (int X = 0; X < Buffer->Width; X++)
        {
                        // A R G B
            *Pixel++ = (255 << 24) | (32 << 16) | (21 << 8) | 51;
        }

        Row += Buffer->Pitch;
    }
}

inline void
draw_square(GameOffscreenBuffer *Buffer, u64 square_height, u64 square_width, u64 posx, u64 posy)
{
    u8 *Row = (u8 *)Buffer->Memory + (posy * Buffer->Pitch) + (posx * sizeof(u32));
    for(int Y = 0;
            Y < square_height;
            ++Y)
    {
        u32 *Pixel = (u32 *)Row;
        for(int X = 0;
                X < square_width;
                ++X)
        {
            u8 Blue = 123;
            u8 Green = 234;
            *Pixel++ = ((Green << 8) | Blue);
        }

        Row += Buffer->Pitch;
    }
}

inline void
draw_text(GameOffscreenBuffer *Buffer, char *text, int text_len, f32 x, f32 y)
{
    //  Assume you have access to your baked font bitmap data from stbtt_BakeFontBitmap
    // (e.g., u8 *font_bitmap_pixels, which is 1024x1024)

    // Assume orthographic projection with units = screen pixels, origin at top left
    for (int i = 0; i < text_len; i++) {
        u64 c = text[i];
        if (c >= 32 && c < 128) {
            stbtt_aligned_quad q;

            // This updates x and y for the next character, and fills 'q'
            stbtt_GetBakedQuad(cdata, bitmap_width, bitmap_height, c-32, &x, &y, &q, 1);

            // 1. Get the pixel bounds of the character quad on the screen
            // Truncate/round to integer screen coordinates safely
            int x0 = (int)floorf(q.x0);
            int x1 = (int)floorf(q.x1);
            int y0 = (int)ceilf(q.y0);
            int y1 = (int)ceilf(q.y1);

            // (Optional but highly recommended) Clip against screen/buffer boundaries
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > Buffer->Width)  x1 = Buffer->Width;
            if (y1 > Buffer->Height) y1 = Buffer->Height;

            // 2. Loop through the destination screen pixels for this character
            for (int Y = y0; Y < y1; ++Y) {
                // Calculate starting row pointer for the target buffer
                u8 *Row = (u8 *)Buffer->Memory + (Y * Buffer->Pitch) + (x0 * sizeof(u32));
                u32 *Pixel = (u32 *)Row;

                // Calculate where we are vertically in the source texture (0.0 to 1.0)
                f32 t = q.t0 + ((f32)(Y - y0) / (y1 - y0)) * (q.t1 - q.t0);
                // Convert normalized 't' to actual texture row offset
                int src_y = (int)(t * bitmap_height);

                for (int X = x0; X < x1; ++X) {
                    // Calculate where we are horizontally in the source texture (0.0 to 1.0)
                    f32 s = q.s0 + ((f32)(X - x0) / (q.x1 - q.x0)) * (q.s1 - q.s0);
                    // Convert normalized 's' to actual texture column offset
                    int src_x = (int)(s * bitmap_width);

                    // 3. Sample the 8-bit alpha value out of the font bitmap
                    u8 Alpha = temp_bitmap[src_y * bitmap_width + src_x];

                    if (Alpha > 0) {
                        u8 Blue = 123;
                        u8 Green = 234;
                        u8 Red = 255; // Adding Red for standard 32-bit color

                        if (Alpha == 255) {
                            // Completely opaque pixel
                            *Pixel = (Red << 16) | (Green << 8) | Blue;
                        } else {
                            // // 4. Alpha Blending (Optional but makes the text look smooth/anti-aliased)
                            u32 SourcePixel = *Pixel;
                            u8 SrcR = (SourcePixel >> 16) & 0xFF;
                            u8 SrcG = (SourcePixel >> 8)  & 0xFF;
                            u8 SrcB = SourcePixel         & 0xFF;

                            // Linear blend equation: Dest = Src + Alpha * (Text - Src)
                            u8 OutR = SrcR + ((Alpha * (Red - SrcR)) >> 8);
                            u8 OutG = SrcG + ((Alpha * (Green - SrcG)) >> 8);
                            u8 OutB = SrcB + ((Alpha * (Blue - SrcB)) >> 8);

                            *Pixel = (OutR << 16) | (OutG << 8) | OutB;
                        }
                    }

                    Pixel++;
                }
            }
        }
    }
}

int CALLBACK
WinMain(HINSTANCE Instance,
        HINSTANCE PrevInstance,
        LPSTR CommandLine,
        int ShowCode)
{
    LARGE_INTEGER PerfCountFrequencyResult;
    QueryPerformanceFrequency(&PerfCountFrequencyResult);
    GlobalPerfCountFrequency = PerfCountFrequencyResult.QuadPart;

    // NOTE sset window scheduler granularity to 1 ms
    // so that our Sleep() can be more granular
    UINT DesiredSchedulerMS = 1;
    u64 SleepIsGranular = (timeBeginPeriod(DesiredSchedulerMS) == TIMERR_NOERROR);


    WNDCLASS WindowClass = {};

    // TODO: How do we reliably get the info about the current monitor from windows?
    int MonitorRefreshHz = 60;
    int GameUpdateHz = MonitorRefreshHz;
    f32 TargetSecondsPerFrame = 1.0f / (f32)GameUpdateHz;

    Win32ResizeDIBSection(&global_back_buffer, SCREEN_WIDTH, SCREEN_HEIGHT);

    WindowClass.style = CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    //WindowClass.hIcon;
    WindowClass.lpszClassName = "Click";

    if(RegisterClassA(&WindowClass))
    {
        HWND Window =
            CreateWindowExA(
                0,
                WindowClass.lpszClassName,
                "Handmade Hero",
                WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                0,
                0,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0,
                0,
                Instance,
                0);
        if(Window)
        {
            // NOTE: Since we specified CS_OWNDC, we can just
            // get one device context and use it forever because we
            // are not sharing it with anyone.
            HDC DeviceContext = GetDC(Window);

            int XOffset = 0;
            int YOffset = 0;

            GlobalRunning = TRUE;

#if BUILD_INTERNAL
            LPVOID BaseAddress = (void *)TB((u64)2);
#else
            LPVOID BaseAddress = 0;
#endif

            GameMemory Memory = {};
            Memory.PermenantStorageSize = MB(64);
            Memory.ScratchStorageSize = GB((u64)4);

            u64 TotalSize = Memory.PermenantStorageSize + Memory.ScratchStorageSize;
            Memory.PermenantStorage = VirtualAlloc(BaseAddress, TotalSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            Memory.ScratchStorage = ((u8 *)Memory.PermenantStorageSize + Memory.PermenantStorageSize);

            if (Memory.PermenantStorage && Memory.ScratchStorage)
            {
                // open and read ariel font file
                //
                // File TTFFile = debug_platform_read_entire_file("c:/Windows/Fonts/arial.ttf");
                //
                // // init stb font
                // stbtt_fontinfo font;
                // stbtt_InitFont(&font, (u8 *)TTFFile.Data, stbtt_GetFontOffsetForIndex((u8 *)TTFFile.Data, 0));
                //
                // int Width, Height, XOffset, YOffset;
                // u8 *MonoBitmap = stbtt_GetCodepointBitmap(
                //         &font,
                //         0,
                //         stbtt_ScaleForPixelHeight(&font, 128.0f),
                //         'A',
                //         &Width,
                //         &Height,
                //         &XOffset,
                //         &YOffset
                //         );
                //
                // // make mepty bitmap
                // // write pixels (alpha in all channels) to bitmap
                //
                // stbtt_FreeBitmap(MonoBitmap, 0);

                GameInput input[2] = {};
                GameInput *new_input = &input[0];
                GameInput *old_input = &input[1];

                new_input->dt_for_frame = TargetSecondsPerFrame;

                LARGE_INTEGER CurrentGameTime = win32_get_wall_clock();
                LARGE_INTEGER LastGameTime = CurrentGameTime;

                LARGE_INTEGER LastCounter = win32_get_wall_clock();

                // __rdtsc is only used for profiling, not game time because it's processor specific.
                u64 LastCycleCount = __rdtsc();

                srand((unsigned int)time(NULL));

                // virtual mouse starts in middle of the screen
                game.mouse_x = SCREEN_WIDTH / 2;
                game.mouse_y = SCREEN_HEIGHT / 2;

                RAWINPUTDEVICE Rid[1];
                Rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
                Rid[0].usUsage = HID_USAGE_GENERIC_MOUSE;
                Rid[0].dwFlags = RIDEV_INPUTSINK;
                Rid[0].hwndTarget = Window;
                RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]));

                fread(ttf_buffer, 1, 1<<20, fopen("c:/windows/fonts/times.ttf", "rb"));
                stbtt_BakeFontBitmap(ttf_buffer,0, 32.0, temp_bitmap,bitmap_width,bitmap_height, 32,96, cdata);

                while(GlobalRunning)
                {
                    game_controller_input *OldKeyboardController = &old_input->Controller;
                    game_controller_input *NewKeyboardController = &new_input->Controller;

                    for (int ButtonIndex = 0; ButtonIndex < ArrayCount(NewKeyboardController->Buttons); ButtonIndex++)
                    {
                        NewKeyboardController->Buttons[ButtonIndex].EndedDown =
                            OldKeyboardController->Buttons[ButtonIndex].EndedDown;
                    }

                    Win32ProcessPendingMessages(NewKeyboardController);

                    GameOffscreenBuffer Buffer = {};
                    Buffer.Memory = global_back_buffer.Memory;
                    Buffer.Width = global_back_buffer.Width;
                    Buffer.Height = global_back_buffer.Height;
                    Buffer.Pitch = global_back_buffer.Pitch;
                    Buffer.BytesPerPixel = global_back_buffer.BytesPerPixel;

                    LARGE_INTEGER WorkCounter = win32_get_wall_clock();
                    f32 WorkSecondsElapsed = win32_get_seconds_elapsed(LastCounter, WorkCounter);

                    f32 SecondsElapsedForFrame = WorkSecondsElapsed;
                    if (SecondsElapsedForFrame < TargetSecondsPerFrame)
                    {
                        while(SecondsElapsedForFrame < TargetSecondsPerFrame)
                        {
                            if(SleepIsGranular)
                            {
                                DWORD SleepMS = (DWORD)(1000.0f * (TargetSecondsPerFrame - SecondsElapsedForFrame));
                                Sleep(SleepMS);
                            }
                            SecondsElapsedForFrame = win32_get_seconds_elapsed(LastCounter, win32_get_wall_clock());
                        }
                    }
                    else
                    {
                        //TODO: missed framerate
                        //TODO: logging
                    }


                    CurrentGameTime = win32_get_wall_clock();
                    long long elapsedTicks = CurrentGameTime.QuadPart - LastGameTime.QuadPart;
                    float deltaTime = (float)((double)elapsedTicks / TargetSecondsPerFrame);
                    if (deltaTime > 0.1f)
                    {
                        deltaTime = 0.1f;
                    }

                    u32 square_width = 50;
                    u32 square_height = 50;

                    game.timer -= deltaTime;
                    if (game.timer <= 0.f || game.frame == 0)
                    {

                        float NormalizedX = (float)rand() / (float)RAND_MAX;
                        float NormalizedY = (float)rand() / (float)RAND_MAX;

                        // float NormalizedX = 20000.f / (float) RAND_MAX;
                        // float NormalizedY = 20000.f / (float) RAND_MAX;

                        game.box_pos_x = (u32)(NormalizedX * (SCREEN_WIDTH - square_width));
                        game.box_pos_y = (u32)(NormalizedY * (SCREEN_HEIGHT - square_height));


                        game.timer = GAME_TIMER;
                    }
                    game.frame++;

                    clear_screen(&Buffer);
                    draw_square(&Buffer, square_width, square_height, game.box_pos_x, game.box_pos_y);

                    f32 x = 0.f;
                    f32 y = 50.f;

                    char text[256];
                    int written = _snprintf_s(
                            text,
                            sizeof(text),
                            256,
                            "x(%lld), y(%lld)",
                            game.mouse_x,
                            game.mouse_y);
                    text[written + 1] = '\0';

                    draw_text(&Buffer, text, (written + 1), x, y);


                    if (game.clicked)
                    {
                        // if (
                        //         (game.mouse_y >= game.box_pos_y && game.mouse_y <= game.box_pos_y + square_height) &&
                        //         (game.mouse_x >= game.box_pos_x && game.mouse_x <= game.box_pos_x + square_width)
                        //    )
                        if (game.mouse_y >= game.box_pos_y)
                        {
                            OutputDebugStringA("Clicked inside of box\n");
                            draw_square(&Buffer, square_width, square_height, 20, 20);
                            game.box_pos_x = 0;
                            game.box_pos_y = 0;
                        }

                        game.clicked = 0;
                    }

                    win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                    win32_display_buffer_in_window(DeviceContext, Dimension.Width, Dimension.Height,
                            global_back_buffer);

                    GameInput *temp = new_input;
                    new_input = old_input;
                    old_input = temp;

                    LARGE_INTEGER EndCounter = win32_get_wall_clock();
                    f64 MSPerFrame = (f64)(1000.0f*win32_get_seconds_elapsed(LastCounter, EndCounter));

                    LastCounter = EndCounter;

                    u64 EndCycleCount = __rdtsc();
                    u64 CyclesElapsed = EndCycleCount - LastCycleCount;
                    LastCycleCount = EndCycleCount;
#if 0
                    // (how many elapsed / how many per second = how many seconds elapsed)
                    /* u64 FPS = (u64)(GlobalPerfCountFrequency / CounterElapsed); */
                    f64 FPS = 0.0f;
                    f64 MCPF = (f64)(CyclesElapsed / (1000.0f * 1000.0f)) ;
                    // FPS * MCPF = ~processor speed

                    char buffer[256];
                    _snprintf_s(
                            buffer,
                            sizeof(buffer),
                            256,
                            "%.02fms/f,  %.02lff/s,  %.02fmc/f\n",
                            MSPerFrame,
                            FPS,
                            MCPF);
                    OutputDebugStringA(buffer);
#endif
                }
            }
            else
            {
                // log that the game couldn't start
                OutputDebugStringA("ERROR: unable to init game");
            }
        }
        else
        {
            // TODO: Logging
        }
    }
    else
    {
        // TODO: Logging
    }

    return(0);
}

