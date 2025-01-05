#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>


/*
Overall Todos:
* Move simulation functions into platform-independent header / cpp files
  
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

const int32 SimulationHeight = 180;
const int32 SimulationWidth = 320;

// (Macro for accessing simulation indices)
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
global_variable WINDOWPLACEMENT g_wpPrev = { sizeof(g_wpPrev) };

struct MouseState{
  bool IsDown = false;
  int32 X;
  int32 Y;
  int32 PriorX;
  int32 PriorY;
};

global_variable MouseState GlobalMouse = {};
  
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

internal void NSSimulationInit(){
  int32 ArrayCount = sizeof(SimulationArrays) / sizeof(SimulationArrays[0]);
  int32 ArraySize = sizeof(real32) * (SimulationWidth + 2) * (SimulationHeight + 2);

  for(int ArrayIndex = 0; ArrayIndex < ArrayCount; ++ArrayIndex){
    *SimulationArrays[ArrayIndex] = (real32*)VirtualAlloc(0, ArraySize, MEM_COMMIT, PAGE_READWRITE);
  }
  
  // (Initializing members to 0)
  for(int ArrayIndex = 0; ArrayIndex < ArrayCount; ++ArrayIndex){
    for(int i = 0; i <= SimulationWidth + 1; ++i){
      for(int j = 0; j <= SimulationHeight + 1; ++j){
	(*SimulationArrays[ArrayIndex])[IX(i,j)] = 0;
      }      
    }
  }
}


// Update priors to hold current values
internal void NSUpdate(real32* &PriorValues, real32* &CurrentValues){
  real32* SwapPointer = PriorValues;
  PriorValues = CurrentValues;
  CurrentValues = SwapPointer; 
}
  
// Sourcing step: add source to desired array
internal void NSAddSource(real32* Destination, real32* Source){
  for(int i = 1; i <= SimulationWidth; ++i){
    for(int j = 1; j <= SimulationHeight; ++j){
      Destination[IX(i,j)] += Source[IX(i,j)] * GlobalBaseDeltaTime;
    }
  }
}

// Bounding step: account for walls and corners
internal void NSBound(int Mode, real32* Array){
  // Walls:
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
  
  // Corners:
  Array[IX(0,0)] = 0.5f * (Array[IX(0,1)] + Array[IX(1,0)]);
  Array[IX(0,SimulationHeight+1)] = 0.5f * (Array[IX(0,SimulationHeight)] + Array[IX(1,SimulationHeight+1)]);
  Array[IX(SimulationWidth+1,0)] = 0.5f * (Array[IX(SimulationWidth,0)] + Array[IX(SimulationWidth+1,1)]);
  Array[IX(SimulationWidth+1,SimulationHeight+1)] = 0.5f * (Array[IX(SimulationWidth+1,SimulationHeight)] +
							    Array[IX(SimulationWidth,SimulationHeight+1)]);
  
} 


// Diffusion step: simulating diffusion of density / velocity 
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

// Advection step: follow defined velocities
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

// Projection step (for velocity):
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

// Draw simulation content into visual buffer
internal void DisplaySimulation(win32_offscreen_buffer *Buffer)
{
  // WindowScale = min(Buffer->Width / SimulationWidth, Buffer->Height / SimulationHeight)
  int32 WindowScale = (Buffer->Width / SimulationWidth) < (Buffer->Height / SimulationHeight) ? 
    Buffer->Width / SimulationWidth : Buffer->Height / SimulationHeight;

  // Total pixels occupied by simulation
  int32 ScaledWidth =  WindowScale * SimulationWidth, ScaledHeight = WindowScale * SimulationHeight;

  // Offsets for simulation window within client window: 
  // OffsetX = max(0.5f * (Buffer->Width - ScaledWidth), 0)
  int32 OffsetX = Buffer->Width > ScaledWidth ?
    (Buffer->Width - ScaledWidth) / 2 : 0;
  int32 OffsetY = Buffer->Height > ScaledHeight ?
    (Buffer->Height - ScaledHeight) / 2 : 0;

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
	  if(Y < OffsetY || Y >= OffsetY + ScaledHeight || X < OffsetX || X >= OffsetX + ScaledWidth)
	    {
	      // (Render black)
	      *Pixel++ = 0;
	    }
	  else
	    {
	      // (Render color)
	      ScaledX = (X - OffsetX) / WindowScale;
	      ScaledY = (Y - OffsetY) / WindowScale;
	      // Render color (+1 since we reserve 0 index for boundaries)
	      real32 PixelDensity = SimulationGrid.Density[IX(ScaledX + 1, ScaledY + 1)];
	      // (Max against 255)
	      uint8 Blue = (PixelDensity > 255) ? 255 : (uint8)PixelDensity;
	      // *Pixel++ = ((Green << 8) | Blue);
	      // (Greyscale)
	      *Pixel++ = ((Blue << 16) | (Blue << 8) | Blue);
	    }
	  
	}

      Row += Buffer->Pitch;
    }
  
}

internal void WindowToSimulationCoords(int32 WindowX, int32 WindowY, int32 WindowWidth, int32 WindowHeight, int32* SimX, int32* SimY){

  // WindowScale = min(WindowWidth / SimulationWidth, WindowHeight / SimulationHeight)
  int32 WindowScale = (WindowWidth / SimulationWidth) < (WindowHeight / SimulationHeight) ?
    WindowWidth / SimulationWidth : WindowHeight / SimulationHeight;

  // (Pixel width and height of simulation display)
  int32 ScaledWidth = WindowScale * SimulationWidth;
  int32 ScaledHeight = WindowScale * SimulationHeight;

  int32 OffsetX = WindowWidth > ScaledWidth ? (WindowWidth - ScaledWidth) / 2 : 0;
  int32 OffsetY = WindowHeight > ScaledHeight ? (WindowHeight - ScaledHeight) / 2 : 0;

  *SimX = ((WindowX - OffsetX) / WindowScale) + 1;
  *SimY = ((WindowY - OffsetY) / WindowScale) + 1;

  *SimX = (*SimX < 1) ? 1 : ((*SimX > SimulationWidth) ? SimulationWidth : *SimX);
  *SimY = (*SimY < 1) ? 1 : ((*SimY > SimulationHeight) ? SimulationHeight : *SimY);
  
}


internal void HandleMouseInput(win32_window_dimension Dimension){
  // (is there a better way to pass in the current dimension)
  if(GlobalMouse.IsDown &&
     GlobalMouse.X >= 0 && GlobalMouse.X < Dimension.Width &&
     GlobalMouse.Y >= 0 && GlobalMouse.Y < Dimension.Height){
    int32 SimX, SimY;
    WindowToSimulationCoords(GlobalMouse.X, GlobalMouse.Y, Dimension.Width, Dimension.Height, &SimX, &SimY);
    // (Starting with 1-pixel density)
    SimulationGrid.DensitySources[IX(SimX, SimY)] = 100.0f;
    if(GlobalMouse.X != GlobalMouse.PriorX || GlobalMouse.Y != GlobalMouse.PriorY){
      // (Handle velocity computing)
    }
    
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
    case VK_ESCAPE: {
      GlobalRunning = false;
    } break;
    case VK_F4: {
      if ((LParam & (1 << 29)) != 0){ GlobalRunning = false; }
    } break;
    case VK_F11: {
      if(IsDown && !WasDown){
      
      DWORD dwStyle = GetWindowLong(Window, GWL_STYLE);
      if(dwStyle & WS_OVERLAPPEDWINDOW){
	
        MONITORINFO mi = {sizeof(mi)};

	// Try to (1) save current window placement in g_wpPrev, and (2) get monitor info for monitor containing window
	if(GetWindowPlacement(Window, &g_wpPrev) &&
	   GetMonitorInfo(MonitorFromWindow(Window, MONITOR_DEFAULTTOPRIMARY), &mi)){

	  // Remove overlapped window style
	  SetWindowLong(Window, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);

	  // Set window to span entire monitor
	  SetWindowPos(Window, HWND_TOP,
		       mi.rcMonitor.left, mi.rcMonitor.top,
		       mi.rcMonitor.right - mi.rcMonitor.left,
		       mi.rcMonitor.bottom - mi.rcMonitor.top,
		       SWP_NOOWNERZORDER | SWP_FRAMECHANGED
		       );
	  }
      }
      else{
	SetWindowLong(Window, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
	SetWindowPlacement(Window, &g_wpPrev);
	SetWindowPos(Window, NULL, 0, 0, 0, 0,
		     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER  | SWP_FRAMECHANGED);
      }} 
    } break;
    }
  } break;
  case WM_PAINT:
  {
    PAINTSTRUCT Paint;
    HDC DeviceContext = BeginPaint(Window, &Paint);

    win32_window_dimension Dimension = GetWindowDimension(Window);
    Win64DisplayBufferInWindow(DeviceContext, Dimension.Width, Dimension.Height, &GlobalBackBuffer);

    EndPaint(Window, &Paint);
 
  } break;
  case WM_NCLBUTTONDBLCLK:
  {
    // TODO: fullscreen to device resolution without removing WS_OVERLAPPEDWINDOW
    SendMessage(Window, WM_KEYDOWN, VK_F11, 0);
  } break;
  case WM_MOUSEMOVE:
  {
    // Handle mouse movement
    if(WParam & MK_LBUTTON){ GlobalMouse.IsDown = true; }
    else{ GlobalMouse.IsDown = false; }
    
    GlobalMouse.PriorX = GlobalMouse.X; GlobalMouse.PriorY = GlobalMouse.Y;
    GlobalMouse.X = GET_X_LPARAM(LParam);
    GlobalMouse.Y = GET_Y_LPARAM(LParam);

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
  WNDCLASSA WindowClass = {};

  Win64ResizeDIBSection(&GlobalBackBuffer, 1280, 720);

  NSSimulationInit();
  
  WindowClass.style = CS_HREDRAW|CS_VREDRAW;
  WindowClass.lpfnWndProc = Win64MainWindowCallback;
  WindowClass.hInstance = Instance; 
  WindowClass.lpszClassName = "SimulationWindow"; 

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
	  GlobalRunning = true;

	  HDC DeviceContext = GetDC(Window);

	  /* Performance tracking: 
	  LARGE_INTEGER LastCounter;
	  QueryPerformanceCounter(&LastCounter);
	  int64 LastCycleCount =  __rdtsc();	  
	  */
	  
	  while(GlobalRunning){
	 
	    MSG Message;
	    while(PeekMessageA(&Message, 0, 0, 0, PM_REMOVE))
	      {
		if(Message.message == WM_QUIT)
		  {
		    GlobalRunning = false;
		  }

		TranslateMessage(&Message);
		DispatchMessage(&Message); 
	      }

	    // Run and draw simulation
	    SimulationDriver();
	    DisplaySimulation(&GlobalBackBuffer);

	    // Update window dimensions
	    win32_window_dimension Dimension = GetWindowDimension(Window);
	    Win64DisplayBufferInWindow(DeviceContext, Dimension.Width, Dimension.Height, &GlobalBackBuffer);

	    HandleMouseInput(Dimension);
	    
	    /* Performance tracking: 
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
	    */
	    
	    // (end of while(GlobalRunning loop)
	  }
	}
    }
  return(0);
}
