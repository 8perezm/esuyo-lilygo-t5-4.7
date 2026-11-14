# Load the ESP-IDF 5.3 toolchain installed on this machine.
# Dot-source it:  . .\scripts\env.ps1
$env:PATH = 'C:\Users\miguel\AppData\Local\Programs\Python\Python313;' + $env:PATH
$env:IDF_PATH = 'C:\Users\miguel\esp\esp-idf'
$env:IDF_PYTHON_ENV_PATH = 'C:\Users\miguel\.espressif\python_env\idf5.3_py3.13_env'
. 'C:\Users\miguel\esp\esp-idf\export.ps1'
