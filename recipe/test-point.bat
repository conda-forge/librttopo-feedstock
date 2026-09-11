@echo on
call %CC% /nologo /MD /I"%LIBRARY_INC%" test-point.c /link /LIBPATH:"%LIBRARY_LIB%" librrttopo_i.lib /OUT:test-point.exe
if errorlevel 1 exit /b 1
test-point.exe
if errorlevel 1 exit /b 1
