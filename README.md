
***[WIP]: Isometric Navier-Stokes Based Rain Simulation***

Current Status: 
* Bug exists where diffusion will occur horizontally but not vertically. From runs through debugger I understand that the rendering, initial value population and sourcing are fine. Gauss-Seidel relaxation also works with 2D arrays, which leads me to believe this is a matter of indexing. (Though I've gone through indexing pretty thoroughly and it seems reasonable). From here I'm going to try with a 2D array and see if I encounter a similar issue, and then ideally find my way back to a contiguous version.


Steps: 
1. Direct (top-down) water simulation
2. Isometric translation for water simulation
3. Isometric rain simulation (particles + interaction with water simulation)


