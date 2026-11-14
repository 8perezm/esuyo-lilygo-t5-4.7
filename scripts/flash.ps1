param(
    [string]$Port = "COM12"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\env.ps1"

Push-Location "$PSScriptRoot\.."
try {
    idf.py -p $Port -b 460800 flash
} finally {
    Pop-Location
}
