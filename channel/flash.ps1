# Build and flash the bt-mixer channel board using ESP-IDF.
# Usage: .\flash.ps1 build
#        .\flash.ps1 -p COM3 build flash monitor

$env:IDF_PATH = "C:\esp\v5.5.3\esp-idf"
$env:IDF_TOOLS_PATH = "C:\Espressif\tools"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\tools\python\v5.5.3\venv"
$env:ESP_ROM_ELF_DIR = "C:\Espressif\tools\esp-rom-elfs\20241011"

$env:PATH = @(
    "C:\Espressif\tools\cmake\3.30.2\bin",
    "C:\Espressif\tools\ninja\1.12.1",
    "C:\Espressif\tools\idf-exe\1.0.3",
    "C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin",
    "C:\Espressif\tools\python\v5.5.3\venv\Scripts",
    $env:PATH
) -join ";"

& C:\Espressif\tools\python\v5.5.3\venv\Scripts\python.exe `
    C:\esp\v5.5.3\esp-idf\tools\idf.py @args
