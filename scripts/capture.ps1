param(
    [string]$Port = "COM12",
    [int]$Seconds = 30
)

# Capture the serial log for a fixed time. The CP210x is wired to EN/GPIO0, and
# pyserial asserts both lines when it opens the port, which puts the ESP32 into
# download mode. Deassert them and pulse reset so it boots the app.
$ErrorActionPreference = "Stop"
$py = "C:\Users\miguel\.espressif\python_env\idf5.3_py3.13_env\Scripts\python.exe"

$code = @'
import sys, time, serial

port = sys.argv[1]
seconds = int(sys.argv[2])
ser = serial.Serial(port, 115200, timeout=0.2)
ser.dtr = False          # GPIO0 high: run, not download
ser.rts = True           # EN low: reset
time.sleep(0.15)
ser.rts = False          # release reset
end = time.time() + seconds
while time.time() < end:
    line = ser.readline()
    if line:
        sys.stdout.write(line.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
'@

$tmp = Join-Path $env:TEMP "t5_capture.py"
Set-Content -Path $tmp -Value $code -Encoding utf8
& $py $tmp $Port $Seconds
