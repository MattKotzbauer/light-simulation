#include <windows.h>
#include <stdint.h>
#include <xinput.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <initguid.h>
#include <math.h>
#include <stdio.h>

// TODO: clean up imported libraries that only have 1-2 functions

#define internal static 
#define local_persist static
#define global_variable static

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32_t bool32;

typedef float real32;
typedef double real64;

const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

int32 GlobalXOffset;
int32 GlobalYOffset;

struct win32_offscreen_buffer
{
  BITMAPINFO Info;
  void *Memory;
  int Width;
  int Height;
  int Pitch;
  int BytesPerPixel;
};


// TODO: this is a global for now
global_variable bool GlobalRunning;
global_variable win32_offscreen_buffer GlobalBackBuffer;

// globals for sound loading
global_variable IAudioClient* pAudioClientGlobal;
global_variable IAudioRenderClient* pRenderClientGlobal;
global_variable UINT32 bufferFrameCountGlobal;
global_variable real32 sampleRateGlobal;

// XInputGetState Support
// Type declaration for 'name'
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState)
// typedef DWORD WINAPI x_input_get_state(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
// DWORD WINAPI XInputGetStateStub(DWORD dwUserIndex, XINPUT_STATE* pState){}
X_INPUT_GET_STATE(XInputGetStateStub){ return ERROR_DEVICE_NOT_CONNECTED; }
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;
// (overwrite XInput declaration)
#define XInputGetState XInputGetState_

// XInputSetState
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub){ return ERROR_DEVICE_NOT_CONNECTED; }
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_


internal void Win32LoadXInput(void){
  // TODO (
  HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");
  if (XInputLibrary == NULL){
    // TODO: diagnostic
    XInputLibrary = LoadLibraryA("xinput1_3.dll");
  }
  if(XInputLibrary){
    // (void* is default return type of GetProcAddress)
    XInputGetState = (x_input_get_state*)GetProcAddress(XInputLibrary, "XInputGetState");
    XInputSetState = (x_input_set_state*)GetProcAddress(XInputLibrary, "XInputSetState");
  }
  else{
    // TODO: diagnostic
  }
}

