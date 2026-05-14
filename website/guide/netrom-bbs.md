# NET/ROM & BBS

## NET/ROM node

```text
node                          # show config
node on / off
node alias AXLORA             # NET/ROM alias (up to 6 chars)
node ident <text>             # node identification string
node interval <seconds>       # NODES broadcast interval (60–86400 s)
node broadcast                # send NODES broadcast now
nodes                         # show learned route table
routes                        # same as nodes
```

Routes expire automatically after `interval × 6` seconds (NET/ROM obsolescence rule). The route table holds up to 20 entries.

## Node shell

When a station connects over AX.25 connected mode, the node shell answers:

```text
?         # command list
INFO      # node identification
NODES     # known NET/ROM routes
ROUTES    # same
MHEARD    # heard station table
BBS       # enter mailbox shell
BYE / B   # disconnect
```

## Mailbox (BBS)

From the node shell, type `BBS` to enter the mailbox.

```text
L         # list all messages (number, timestamp, from, to, status)
LT        # list messages addressed to you
R <n>     # read message n
S <CALL>  # compose a message to CALL (end with an empty line)
K <n>     # delete message n (only your own callsign)
X / EXIT  # back to node shell
B / BYE   # disconnect
```

Messages are stored in NVS: up to **12 messages**, **200 characters** each, with sender, recipient, body, read flag, and uptime timestamp.

### Sysop access (local console)

```text
bbs        # list all messages regardless of recipient
```

## AX.25 connected mode commands

```text
connect N0CALL-1           # connect on channel 1
connect 2 N0CALL-2         # connect on channel 2 (1–8)
disconnect                 # disconnect channel 1
disconnect 2               # disconnect channel 2
send hello                 # send on channel 1
send 2 hello               # send on channel 2
sendui CQ hello world      # send UI frame
ax25                       # show status of all active channels
stats                      # full statistics
```
