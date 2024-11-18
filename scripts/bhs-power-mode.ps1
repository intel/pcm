Write-Output "Intel(r) Performance Counter Monitor"
Write-Output "Birch Stream Power Mode Utility"
Write-Output ""

Write-Output " Options:"
Write-Output " --default                : set default power mode"
Write-Output " --latency-optimized-mode : set latency optimized mode"
Write-Output ""

# Run the pcm-tpmi command to determine I/O and compute dies
$output = pcm-tpmi 2 0x10 -d -b 26:26

# Parse the output to build lists of I/O and compute dies
$io_dies = @()
$compute_dies = @()
$die_types = @{}

$output -split "`n" | ForEach-Object {
    $line = $_
    if ($line -match "instance 0") {
        $die = $line -match 'entry (\d+)' | Out-Null; $matches[1]
        if ($line -match "value 1") {
            $die_types[$die] = "IO"
            $io_dies += $die
        } elseif ($line -match "value 0") {
            $die_types[$die] = "Compute"
            $compute_dies += $die
        }
    }
}

if ($args[0] -eq "--default") {
    Write-Output "Setting default mode..."

    foreach ($die in $io_dies) {
        # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 28:22 -w 8 

        # EFFICIENCY_LATENCY_CTRL_LOW_THRESHOLD (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 38:32 -w 13

        # EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 46:40 -w 120

        # EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD_ENABLE (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 39:39 -w 1
    }

    foreach ($die in $compute_dies) {
        # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore Compute)
        pcm-tpmi 2 0x18 -d -e $die -b 28:22 -w 12
    }
}

if ($args[0] -eq "--latency-optimized-mode") {
    Write-Output "Setting latency optimized mode..."

    foreach ($die in $io_dies) {
        # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 28:22 -w 0

        # EFFICIENCY_LATENCY_CTRL_LOW_THRESHOLD (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 38:32 -w 0

        # EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 46:40 -w 0

        # EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD_ENABLE (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 39:39 -w 1
    }

    foreach ($die in $compute_dies) {
        # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore Compute)
        pcm-tpmi 2 0x18 -d -e $die -b 28:22 -w 0
    }
}

Write-Output "Dumping TPMI Power control register states..."
Write-Output ""

# Function to extract and calculate metrics from the value
function ExtractAndPrintMetrics {
    param (
        [int]$value,
        [int]$socket_id,
        [int]$die
    )

    $die_type = $die_types[$die]

    # Extract bits and calculate metrics
    $min_ratio = ($value -shr 15) -band 0x7F
    $max_ratio = ($value -shr 8) -band 0x7F
    $eff_latency_ctrl_ratio = ($value -shr 22) -band 0x7F
    $eff_latency_ctrl_low_threshold = ($value -shr 32) -band 0x7F
    $eff_latency_ctrl_high_threshold = ($value -shr 40) -band 0x7F
    $eff_latency_ctrl_high_threshold_enable = ($value -shr 39) -band 0x1

    # Convert to MHz or percentage
    $min_ratio = $min_ratio * 100
    $max_ratio = $max_ratio * 100
    $eff_latency_ctrl_ratio = $eff_latency_ctrl_ratio * 100
    $eff_latency_ctrl_low_threshold = ($eff_latency_ctrl_low_threshold * 100) / 127
    $eff_latency_ctrl_high_threshold = ($eff_latency_ctrl_high_threshold * 100) / 127

    # Print metrics
    Write-Output "Socket ID: $socket_id, Die: $die, Type: $die_type"
    Write-Output "MIN_RATIO: $min_ratio MHz"
    Write-Output "MAX_RATIO: $max_ratio MHz"
    Write-Output "EFFICIENCY_LATENCY_CTRL_RATIO: $eff_latency_ctrl_ratio MHz"
    if ($die_type -eq "IO") {
        Write-Output "EFFICIENCY_LATENCY_CTRL_LOW_THRESHOLD: $eff_latency_ctrl_low_threshold%"
        Write-Output "EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD: $eff_latency_ctrl_high_threshold%"
        Write-Output "EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD_ENABLE: $eff_latency_ctrl_high_threshold_enable"
    }
    Write-Output ""
}

# Iterate over all dies and run pcm-tpmi for each to get the metrics
foreach ($die in $die_types.Keys) {
    $output = pcm-tpmi 2 0x18 -d -e $die

    # Parse the output and extract metrics for each socket
    $output -split "`n" | ForEach-Object {
        $line = $_
        if ($line -match "Read value") {
            $value = $line -match 'value (\d+)' | Out-Null; $matches[1]
            $socket_id = $line -match 'instance (\d+)' | Out-Null; $matches[1]
            ExtractAndPrintMetrics -value $value -socket_id $socket_id -die $die
        }
    }
}