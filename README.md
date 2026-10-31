# ModbusSniffer_RTUOverTCP
Plugin for LogisDom for sniffing Modbus protocol ran via TCP with RTU device (RS232/485 to Ethernet tranceiver)

The plugin is passive: it never sends anything on the bus. It listens to the traffic between an
existing Modbus master and its slaves, decodes every request/response pair and exposes the
register values to LogisDom as devices.

```
 Modbus master ──┐
                 ├── RS485 bus ── RS485/Ethernet converter ── TCP ── LogisDom + ModbusSniffer
 Modbus slave(s) ┘
```

## How it works

Once the RTU frames go through the Ethernet converter, the inter-frame silences (3.5 characters)
that normally delimit Modbus RTU frames are lost. The plugin therefore rebuilds the frames from the
raw byte stream only:

- **CRC resynchronisation**: at each position of the stream, the expected frame length is computed
  from the function code, and the candidate is accepted only if its CRC16 is valid. Otherwise one
  byte is dropped and the search goes on.
- **Request/response pairing**: a frame is taken as the response only if it comes from the slave of
  the pending request and carries the same function code (or its exception code). If no response
  comes (silent slave, broadcast, lost bytes), the next master request is recognised directly.
- **Consistency checks**: every decoded field is bounded by the real frame size, and a response
  whose size does not match its request is ignored, so a random frame that passes the CRC by chance
  cannot create fake register values.

Supported function codes:

| Code | Function | Table |
|------|----------|-------|
| 0x01 | Read coils | Lecture |
| 0x02 | Read discrete inputs | Lecture |
| 0x03 | Read holding registers | Lecture |
| 0x04 | Read input registers | Lecture |
| 0x05 | Write single coil | Ecriture |
| 0x06 | Write single register | Ecriture |
| 0x0F | Write multiple coils | Ecriture |
| 0x10 | Write multiple registers | Ecriture |

Exception responses are recognised and skipped.

## User interface

- **IP Adress / Port**: address of the RS485/Ethernet converter (default port 1470). Changing them
  restarts the connection.
- **Start / Stop**: start or stop listening. Listening starts automatically when the configuration
  is loaded.
- **Lecture**: every register/coil seen in a read response, with its value (decimal and hex) and the
  time of the last update. Select lines and click *Add* to create LogisDom devices from them.
- **Ecriture**: every register/coil written by the master.
- **Registres**: the LogisDom devices (slave, address, type, resolution, max value). Each device
  receives the value of its register every time the master reads it.
- **Log**: connection events and raw received bytes (when *Log* is checked).

## Connection

The TCP connection runs in its own thread (`TcpWorker`), without any blocking wait:

- a reconnection attempt every 3 s while disconnected,
- a connection attempt still pending after 3 s is aborted and retried,
- if no data is received for 60 s, the connection is closed and opened again,
- the thread is stopped cleanly when the plugin is closed.

## Files

| File | Content |
|------|---------|
| `modbus_rtu_core.hpp` | Header-only decoding engine, no Qt dependency: CRC, frame extraction, decoding, request/response pairing |
| `tcpworker.h` | TCP connection worker, feeds the engine and emits the register updates |
| `ModbusSnifferPlugin.h/.cpp` | LogisDom plugin and user interface |
| `ModbusSniffer.ui` | Qt Designer form |
| `interface.h` | Copy of the LogisDom plugin interface (the build uses `../interface.h`) |

## Build

Qt 6 (Core, Gui, Network, Widgets, Xml), C++17.

With qmake, using the LogisDom development tree:

```
qmake ModbusSniffer_RtuOverTCP_Plugin_Dev.pro
make
```

The plugin is written to the `DESTDIR` set in the `.pro` file. After adding or removing a header
containing `Q_OBJECT`, run qmake again so that moc is generated for it.

With CMake:

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6>
cmake --build build
```

Both builds expect `../interface.h` and `../common.h` from the LogisDom plugins directory.

## License

GPL-3.0, see [LICENSE](LICENSE).
