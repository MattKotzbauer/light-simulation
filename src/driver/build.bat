@echo off

mkdir ..\..\bin
pushd ..\..\bin
cl -FC -Zi /std:c++17 ..\src\driver\driver.cpp user32.lib Gdi32.lib Ole32.lib Mmdevapi.lib 
popd

