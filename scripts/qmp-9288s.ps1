param(
    [ValidateSet("key", "touch", "shot")]
    [string]$Action,
    [string]$Key = "ret",
    [int]$X = 80,
    [int]$Y = 120,
    [string]$Output = "build/bbk9588/9288s-live.png",
    [int]$Port = 6682
)

$ErrorActionPreference = "Stop"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Invoke-Qmp {
    param([hashtable]$Command)

    $client = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $Port)
    try {
        $stream = $client.GetStream()
        $reader = [System.IO.StreamReader]::new(
            $stream, $utf8NoBom, $false, 1024, $true)
        $writer = [System.IO.StreamWriter]::new(
            $stream, $utf8NoBom, 1024, $true)
        $writer.NewLine = "`r`n"
        $writer.AutoFlush = $true

        $null = $reader.ReadLine()
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        $null = $reader.ReadLine()
        $writer.WriteLine(($Command | ConvertTo-Json -Compress -Depth 8))
        return $reader.ReadLine() | ConvertFrom-Json
    }
    finally {
        $client.Dispose()
    }
}

switch ($Action) {
    "key" {
        Invoke-Qmp @{
            execute = "input-send-event"
            arguments = @{
                events = @(
                    @{
                        type = "key"
                        data = @{
                            down = $true
                            key = @{ type = "qcode"; data = $Key }
                        }
                    }
                )
            }
        } | Out-Null
        Start-Sleep -Milliseconds 120
        Invoke-Qmp @{
            execute = "input-send-event"
            arguments = @{
                events = @(
                    @{
                        type = "key"
                        data = @{
                            down = $false
                            key = @{ type = "qcode"; data = $Key }
                        }
                    }
                )
            }
        }
    }
    "touch" {
        if ($X -lt 0 -or $X -gt 159 -or $Y -lt 0 -or $Y -gt 239) {
            throw "Touch coordinates must fit the 160x240 9288S display."
        }
        $absX = [int][Math]::Round($X * 32767.0 / 159.0)
        $absY = [int][Math]::Round($Y * 32767.0 / 239.0)
        Invoke-Qmp @{
            execute = "input-send-event"
            arguments = @{
                events = @(
                    @{ type = "abs"; data = @{ axis = "x"; value = $absX } }
                    @{ type = "abs"; data = @{ axis = "y"; value = $absY } }
                    @{
                        type = "btn"
                        data = @{ button = "left"; down = $true }
                    }
                )
            }
        } | Out-Null
        Start-Sleep -Milliseconds 120
        Invoke-Qmp @{
            execute = "input-send-event"
            arguments = @{
                events = @(
                    @{
                        type = "btn"
                        data = @{ button = "left"; down = $false }
                    }
                )
            }
        }
    }
    "shot" {
        $absoluteOutput = [System.IO.Path]::GetFullPath(
            (Join-Path (Get-Location) $Output))
        $outputDirectory = Split-Path -Parent $absoluteOutput
        if (-not (Test-Path -LiteralPath $outputDirectory)) {
            New-Item -ItemType Directory -Path $outputDirectory | Out-Null
        }
        $ppmPath = [System.IO.Path]::ChangeExtension($absoluteOutput, ".ppm")
        Invoke-Qmp @{
            execute = "screendump"
            arguments = @{ filename = $ppmPath.Replace("\", "/") }
        } | Out-Null
        & ffmpeg -y -loglevel error -i $ppmPath $absoluteOutput
        Get-Item -LiteralPath $absoluteOutput
    }
}
