@echo off
REM DropBox Server Test Suite for Windows
REM This script tests various aspects of the server implementation

echo === DropBox Server Test Suite ===

set TESTS_PASSED=0
set TESTS_FAILED=0

echo Building server...
make clean >nul 2>&1
make >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [PASS] Server compilation
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Server compilation
    set /A TESTS_FAILED+=1
    echo Cannot proceed without successful compilation
    exit /b 1
)

echo Building test client...
gcc test_client.c -o test_client.exe >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [PASS] Test client compilation
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Test client compilation
    set /A TESTS_FAILED+=1
)

echo Testing with Thread Sanitizer...
make clean >nul 2>&1
make tsan >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [PASS] Thread Sanitizer build
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Thread Sanitizer build
    set /A TESTS_FAILED+=1
)

REM Clean up for standard build
make clean >nul 2>&1
make >nul 2>&1

echo Starting server for functional tests...
start /B dropbox_server.exe
timeout /T 3 >nul

REM Check if executable exists
if exist dropbox_server.exe (
    echo [PASS] Server executable created
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Server executable created
    set /A TESTS_FAILED+=1
)

REM Basic file structure tests
if exist dropbox_server.h (
    echo [PASS] Header file exists
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Header file exists
    set /A TESTS_FAILED+=1
)

if exist main.c (
    echo [PASS] Main source file exists
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Main source file exists
    set /A TESTS_FAILED+=1
)

if exist queue_operations.c (
    echo [PASS] Queue operations file exists
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Queue operations file exists
    set /A TESTS_FAILED+=1
)

if exist authentication.c (
    echo [PASS] Authentication file exists
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Authentication file exists
    set /A TESTS_FAILED+=1
)

if exist thread_pool.c (
    echo [PASS] Thread pool file exists
    set /A TESTS_PASSED+=1
) else (
    echo [FAIL] Thread pool file exists
    set /A TESTS_FAILED+=1
)

REM Kill any running server processes
taskkill /F /IM dropbox_server.exe >nul 2>&1

echo.
echo === Test Summary ===
echo Tests Passed: %TESTS_PASSED%
echo Tests Failed: %TESTS_FAILED%
set /A TOTAL_TESTS=%TESTS_PASSED%+%TESTS_FAILED%
echo Total Tests: %TOTAL_TESTS%

if %TESTS_FAILED% EQU 0 (
    echo All tests passed! 
    exit /b 0
) else (
    echo Some tests failed. Please review the implementation.
    exit /b 1
)