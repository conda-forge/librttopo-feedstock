@echo on
call %CC% /nologo /MD /I"%LIBRARY_INC%" test-addline.c /link /LIBPATH:"%LIBRARY_LIB%" librttopo.lib geos_c.lib /OUT:test-addline.exe
if errorlevel 1 exit /b 1
test-addline.exe
if errorlevel 1 exit /b 1
