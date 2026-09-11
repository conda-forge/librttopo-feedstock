@echo on
call %CC% /nologo /MD /I"%LIBRARY_INC%" test-point.c /link /LIBPATH:"%LIBRARY_LIB%" librttopo.lib geos_c.lib /OUT:test-point.exe
if errorlevel 1 exit /b 1
test-point.exe
if errorlevel 1 exit /b 1
