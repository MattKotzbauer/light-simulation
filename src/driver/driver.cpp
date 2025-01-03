#include <windows.h>
#include <stdint.h>
#include <xinput.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <initguid.h>
#include <math.h>
#include <stdio.h>

#include <windowsx.h>

/*
Large-scale TODO's:


* move simulation functions to a helper file that we then import
* HSV conversion (have colors correspond to varying values of dark blue as start)
* change SimulationDriver() call to have pointer to simulation grid as parameter, then also pass into SimulationRender(), then we can remove globals
* (control saturation through pixel guy)

* (play around and see if there's an easier way to instantiate arrays / allocate memory: could have pointer be to malloc call of size (sizeof(real32) *  (GlobalHeight + 2) * (GlobalWidth + 2)), which could be allocated within SimulationInit, though may want to use some type of static marker to ensure they're stored in data segment rather than on stack, since may stack overflow)

(* clean up imported libraries: math.h, stdio.h may not be necessary, as well as xinput, etc dependeing on how we're processing input)
  
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

global_variable real32 GlobalDiffusionRate = 1.0f;
global_variable real32 GlobalBaseDeltaTime = 1.0f;

// TODO: make these configurable
const int32 GlobalHeight = 720;
const int32 GlobalWidth = 1280;
// const int32 SimulationHeight = 720;
// const int32 SimulationWidth = 1280;
const int32 SimulationHeight = 180;
const int32 SimulationWidth = 320;

const int32 GlobalXScale = GlobalWidth / SimulationWidth;
const int32 GlobalYScale = GlobalHeight / SimulationHeight;

// TODO: (when using, bound queries to be within the GlobalWidth, GlobalHeight bounds)
#define IX(i,j) (i+(SimulationWidth+2)*(j))

struct SimulationData{
  real32* Density;
  real32* PriorDensity;
  real32* VelocityX;
  real32* PriorVelocityX;
  real32* VelocityY;
  real32* PriorVelocityY;

  real32* DensitySources;
  real32* VelocityXSources;
  real32* VelocityYSources;
};

global_variable SimulationData SimulationGrid;

global_variable real32** SimulationArrays[] = {
  &SimulationGrid.Density,
  &SimulationGrid.PriorDensity,
  &SimulationGrid.VelocityX,
  &SimulationGrid.PriorVelocityX,
  &SimulationGrid.VelocityY,
  &SimulationGrid.PriorVelocityY,
  &SimulationGrid.DensitySources,
  &SimulationGrid.VelocityXSources,
  &SimulationGrid.VelocityYSources
};

// Globals for window management
global_variable bool GlobalRunning;
global_variable win32_offscreen_buffer GlobalBackBuffer;

// Globals for sound loading
global_variable IAudioClient* pAudioClientGlobal;
global_variable IAudioRenderClient* pRenderClientGlobal;
global_variable UINT32 bufferFrameCountGlobal;
global_variable real32 sampleRateGlobal;

// Globals for input management
global_variable bool GlobalMouseDown = false;
// TODO: scope lexically once we better understand where we use these
global_variable int32 PriorMouseX = 0;
global_variable int32 PriorMouseY = 0;
global_variable int32 GlobalMouseX = 0;
global_variable int32 GlobalMouseY = 0;


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
  int32 ArrayCount = sizeof(SimulationArrays) / sizeof(SimulationArrays[0]);
  int32 ArraySize = sizeof(real32) * (SimulationWidth + 2) * (SimulationHeight + 2);

  for(int ArrayIndex = 0; ArrayIndex < ArrayCount; ++ArrayIndex){
    *SimulationArrays[ArrayIndex] = (real32*)VirtualAlloc(0, ArraySize, MEM_COMMIT, PAGE_READWRITE);
  }
  
  // (we'll initialize to 0 for now)
  for(int ArrayIndex = 0; ArrayIndex < ArrayCount; ++ArrayIndex){
    for(int i = 0; i <= SimulationWidth + 1; ++i){
      for(int j = 0; j <= SimulationHeight + 1; ++j){
	(*SimulationArrays[ArrayIndex])[IX(i,j)] = 0;
      }      
    }
  }

  // TEMPORARY: manual allocation of density / velocity
  // SimulationGrid[300][300].SourceValue = 3;
  for(int i = 50; i < 100; ++i){ for(int j = 50; j < 100; ++j){
      SimulationGrid.DensitySources[IX(i,j)] = 10;
      SimulationGrid.VelocityXSources[IX(i,j)] = 5;
      if(i > 75){SimulationGrid.DensitySources[IX(i,j)] = 0;
	}
    }}
}


internal void NSUpdate(real32* &PriorValues, real32* &CurrentValues){
  real32* SwapPointer = PriorValues;
  PriorValues = CurrentValues;
  CurrentValues = SwapPointer;
  
}
  

internal void NSAddSource(real32* Destination, real32* Source){
  // (Sourcing Step)
  for(int i = 1; i <= SimulationWidth; ++i){
    for(int j = 1; j <= SimulationHeight; ++j){
      Destination[IX(i,j)] += Source[IX(i,j)] * GlobalBaseDeltaTime;
    }
  }
}

internal void NSBound(int Mode, real32* Array){
  // Walls
  for(int i = 1; i <= SimulationWidth; ++i){
    Array[IX(i, 0)] = Mode == 2 ? -Array[IX(i,1)] : Array[IX(i,1)];
    Array[IX(i, SimulationHeight + 1)] = Mode == 2 ?
      -Array[IX(i,SimulationHeight)] : Array[IX(i,SimulationHeight)];
  }
  for(int j = 1; j <= SimulationHeight; ++j){
    Array[IX(0, j)] = Mode == 1 ? -Array[IX(1,j)] : Array[IX(1,j)];
    Array[IX(SimulationWidth + 1, j)] = Mode == 1 ?
      -Array[IX(SimulationWidth, j)] : Array[IX(SimulationWidth, j)];
  }
  
  // Corners
  Array[IX(0,0)] = 0.5f * (Array[IX(0,1)] + Array[IX(1,0)]);
  Array[IX(0,SimulationHeight+1)] = 0.5f * (Array[IX(0,SimulationHeight)] + Array[IX(1,SimulationHeight+1)]);
  Array[IX(SimulationWidth+1,0)] = 0.5f * (Array[IX(SimulationWidth,0)] + Array[IX(SimulationWidth+1,1)]);
  Array[IX(SimulationWidth+1,SimulationHeight+1)] = 0.5f * (Array[IX(SimulationWidth+1,SimulationHeight)] +
							    Array[IX(SimulationWidth,SimulationHeight+1)]);
  
} 


internal void NSDiffuse(int32 Mode, real32* DiffusionTarget, real32* TargetPrior){
  // Gauss-Seidel Relaxation (20 passes): 
  real32 DiffusionConstant = GlobalDiffusionRate * GlobalBaseDeltaTime;
  
  for(int GaussSeidelIterations = 0; GaussSeidelIterations < 20; ++GaussSeidelIterations){
    for(int i = 1; i <= SimulationWidth; ++i){
      for(int j = 1; j <= SimulationHeight; ++j){
	SimulationGrid.Density[IX(i,j)] = (SimulationGrid.PriorDensity[IX(i,j)] +
					   DiffusionConstant * (
								SimulationGrid.Density[IX(i-1,j)] + 
								SimulationGrid.Density[IX(i+1,j)] +
								SimulationGrid.Density[IX(i,j-1)] +
								SimulationGrid.Density[IX(i,j+1)]
								))/(1 + 4 * DiffusionConstant);
      }}
    
    NSBound(Mode, DiffusionTarget);
  }
}


internal void NSAdvect(int32 Mode, real32* AdvectionTarget, real32* TargetPrior){
  int32 InterpolationI0, InterpolationI1, InterpolationJ0, InterpolationJ1;
  float BackTraceX, BackTraceY, RelativeDeltaTime;
  float BW[4]; // (Bilinear Weights)
  for(int i = 1; i < SimulationWidth; ++i){
    for(int j = 1; j < SimulationHeight; ++j){

      BackTraceX = i - (SimulationGrid.VelocityX[IX(i,j)] * GlobalBaseDeltaTime);
      BackTraceY = j - (SimulationGrid.VelocityY[IX(i,j)] * GlobalBaseDeltaTime);

      // (Normalize BackTraceX, BackTraceY)
      if(BackTraceX < 0.5) BackTraceX = 0.5; if (BackTraceX > SimulationHeight + 0.5) BackTraceX = SimulationWidth + 0.5;
      if(BackTraceY < 0.5) BackTraceY = 0.5; if (BackTraceY > SimulationWidth + 0.5) BackTraceY = SimulationHeight + 0.5;

      InterpolationI0 = (int32)BackTraceX; InterpolationI1 = InterpolationI0 + 1;
      InterpolationJ0 = (int32)BackTraceY; InterpolationJ1 = InterpolationJ0 + 1;
      
      BW[1] = BackTraceX - InterpolationI0; BW[0] = 1 - BW[1];
      BW[3] = BackTraceY - InterpolationJ0; BW[2] = 1 - BW[3];

      AdvectionTarget[IX(i,j)] =
	BW[0] *
	(BW[2] * TargetPrior[IX(InterpolationI0,InterpolationJ0)]
	 + BW[3] * TargetPrior[IX(InterpolationI0,InterpolationJ1)]) + 
	BW[1] *
	(BW[2] * TargetPrior[IX(InterpolationI1,InterpolationJ0)]
	 + BW[3] * TargetPrior[IX(InterpolationI1,InterpolationJ1)]);
      
    }
  }
  
  NSBound(Mode, AdvectionTarget);
}


internal void NSProject(real32* Poisson, real32* Divergence){
  real32 ProjectionConstantX = 1.0f / (real32)SimulationWidth;
  real32 ProjectionConstantY = 1.0f / (real32)SimulationHeight;
      
  for(int i = 1; i <= SimulationWidth; ++i){
    for(int j = 1; j <= SimulationHeight; ++j){
      Divergence[IX(i,j)] = -0.5 * ProjectionConstantY *
	(
	 SimulationGrid.VelocityX[IX(i+1,j)] - SimulationGrid.VelocityX[IX(i-1,j)] +
	 SimulationGrid.VelocityY[IX(i,j+1)] - SimulationGrid.VelocityY[IX(i,j-1)]
	 );
      Poisson[IX(i,j)] = 0;
    }
  }
  
  NSBound(0, Divergence); NSBound(0, Poisson);

  for(int GaussSeidelIterations = 0; GaussSeidelIterations < 20; ++GaussSeidelIterations){
    for(int i = 1; i <= SimulationWidth; ++i){
      for(int j = 1; j <= SimulationHeight; ++j){
	Poisson[IX(i,j)] = (Divergence[IX(i,j)] + Poisson[IX(i-1,j)] +
			    Poisson[IX(i+1,j)] + Poisson[IX(i,j-1)] + Poisson[IX(i,j+1)]) / 4.0f;
      }
    }
    NSBound(0, Poisson);
  }

  for(int i = 1; i <= SimulationWidth; ++i){
    for(int j = 1; j <= SimulationHeight; ++j){
      SimulationGrid.VelocityX[IX(i,j)] -= 0.5 * (Poisson[IX(i+1,j)] - Poisson[IX(i-1,j)]) / ProjectionConstantX;
      SimulationGrid.VelocityY[IX(i,j)] -= 0.5 * (Poisson[IX(i,j+1)] - Poisson[IX(i,j-1)]) / ProjectionConstantY;
    }
  }
  NSBound(1, SimulationGrid.VelocityX); NSBound(2, SimulationGrid.VelocityY);
  
}


internal void SimulationDriver(){

  // 1: Density Operations
  NSUpdate(SimulationGrid.PriorDensity, SimulationGrid.Density);
  
  NSAddSource(SimulationGrid.PriorDensity, SimulationGrid.DensitySources);

  NSDiffuse(0, SimulationGrid.Density, SimulationGrid.PriorDensity);

  NSUpdate(SimulationGrid.PriorDensity, SimulationGrid.Density);
  NSAdvect(0, SimulationGrid.Density, SimulationGrid.PriorDensity); NSBound(0, SimulationGrid.Density);

  // 2: Velocity Operations
  NSAddSource(SimulationGrid.VelocityX, SimulationGrid.VelocityXSources);
  NSAddSource(SimulationGrid.VelocityY, SimulationGrid.VelocityYSources);

  NSUpdate(SimulationGrid.VelocityX, SimulationGrid.PriorVelocityX);
  NSDiffuse(1, SimulationGrid.VelocityX, SimulationGrid.PriorVelocityX);
  
  NSUpdate(SimulationGrid.VelocityY, SimulationGrid.PriorVelocityY);
  NSDiffuse(2, SimulationGrid.VelocityY, SimulationGrid.PriorVelocityY);

  NSProject(SimulationGrid.PriorVelocityX, SimulationGrid.PriorVelocityY);

  NSUpdate(SimulationGrid.VelocityX, SimulationGrid.PriorVelocityX);
  NSUpdate(SimulationGrid.VelocityY, SimulationGrid.PriorVelocityY);

  NSAdvect(1, SimulationGrid.VelocityX, SimulationGrid.PriorVelocityX); 
  NSAdvect(2, SimulationGrid.VelocityY, SimulationGrid.PriorVelocityY); 

  NSProject(SimulationGrid.PriorVelocityX, SimulationGrid.PriorVelocityY);
 
  // (3: Overflow Prevention / Cleanup)
  for(int i = 1; i <= SimulationWidth; ++i){
    for(int j = 1; j <= SimulationHeight; ++j){

      SimulationGrid.Density[IX(i,j)] = (SimulationGrid.Density[IX(i,j)] > 500) ? 500 : SimulationGrid.Density[IX(i,j)];

      SimulationGrid.PriorDensity[IX(i,j)] = (SimulationGrid.PriorDensity[IX(i,j)] > 500) ? 500 : SimulationGrid.PriorDensity[IX(i,j)];
    }
  }

  
}

/* Render animated pixel content into Buffer */
internal void SimulationRender(win32_offscreen_buffer *Buffer)
{
  // (Assuming that Buffer->Width, Buffer->Height hold current window width / height) 

  // Scaling for (320 x 180) Simulation Dimensions 
  // WindowScale = min(Buffer->Width / SimulationWidth, Buffer->Height / SimulationHeight)
  int32 WindowScale = (Buffer->Width / SimulationWidth) < (Buffer->Height / SimulationHeight) ? 
    Buffer->Width / SimulationWidth : Buffer->Height / SimulationHeight;

  // Total pixels occupied by simulation
  int32 ScaledWidth =  WindowScale * SimulationWidth, ScaledHeight = WindowScale * SimulationHeight;

  // Offsets for simulation window within client window
  // OffsetX = max(0.5f * (Buffer->Width - ScaledWidth), 0)
  int32 OffsetX = Buffer->Width > ScaledWidth ?
    0.5f * (Buffer->Width - ScaledWidth) : 0;
  int32 OffsetY = Buffer->Height > ScaledHeight ?
    0.5f * (Buffer->Height - ScaledHeight) : 0;
  

  uint32 ScaledX, ScaledY;
  uint8 *Row = (uint8*)Buffer->Memory;
  for(int Y = 0;
      Y < Buffer->Height;
      ++Y)
    {
      // (Row-wise iteration)
      uint32 *Pixel = (uint32*)Row; 
      for(int X = 0;
	  X < Buffer->Width;
	  ++X
	  )
	{
	  if(Y < OffsetY || Y > (Buffer->Height - OffsetY) || X < OffsetX || (X > Buffer->Width - OffsetX))
	    {
	      *Pixel++ = ((255 << 16) | (255 << 8) | 255);
		// Render black
	    }
	  else
	    {
	      ScaledX = (X - OffsetX) / WindowScale;
	      ScaledY = (Y - OffsetY) / WindowScale;
	      // Render color (+1 since we reserve 0 index for )
	      real32 PixelDensity = SimulationGrid.Density[IX(ScaledX + 1, ScaledY + 1)];
	      // (Max against 255)
	      uint8 Blue = (PixelDensity > 255) ? 255 : (uint8)PixelDensity;
	      // *Pixel++ = ((Green << 8) | Blue); // (pixel bytes are (blank)RGB, 4 bytes)
	      *Pixel++ = ((Blue << 16) | (Blue << 8) | Blue);
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

  Buffer->Pitch = Buffer->BytesPerPixel * Width;
  
}

internal void Win64DisplayBufferInWindow(HDC DeviceContext, int WindowWidth, int WindowHeight, win32_offscreen_buffer *Buffer)
{
  // TODO: aspect ratio correction
  // (WindowWidth, WindowHeight taken from GetWindowDimension())
  // (Buffer->Width, Buffer->Height currently fixed to GlobalWidth, GlobalHeight)

  /* 
  Buffer->Width = WindowWidth;
  Buffer->Height = WindowHeight;
  */


  // At this point, WindowWidth and WindowHeight should be = to Buffer->Width, Buffer->Height
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
    win32_window_dimension Dimension = GetWindowDimension(Window);
    Win64ResizeDIBSection(&GlobalBackBuffer, Dimension.Width, Dimension.Height);
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
  case WM_LBUTTONDOWN:
  {  } break;
  case WM_LBUTTONUP:
  {  } break;
  case WM_MOUSEMOVE:
  {
    if(WParam & MK_LBUTTON){ GlobalMouseDown = true; }
    else{ GlobalMouseDown = false; }
    
    PriorMouseX = GlobalMouseX; PriorMouseY = GlobalMouseY;
    GlobalMouseX = GET_X_LPARAM(LParam);
    GlobalMouseY = GET_Y_LPARAM(LParam);

    
    char MouseBuffer[256];
    sprintf(MouseBuffer, "Mouse: Current(X=%d, Y=%d) Prior(X=%d, Y=%d) Button:%s\n", 
            GlobalMouseX, GlobalMouseY, 
            PriorMouseX, PriorMouseY,
            GlobalMouseDown ? "DOWN" : "UP");
    OutputDebugStringA(MouseBuffer);
    
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

  Win64ResizeDIBSection(&GlobalBackBuffer, GlobalWidth, GlobalHeight);

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
				    "Fluid Sim v1",
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
	    SimulationRender(&GlobalBackBuffer);

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

	    /* 
	    char Buffer[256];
	    sprintf(Buffer, "ms / frame: %.02fms --- FPS: %.02ffps --- m-cycles / frame: %.02f\n", MSPerFrame, FPS, MCPF);
	    OutputDebugStringA(Buffer);
	    */
	    
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
