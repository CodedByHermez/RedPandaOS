# =============================================================================
# RedPandaOS headless test harness
# =============================================================================
# Boots the disk image in QEMU with no window, optionally types text into the
# VM via QMP send-key, takes a screenshot, and shuts the VM down.
#
#   .\tools\qmp-test.ps1 -Screenshot build\screen.png
#   .\tools\qmp-test.ps1 -Type "help`nuptime`n" -Screenshot build\screen.png
# =============================================================================

param(
    [string]$Image      = (Join-Path $PSScriptRoot '..\build\RedPandaOS.img'),
    [string]$Type       = '',
    [string[]]$Mouse    = @(),   # e.g. 'move 40 0', 'down left', 'up left', 'sleep 200'
    [string]$Screenshot = (Join-Path $PSScriptRoot '..\build\screen.png'),
    [int]$BootWaitMs    = 1500,
    [int]$Port          = 4444
)

$ErrorActionPreference = 'Stop'
$qemu = 'C:\Program Files\qemu\qemu-system-i386.exe'

# Character -> QEMU qcode. Letters/digits map to themselves; everything else
# (and every shifted character) is listed here.
$qcodes = @{
    ' ' = 'spc';  "`n" = 'ret'; '-' = 'minus'; '=' = 'equal'
    '[' = 'bracket_left'; ']' = 'bracket_right'; ';' = 'semicolon'
    "'" = 'apostrophe'; '`' = 'grave_accent'; '\' = 'backslash'
    ',' = 'comma'; '.' = 'dot'; '/' = 'slash'
}
$shifted = @{
    '!' = '1'; '@' = '2'; '#' = '3'; '$' = '4'; '%' = '5'; '^' = '6'
    '&' = '7'; '*' = '8'; '(' = '9'; ')' = '0'; '_' = 'minus'; '+' = 'equal'
    '{' = 'bracket_left'; '}' = 'bracket_right'; ':' = 'semicolon'
    '"' = 'apostrophe'; '~' = 'grave_accent'; '|' = 'backslash'
    '<' = 'comma'; '>' = 'dot'; '?' = 'slash'
}

$proc = Start-Process -PassThru -FilePath $qemu -ArgumentList @(
    '-drive', "format=raw,file=$Image",
    '-display', 'none',
    '-qmp', "tcp:127.0.0.1:$Port,server,nowait"
)

try {
    Start-Sleep -Milliseconds $BootWaitMs

    $client = New-Object Net.Sockets.TcpClient('127.0.0.1', $Port)
    $stream = $client.GetStream()
    $reader = New-Object IO.StreamReader($stream)
    $writer = New-Object IO.StreamWriter($stream)
    $writer.AutoFlush = $true

    function Send-Qmp([string]$json) {
        $writer.WriteLine($json)
        return $reader.ReadLine()
    }

    $reader.ReadLine() | Out-Null                       # greeting banner
    Send-Qmp '{"execute":"qmp_capabilities"}' | Out-Null

    function Send-Char([string]$c) {
        $keys = @()
        if ($qcodes.ContainsKey($c))       { $keys = @($qcodes[$c]) }
        elseif ($shifted.ContainsKey($c))  { $keys = @('shift', $shifted[$c]) }
        elseif ($c -cmatch '[A-Z]')        { $keys = @('shift', $c.ToLower()) }
        elseif ($c -cmatch '[a-z0-9]')     { $keys = @($c) }
        else { Write-Warning "no qcode for '$c', skipped"; return }

        $keyObjs = ($keys | ForEach-Object { "{""type"":""qcode"",""data"":""$_""}" }) -join ','
        Send-Qmp "{""execute"":""send-key"",""arguments"":{""keys"":[$keyObjs]}}" | Out-Null
        Start-Sleep -Milliseconds 130   # send-key holds keys ~100ms
    }

    foreach ($ch in $Type.ToCharArray()) {
        Send-Char ([string]$ch)
    }

    foreach ($m in $Mouse) {
        $parts = $m -split '\s+'
        switch ($parts[0]) {
            'move' {
                $json = '{"execute":"input-send-event","arguments":{"events":[' +
                        '{"type":"rel","data":{"axis":"x","value":' + $parts[1] + '}},' +
                        '{"type":"rel","data":{"axis":"y","value":' + $parts[2] + '}}]}}'
                Send-Qmp $json | Out-Null
            }
            'down' {
                Send-Qmp ('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"' + $parts[1] + '"}}]}}') | Out-Null
            }
            'up' {
                Send-Qmp ('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"' + $parts[1] + '"}}]}}') | Out-Null
            }
            'key' {
                # 'key f12' or a combo 'key ctrl-s' (pressed together)
                $keyObjs = ($parts[1] -split '-' | ForEach-Object { "{""type"":""qcode"",""data"":""$_""}" }) -join ','
                Send-Qmp ('{"execute":"send-key","arguments":{"keys":[' + $keyObjs + ']}}') | Out-Null
            }
            'text' {
                $str = ($parts | Select-Object -Skip 1) -join ' '
                foreach ($ch in $str.ToCharArray()) { Send-Char ([string]$ch) }
            }
            'sleep' { Start-Sleep -Milliseconds ([int]$parts[1]) }
        }
        Start-Sleep -Milliseconds 40
    }

    if ($Type -or $Mouse) { Start-Sleep -Milliseconds 500 }   # let output settle

    $shotPath = ([IO.Path]::GetFullPath($Screenshot)) -replace '\\', '/'
    Send-Qmp "{""execute"":""screendump"",""arguments"":{""filename"":""$shotPath"",""format"":""png""}}" | Out-Null
    Start-Sleep -Milliseconds 300
    $client.Close()
    Write-Output "Screenshot: $shotPath"
}
finally {
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -Confirm:$false }
}