internal void Win64InitWASAPI(HWND Window){

  // https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi
  HRESULT hr;

  // 1: Initialize COM
  hr = CoInitializeEx(
			 NULL,
			 COINIT_MULTITHREADED
			 );
  if FAILED(hr){ return;}

  // 2: Create IMMDeviceEnumerator
  IMMDeviceEnumerator* pEnumerator = NULL;
  hr = CoCreateInstance(
			__uuidof(MMDeviceEnumerator),
			NULL,
			CLSCTX_ALL,
			IID_PPV_ARGS(&pEnumerator)
			);
  if FAILED(hr){ return; }
  
  // 3: Get default audio endpoint
  IMMDevice* pDevice = NULL;
  hr = pEnumerator->GetDefaultAudioEndpoint(
					    eRender,
					    eConsole,
					    &pDevice
					    );
  if(FAILED(hr)){ return; }

  // 4: Activate IAudioClient
  //IAudioClient *pAudioClient = NULL;
  pAudioClientGlobal = NULL;
  hr = pDevice->Activate(
			 IID_IAudioClient,
			 CLSCTX_ALL,
			 NULL,
			 (void**)&pAudioClientGlobal
			 );
  if (FAILED(hr)){
    OutputDebugStringA("Activate IAudioClient failed.\n"); return; }
  
  // 5: Get mix format
  WAVEFORMATEX *pwfx = NULL;
  hr = pAudioClientGlobal->GetMixFormat(&pwfx);
  if (FAILED(hr)) { return; }
  
  // 6: Initialize audio format
  hr = pAudioClientGlobal->Initialize(
				AUDCLNT_SHAREMODE_SHARED,
				0,
				10000000,
				0,
				pwfx,
				NULL
				);
  if (FAILED(hr)) { return; }
      
  // 7: get size of allocated buffer
  //  UINT32 bufferFrameCount;
  
  hr = pAudioClientGlobal->GetBufferSize(&bufferFrameCountGlobal);
  if (FAILED(hr)) { return; }
  
  // 8: 
  //IAudioRenderClient* pRenderClient = NULL;
  pRenderClientGlobal = NULL;
  hr = pAudioClientGlobal->GetService(
				IID_IAudioRenderClient,
				(void**)&pRenderClientGlobal
				);
  if (FAILED(hr)) { return; }
  
  // 9: start audio client
  hr = pAudioClientGlobal->Start();
  if (FAILED(hr)) { return; }

  // (Check pwfx format)
  bool isFormatFloat = (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) ||
                   (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
  if(!isFormatFloat) { return; }

  sampleRateGlobal = pwfx->nSamplesPerSec;

  
}

struct win32_window_dimension{
  int Width;
  int Height;
};

internal win32_window_dimension GetWindowDimension(HWND Window){
  win32_window_dimension Result;
  
  RECT ClientRect;
  GetClientRect(Window, &ClientRect);
  Result.Width = ClientRect.right - ClientRect.left;
  Result.Height = ClientRect.bottom - ClientRect.top;

  return Result;
}

/* Render animated pixel content into Buffer */
internal void RenderWeirdGradient(win32_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset)
{
  int32 XCenter = Buffer->Width / 2;
  int32 YCenter = Buffer->Height / 2;
  int32 SquareSize = 50;
  
  uint8 *Row = (uint8*)Buffer->Memory;
  for(int Y = 0;
      Y < Buffer->Height;
      ++Y)
    {
      // (Row-wise iteration)
      uint32 *Pixel = (uint32*) Row; 
      for(int X = 0;
	  X < Buffer->Width;
	  ++X
	  )
	{
	  if(
	     X < XCenter + SquareSize
	     && X > XCenter - SquareSize
	     && Y < YCenter + SquareSize
	     && Y > YCenter - SquareSize
	     ){
	    *Pixel++ = 0x00FF0000;
	  }
	  else{
	    // Blue: 
	    uint8 Blue = (X + BlueOffset);
	    // Green:
	    uint8 Green = (Y + GreenOffset);
	    // (Red Blank)
	    *Pixel++ = ((Green << 8) | Blue);
	  }
	}

      Row += Buffer->Pitch;
    }
}



internal void Win64ResizeDIBSection(win32_offscreen_buffer* Buffer, int Width, int Height)
{
  if (Buffer->Memory)
    {
      VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

  Buffer->Width = Width;
  Buffer->Height = Height;
  Buffer->BytesPerPixel = 4;
  
  Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
  Buffer->Info.bmiHeader.biWidth = Buffer->Width;
  // NOTE: when biHeight field is negative, serves as clue to Windows to treat bitmap as top-down rather than bottom-up: first 3 bytes of image are colors for top-left pixel
  Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
  Buffer->Info.bmiHeader.biPlanes = 1;
  Buffer->Info.bmiHeader.biBitCount = 32;
  Buffer->Info.bmiHeader.biCompression = BI_RGB;

  int BitmapMemorySize = (Width * Height) * Buffer->BytesPerPixel;
  Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);

  // TODO: probably clear this to black
  Buffer->Pitch = Buffer->BytesPerPixel * Width;
  
}

internal void Win64DisplayBufferInWindow(HDC DeviceContext, int WindowWidth, int WindowHeight, win32_offscreen_buffer *Buffer)
{
  // TODO: aspect ratio correction
  StretchDIBits(DeviceContext,
		0, 0, WindowWidth, WindowHeight,
		0, 0, Buffer->Width, Buffer->Height,
		Buffer->Memory,
		&Buffer->Info,
		DIB_RGB_COLORS, SRCCOPY);
}


/* WNDPROC function associated with our main window class (WNDCLASSA WindowClass) */
LRESULT Win64MainWindowCallback(
  HWND Window,
  UINT Message,
  WPARAM WParam,
  LPARAM LParam)
{
  LRESULT Result = 0;
  
  switch(Message){
  case WM_SIZE: 
  {
  } break;

  case WM_CLOSE:
  {
    // TODO: handle this with message to the user
    GlobalRunning = false;
  } break;

  case WM_ACTIVATEAPP:
  {
    OutputDebugStringA("wm_activateapp\n");
  } break;

  case WM_DESTROY:
  {
    // TODO: handle this as an error, recreate window
     GlobalRunning = false;
  } break;
  
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_KEYDOWN:
  case WM_KEYUP:
  {
    uint32 VK_Code = WParam;
    bool WasDown = ((LParam & (1 << 30)) != 0);
    bool IsDown = ((LParam & (1 << 31)) == 0); 
    switch(VK_Code){
    case 'W': { OutputDebugStringA("W\n");
    } break;
    case 'A': {
    } break;
    case 'S': {
    } break;
    case 'D': {
    } break;
    case 'Q': {
    } break;
    case 'E': {
    } break;
    case VK_UP: {
    } break;
    case VK_LEFT: {
    } break;
    case VK_DOWN: {
    } break;
    case VK_RIGHT: {
    } break;
    case VK_ESCAPE: {
      OutputDebugStringA("Escape: ");
      if(WasDown){OutputDebugStringA("WasDown");}
      if(IsDown){OutputDebugStringA("IsDown ");}
      OutputDebugStringA("\n");
    } break;
    case VK_SPACE: {
    } break;
    case VK_F4: {
      if ((LParam & (1 << 29)) != 0){ GlobalRunning = false; }
    } break;
    }
  } break;

  case WM_PAINT:
  {
    PAINTSTRUCT Paint;
    HDC DeviceContext = BeginPaint(Window, &Paint);

    win32_window_dimension Dimension = GetWindowDimension(Window);
    Win64DisplayBufferInWindow(DeviceContext, Dimension.Width, Dimension.Height, &GlobalBackBuffer);

    EndPaint(Window, &Paint); // (returns bool)
 
  } break;

  default:
  {
    //OutputDebugStringA("default");
    Result = DefWindowProcA(Window, Message, WParam, LParam);

  } break;
  
  }
  return Result;
}


/* Driver Function */
int WINAPI WinMain(HINSTANCE Instance,
		   HINSTANCE PrevInstance,
		   PSTR CommandLine,
		   int ShowCode)
{
  // MessageBox(0, "This is Handmade Hero.", "Handmade Hero", MB_OK|MB_ICONINFORMATION)

  Win32LoadXInput();
  
  WNDCLASSA WindowClass = {};

  Win64ResizeDIBSection(&GlobalBackBuffer, 1280, 720);
  
  WindowClass.style = CS_HREDRAW|CS_VREDRAW;
  WindowClass.lpfnWndProc = Win64MainWindowCallback;
  WindowClass.hInstance = Instance; 
  // HICON hIcon; 
  WindowClass.lpszClassName = "HandmadeHeroWindowClass"; 

  LARGE_INTEGER PerfCountFrequencyResult;
  QueryPerformanceFrequency(&PerfCountFrequencyResult);
  int64 PerfCountFrequency = PerfCountFrequencyResult.QuadPart;

  
  if (RegisterClassA(&WindowClass))
    {
      HWND Window = CreateWindowExA(
				    0,
				    WindowClass.lpszClassName,
				    "Handmade Hero",
				    WS_OVERLAPPEDWINDOW|WS_VISIBLE,
				    CW_USEDEFAULT,
				    CW_USEDEFAULT,
				    CW_USEDEFAULT,
				    CW_USEDEFAULT,
				    0,
				    0,
				    Instance,
				    0);
      if(Window)
	{
	  // (Graphics vars)
	  GlobalXOffset = 0;
	  GlobalYOffset = 0;

	  GlobalRunning = true;

	  HDC DeviceContext = GetDC(Window);

	  Win64InitWASAPI(Window);

	  // Sound vars
	  HRESULT hr;
	  real32 frequencyHz = 261;
	  real32 phase = 0.0;
	  real32 phaseDelta = (2.0 * 3.14159265358979323846 * frequencyHz) / sampleRateGlobal;
	  real32 volume = 1;
	  real32 sample;

	  // LARGE_INTEGER BeginCounter;
	  // QueryPerformanceCounter(&BeginCounter);

	  LARGE_INTEGER LastCounter;
	  QueryPerformanceCounter(&LastCounter);
	  int64 LastCycleCount =  __rdtsc();	  
	  
	  while(GlobalRunning){
	 
	    MSG Message;
	    // win32_offscreen_buffer Buffer;
	    while(PeekMessageA(&Message, 0, 0, 0, PM_REMOVE))
	      {
		if(Message.message == WM_QUIT)
		  {
		    GlobalRunning = false;
		  }

		TranslateMessage(&Message);
		DispatchMessage(&Message); 
	      }

	    // Process frame-perfect keyboard input
	    if(GetAsyncKeyState('S') & (1 << 15)){ GlobalYOffset += 1; }
	    if(GetAsyncKeyState('W') & (1 << 15)){ GlobalYOffset -= 1; }
	    if(GetAsyncKeyState('D') & (1 << 15)){ GlobalXOffset += 1; }
	    if(GetAsyncKeyState('A') & (1 << 15)){ GlobalXOffset -= 1; }
	    
	    // Render Visual Buffer
	    RenderWeirdGradient(&GlobalBackBuffer, GlobalXOffset, GlobalYOffset);

	    /* 
	    // SOUND LOAD START
	    UINT32 currentPaddingFrames;
	    hr = pAudioClientGlobal->GetCurrentPadding(&currentPaddingFrames);
	    if(FAILED(hr)){ OutputDebugStringA("Failed to get current audio frame padding"); }
	    UINT32 currentAvailableFrames = bufferFrameCountGlobal - currentPaddingFrames;
	    if (currentAvailableFrames > 0){

	      BYTE* pData = NULL;
	      hr = pRenderClientGlobal->GetBuffer(currentAvailableFrames, &pData);
	      if(FAILED(hr)){ OutputDebugStringA("Failed to get audio buffer"); }
	      for(uint32 i = 0; i < currentAvailableFrames; ++i){
	     

		// GENERATE SAMPLE START
		// sample = GenerateWave(volume, frequencyHz, sampleRateGlobal, phase);
		sample = sinf(phase) * volume;

		phase += phaseDelta;
		if (phase >= 2.0 * 3.14159265358979323846){
		  phase -= 2.0 * 3.14159265358979323846;
		}
		// GENERATE SAMPLE END

		((float*)pData)[i*2] = sample; // Left channel
		((float*)pData)[i*2 + 1] = sample; // Right channel
	     
	      }
	      pRenderClientGlobal->ReleaseBuffer(currentAvailableFrames, 0);

	    }
	    // SOUND LOAD END
	    */
	 
	    win32_window_dimension Dimension = GetWindowDimension(Window);
	    Win64DisplayBufferInWindow(DeviceContext, Dimension.Width, Dimension.Height, &GlobalBackBuffer);
	 
	    //++XOffset;

	    // Time Tracking (QueryPerformanceCounter, rdtsc): 
	    int64 EndCycleCount = __rdtsc();
	    LARGE_INTEGER EndCounter;
	    QueryPerformanceCounter(&EndCounter);	    
	    
	    int64 CyclesElapsed = EndCycleCount - LastCycleCount;
	    int64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
	    LastCounter = EndCounter;

	    // (Debugging: )
	    real32 MSPerFrame = ((real32)(1000.0f * (real32)CounterElapsed) / (real32)PerfCountFrequency);
	    real32 FPS = (real32)PerfCountFrequency / (real32)CounterElapsed;
	    real32 MCPF = (real32)CyclesElapsed / (1000.0f * 1000.0f);
	    
	    char Buffer[256];
	    sprintf(Buffer, "ms / frame: %.02fms --- FPS: %.02ffps --- m-cycles / frame: %.02f\n", MSPerFrame, FPS, MCPF);
	    OutputDebugStringA(Buffer);

	    // GlobalXOffset += 1;
	    // GlobalYOffset -= 1;
	    
	    // (end of while(GlobalRunning loop)
	  }

	  hr = pAudioClientGlobal->Stop();
	  if(FAILED(hr)){ OutputDebugStringA("stopping of audio client failed"); }
       
       
	}
      else{
	// TODO: CreateWindowExA error handling
      }
    }
  else{
    // TODO: RegisterClassA error handling
  }
  
  
  return(0);
}