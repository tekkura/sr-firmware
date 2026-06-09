Our current setup is:
We have a phone connected to the firmware. On it, we run a full Round-Trip Time test and want to get accurate granular measurements. Currently, firmware does not record and store any time measurements.

Current measurement points for tests done via mock firmware:

| ID     | Junction Point            | Location                                             |
|:-------|:--------------------------|:-----------------------------------------------------|
| **T1** | Command Creation Start    | `SerialCommManager.setMotorLevels()`                 |
| **T2** | Writer Loop Wake-up       | `SerialCommManager.android2PiWriter` (after `wait`)  |
| **T3** | Transport Dispatch        | `UsbSerial.send(ByteArray)` (start)                  |
| **T4** | Simulator Receipt         | `MockRP2040.processPacket()` (start)                 |
| **T5** | Response Receipt at Phone | `UsbSerial.onNewData()` (start)                      |
| **T6** | Packet Queue Entry        | `UsbSerial.onCompletePacketReceived()` (end)         |
| **T7** | Manager Wake-up           | `SerialCommManager.receivePacket()` (after await)    |
| **T8** | State Applied             | `SerialCommManager.parseStatus()` (after publishers) |

New proposed measurements:

| ID     | Junction Point            | Location                                             |
|:-------|:--------------------------|:-----------------------------------------------------|
| **T1** | Command Creation Start    | `SerialCommManager.setMotorLevels()`                 |
| **T2** | Writer Loop Wake-up       | `SerialCommManager.android2PiWriter` (after `wait`)  |
| **T3** | Transport Dispatch        | `UsbSerial.send(ByteArray)` (start)                  |
| **T4** | Firmware Receipt          | `get_block` (after `END_MARKER` receive)             |
| **T5** | Firmware processing time  | `handle_packet` (before sending response)            |
| **T6** | Response Receipt at Phone | `UsbSerial.onNewData()` (start)                      |
| **T7** | Packet Queue Entry        | `UsbSerial.onCompletePacketReceived()` (end)         |
| **T8** | Manager Wake-up           | `SerialCommManager.receivePacket()` (after await)    |
| **T9** | State Applied             | `SerialCommManager.parseStatus()` (after publishers) |

We need to add collection of T4 and T5. It should be conditional, using LATENCY_BENCHMARK build option. For now, we don't want to do anything with the results, just store them in memory and update whenever a new packet is processed.