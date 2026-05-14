# APRS

APRS frames are AX.25 UI frames with PID `0xF0`. AXLoRaTNC decodes incoming APRS frames and logs the parsed content (position, message, status, weather) to the console.

## Position beacon

```text
beacon aprs 52.0167 4.7000 /> LoRa TNC on 869.480 MHz
```

Arguments: `lat lon [symbol-table+code] [comment]`. Default symbol is `/>` (car). This sets the destination to `APRS` and formats an uncompressed APRS position info field automatically.

Then enable the beacon:

```text
beacon interval 600    # every 10 minutes
beacon on
```

## Beacon commands

```text
beacon                         # show config
beacon on / off
beacon now                     # transmit immediately
beacon text <text>             # set info field manually
beacon dest <CALLSIGN-SSID>    # set destination (default CQ)
beacon interval <seconds>      # 10–86400 s
beacon path <CALL1,CALL2|off>  # set repeater path
```

## Digipeater

```text
digi on / off
digi mode ui        # relay only UI frames (default, for APRS/beacons)
digi mode all       # relay UI and connected-mode AX.25 frames
digialias WIDE1-1   # set secondary alias
digialias off
```

The digipeater matches the first unrepeated address against the node callsign or configured alias, sets the H-bit, recalculates FCS, and retransmits. A 30-second duplicate cache suppresses UI-frame loops.

## Mheard

```text
mheard          # list stations heard: uptime timestamp, RSSI, SNR, frame count, via flag
mheard clear    # reset the table
```

The table stores up to 20 entries with first-heard and last-heard times, RSSI, SNR, frame count, and whether the station was heard via a digipeater.
