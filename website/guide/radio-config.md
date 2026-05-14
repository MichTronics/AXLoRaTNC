# Radio config

All radio parameters are set from the serial console and persisted in NVS.

## Show current config

```text
radio
```

Displays frequency, bandwidth, SF, CR, TX power, and live RSSI/SNR.

## Set parameters

```text
radio freq 869.480    # MHz
radio bw   125        # kHz — valid: 7.8 10.4 15.6 20.8 31.25 41.7 62.5 125 250 500
radio sf   7          # spreading factor 6–12
radio cr   5          # coding rate denominator: 5=4/5  6=4/6  7=4/7  8=4/8
radio power 22        # dBm
radio reset           # restore variant defaults
```

Changes are applied immediately and written to NVS.

## Quick profiles

```text
profile fast      # txdelay=0 p=255 slot=1 fulldup=0 duty=off   (lab/bench only)
profile normal    # txdelay=30 p=63 slot=10 fulldup=0 duty=on
```

::: warning
`profile fast` disables the duty-cycle guard. Use it only on a dummy load or shielded lab setup. On 869 MHz in EU/NL the duty-cycle guard must remain enabled during normal operation.
:::

## Duty-cycle guard

```text
duty          # show current state
duty on       # enable (default)
duty off      # disable (lab only)
duty 10       # set 10 % limit
duty 1        # set 1 % limit
duty 0.1      # set 0.1 % limit
```

## KISS timing parameters

KISS TxDelay, Persistence, SlotTime, and FullDuplex are set by the host over the KISS protocol and drive the p-persistent CSMA algorithm.

| Parameter | Default | Description |
|---|---|---|
| TxDelay | 30 (×10 ms = 300 ms) | Key-up delay before TX |
| Persistence | 63 | Probability: (p+1)/256 |
| SlotTime | 10 (×10 ms = 100 ms) | CSMA slot interval |
| FullDuplex | 0 | 1 = skip CSMA listen |
