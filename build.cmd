@echo off
REM Bootstrap for the wozzits build orchestrator (see tools/WzBuild).
REM   build.cmd build            build+install engine, publish editor -> install dir
REM   build.cmd build --config Release
REM   build.cmd test
dotnet run --project "%~dp0tools\WzBuild\WzBuild.csproj" -v quiet -- %*
exit /b %ERRORLEVEL%
