@echo off
REM ============================================================
REM  iniciar_windows.bat
REM  Levanta el dashboard (backend FastAPI) en Windows.
REM
REM  Uso:
REM    iniciar_windows.bat            -> deteccion automatica del puerto
REM    iniciar_windows.bat COM3       -> fuerza el puerto COM3
REM
REM  Requiere haber creado el venv y las dependencias una vez:
REM    python -m venv venv
REM    venv\Scripts\activate
REM    python -m pip install -r requirements.txt
REM ============================================================

setlocal
cd /d "%~dp0"

if not exist "venv\Scripts\activate.bat" (
    echo [ERROR] No existe el entorno virtual "venv".
    echo Crearlo una sola vez con:
    echo     python -m venv venv
    echo     venv\Scripts\activate
    echo     python -m pip install -r requirements.txt
    pause
    exit /b 1
)

call "venv\Scripts\activate.bat"

if not "%~1"=="" (
    echo [info] Puerto forzado: %~1
    set PUERTO_FORZADO=%~1
) else (
    echo [info] Deteccion automatica del Arduino por VID.
)

echo.
echo [info] Abriendo el dashboard en http://localhost:8000
echo [info] Para detenerlo: Ctrl+C
echo.

REM Abrir el navegador un instante despues de que arranque el server
start "" cmd /c "timeout /t 3 >nul & start http://localhost:8000"

python -m uvicorn main:app --host 0.0.0.0 --port 8000
