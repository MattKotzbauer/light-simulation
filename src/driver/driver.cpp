#include <windows.h>
#include <stdint.h>
#include <xinput.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <initguid.h>
#include <math.h>
#include <stdio.h>

/*
Large-scale TODO's:
* HSV conversion (have colors correspond to varying values of dark blue as start)
* change SimulationGrid to be set of contiguous arrays
* change SimulationDriver() call to have pointer to simulation grid as parameter, then also pass into SimulationRender(), then we can remove globals
* (control saturation through pixel guy)


(* clean up imported libraries)
  
 */


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

struct NS_Pixel{
  real32 Density;
  real32 VelocityX;
  real32 VelocityY;

  real32 AuxDensity;
  real32 PriorDensity;
  real32 PriorVelocityX;
  real32 PriorVelocityY;

  real32 DensitySource;
};

global_variable real32 GlobalDiffusionRate = 1.0f;
global_variable real32 GlobalBaseDeltaTime = 20.0f;

// TODO: make these configurable
const int32 GlobalHeight = 720;
const int32 GlobalWidth = 1280;

// TODO: (when using, bound queries to be within the GlobalWidth, GlobalHeight bounds)
#define IX(i,j) (i+(GlobalWidth+2)*j)

global_variable bool GlobalRunning;
// TODO: convert into pair of single-dimensional arrays for density and velocity
// TODO: support down-scaled version that treats multiple render pixels as single simulation pixel
global_variable NS_Pixel SimulationGrid[GlobalHeight + 2][GlobalWidth + 2];
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

/* Initialize contents of SimulationGrid on first frame */
internal void NSSimulationInit(){
  // (we'll initialize to 0 for now)
  for(int i = 1; i <= GlobalHeight; ++i){
    for(int j = 1; j <= GlobalWidth; ++j){
      SimulationGrid[i][j].DensitySource = 0;
      SimulationGrid[i][j].Density = 0;
      SimulationGrid[i][j].PriorDensity = 0;
    }
  }
  // SimulationGrid[300][300].SourceValue = 3;
  for(int i = 300; i < 400; ++i){ for(int j = 300; j < 400; ++j){ SimulationGrid[i][j].DensitySource = 1; }}
}


internal void SimulationDriver(){
  // A: Simluate 

  // 1: Density Simulation
  
  // a: Sourcing
  for(int i = 1; i < GlobalHeight; ++i){
    for(int j = 1; j < GlobalWidth; ++j){
      SimulationGrid[i][j].AuxDensity = SimulationGrid[i][j].Density;
      SimulationGrid[i][j].Density += SimulationGrid[i][j].DensitySource * GlobalBaseDeltaTime;
    }
  }

  
  // b: Diffusion
  real32 DiffusionConstant = GlobalHeight * GlobalWidth * GlobalDiffusionRate * GlobalBaseDeltaTime;
  for(int GaussSeidelIterations = 0; GaussSeidelIterations < 20; ++GaussSeidelIterations){
    for(int i = 1; i <= GlobalHeight; ++i){
      for(int j = 1; j <= GlobalWidth; ++j){
	SimulationGrid[i][j].Density = (SimulationGrid[i][j].PriorDensity
	  + DiffusionConstant * (SimulationGrid[i-1][j].Density + SimulationGrid[i+1][j].Density
				 + SimulationGrid[i][j+1].Density + SimulationGrid[i][j-1].Density
				 ))/(1+4*DiffusionConstant);
      }
    }
    // TODO: set boundary
  }

  /* 
  // c: Advection
  int32 InterpolationI0, InterpolationI1, InterpolationJ0, InterpolationJ1;
  float BackTraceX, BackTraceY, RelativeDeltaTime;
  float BW[4]; // (Bilinear Weights)
  RelativeDeltaTime = GlobalBaseDeltaTime * (real32)((GlobalHeight + GlobalWidth)/2);
  for(int i = 1; i <= GlobalWidth; ++i){
    for(int j = 1; j <= GlobalHeight; ++j){

      BackTraceX = i - (SimulationGrid[i][j].VelocityX * RelativeDeltaTime);
      BackTraceY = j - (SimulationGrid[i][j].VelocityY * RelativeDeltaTime);

      // (Normalize BackTraceX, BackTraceY)
      if(BackTraceX < 0.5) BackTraceX = 0.5; if (BackTraceX > GlobalHeight + 0.5) BackTraceX = GlobalHeight + 0.5;
      if(BackTraceY < 0.5) BackTraceY = 0.5; if (BackTraceY > GlobalWidth + 0.5) BackTraceY = GlobalWidth + 0.5;

      InterpolationI0 = (int32)BackTraceX; InterpolationI1 = InterpolationI0 + 1;
      InterpolationJ0 = (int32)BackTraceY; InterpolationJ1 = InterpolationJ0 + 1;
      
      BW[1] = BackTraceX - InterpolationI0; BW[0] = 1 - BW[1];
      BW[3] = BackTraceY - InterpolationJ0; BW[2] = 1 - BW[3];

      SimulationGrid[i][j].Density = BW[0] * (BW[2] * SimulationGrid[InterpolationI0][InterpolationJ0].PriorDensity
					      + BW[3] * SimulationGrid[InterpolationI0][InterpolationJ1].PriorDensity)
	+ BW[1] * (BW[2] * SimulationGrid[InterpolationI1][InterpolationJ0].PriorDensity
		   + BW[3] * SimulationGrid[InterpolationI1][InterpolationJ1].PriorDensity);

 
      
      // TODO: set boundary
      
    }
  }

  */
	  
  // 2: (Velocity Simulation)


  // Other Variable Changes:
  for(int i = 1; i <= GlobalHeight; ++i){
    for(int j = 1; j <= GlobalWidth; ++j){
      // (Overflow cleanup)
      SimulationGrid[i][j].Density = (SimulationGrid[i][j].Density > 255) ? 255 : SimulationGrid[i][j].Density;
      // (Update prior densities)
      SimulationGrid[i][j].PriorDensity = SimulationGrid[i][j].Density;
    }
  }
	  
}

/* Render animated pixel content into Buffer */
internal void SimulationRender(win32_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset)
{

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
	  real32 PixelDensity = SimulationGrid[Y + 1][X + 1].Density;
	  // PixelDensity = 250;
	  uint8 Blue = (PixelDensity > 255) ? 255 : (uint8)PixelDensity;
	  // Green:
	  // uint8 Green = (Y + GreenOffset);
	  // (Red Blank)
	  // *Pixel++ = ((Green << 8) | Blue);
	  *Pixel++ = Blue;
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

  NSSimulationInit();
  
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

	  // Win64InitWASAPI(Window);

	  // Sound vars
	  /* 
	  HRESULT hr;
	  real32 frequencyHz = 261;
	  real32 phase = 0.0;
	  real32 phaseDelta = (2.0 * 3.14159265358979323846 * frequencyHz) / sampleRateGlobal;
	  real32 volume = 1;
	  real32 sample;
	  */
	  
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

	    // Render Visual Buffer
	    SimulationDriver();
	    SimulationRender(&GlobalBackBuffer, GlobalXOffset, GlobalYOffset);

	    win32_window_dimension Dimension = GetWindowDimension(Window);
	    Win64DisplayBufferInWindow(DeviceContext, Dimension.Width, Dimension.Height, &GlobalBackBuffer);
	 
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
