@echo off
set FilePath="./file"
set GenerateOutPath="./output_cs"

for  %%i in (%FilePath%/*.fb) do (
   @echo Generate: %%i
   flatc -n -o %GenerateOutPath% %FilePath%/%%i
)

for /d %%i in (%FilePath%/*) do (
   call:SubDirFun %%i
)
pause

:SubDirFun
@echo Sub File Path: %1
set CurSubDirPath=%1
for  %%i in (%FilePath%/%CurSubDirPath%/*.fb) do (
   @echo Generate: %%i
   flatc -n -o %GenerateOutPath%/%CurSubDirPath% %FilePath%/%CurSubDirPath%/%%i
)
