# OneWire Master Library for Arduino

This library implements an OneWire master purely using Arduino's C library
functions. Note that this means that this library does not implement timing
in the tightest possible way to avoid assembly usage but should be capable
to talk to conformant hardware. This libray does not support overdrive mode.

## What is the OneWire bus system

The one wire bus system is a simple master-slave based bus system that uses
in it's most extreme form only one wire for power and data (and of course
requires common ground between all bus members). It can use an additional
power pin of course. Most 1W devices are capable of operating at 3.3V or 5V,
depending on bus loading, topology and master hardware busses can reach ranges
in the kilometer range (this requires signal shaping, etc. from more
sophisticated masters than this code provides). With normal hardware data
rates up to 16.3 kbps can be achieved - of course this implementation never
reaches such data rates because of it's loose timing. Note that overdrive
support is not possible with this implementation and may not be possible
because of strict timing on AVRs generally.

The onewire bus is driven by the master and does not require clock
synchronization. Whenever the master wants to read a bit it pulls the
data line low for a specific amount of time (1 to 15 microseconds to write
a logic 1, 60 to 120 microseconds to write a logic 0). For reading the master
initiates the read slot by pulling the data line low 1-15 microseconds - if
the slave wants to respond with a logic 0 it pulls the line low for the entire
60-120 microsecond timeframe, else it does not react in any way except updating
its internal state maching.

One of the most common device seen is the DS18B20 temperature sensor

## How to use this library

### Initialization

Initialization is done by creating an instance of the ```InterfaceOnWire``` class.
Multiple classes can be instantiated for multiple ports. There are no special
requirements for the I/O pin - it is specified as Arduino pin number (that gets
translated via the ```portInputRegister```, ```digitalPinToPort``` and
the ```digitalPinToBitMask``` functions to access the port register directly).

There is a second argument to the constructor that allows specification of an
active pullup pin that can optionally be pulled whenever active pullup is
requested.

```
static InterfaceOneWire* wire1;
void setup() {
   wdt_disable();
   wire1 = new InterfaceOneWire(ONEWIRE_PIN, ~0);
}
```

### Bus reset

_Note_: Bus reset is __not__ required after each communication cycle is finished
and another device should be selected. This is handeled by the ROM routines
internally.

To reset the bus one can use the ```resetAndPresenceDetection()``` function.
This function returns ```true```whenever any device signaled presence during
the reset period or ```false``` if no device has signaled it's presence.

```
if(wire1->resetAndPresenceDetection()) {
	// Devices are present
} else {
	// No devices are present
}
```

### Rom search

_Note_: Bus reset is __not__ required after each communication cycle is finished
and another device should be selected. This is handeled by the ROM routines
internally.

One of the features the 1-Wire bus supports is automatic device enumeration.
One can decide if one wants to discover all devices or only the devices
that are currently in alert state. This is done via a callback mechanism:

```
static void discoveredRomId(uint8_t* lpRomID) {
   // Do something with the 8 byte long RomID
}

// Discovery function called anywhere

void doDiscovery() {
   /*
      Only perform bus discovery if there are any devices
      present on the bus. Else abort.
   */
   if(!wire1->resetAndPresenceDetection()) {
      // No devices present
      return;
   }

   /*
      If we want to discover ALL slaves we set the second
      parameter to false, if we want to discover only
      slaves in alert state, we would set it to true
   */
   wire1->discoverDevices(&discoveredRomId, false);
}
```

### Selecting device that is communicated with

_Note_: Bus reset is __not__ required after each communication cycle is finished
and another device should be selected. This is handeled by the ROM routines
internally.

Devices on the 1-Wire bus are identified via an 8 Byte ROM-ID. There are
three additional ROM commands (besides search commands) that can be used
to put a device into a mode in which it accepts communication.

* ```romCommand_ROMSingle()``` puts the only single device that is attached to
the bus into communication mode.
* ```romCommand_ROMSelect(romAddress)``` selects a specific device
* ```romCommand_ROMBroadcast()``` puts all devices into communicating mode. This
can be useful if you've only got a single device type (like temperature sensors)
on the network and want to trigger a single action (like starting a measurement
cycle) on all devices. Note that the commands that you send have to be
compatible with all attached devices.

```
// ...
 wire1->romCommand_ROMSingle();
 wire1->romCommand_ROMSelect(romAdress); // romAdress is uint8_t[8] (for example from discovery callback)
 wire1->romCommand_ROMBroadcast();
 // ...
 ```
### Reading and writing data

There are three groups of commands.

* Writing and reading single bits
* Writing and reading single bytes (groups 8 bits)
* Writing and reading a sequence of bytes (groups of n*8 bits)

WriteBit supports an additional parameter that keeps interrupts disabled
after the write (this is also used to implement immedate active pullup).

```
uint8_t readBit = wire1->readBit();
```

```
wire1->writeBit(0,  false);
```

```
wire1->writeBit(0,  true);
// Do something
interrupts();
```

The write byte(s) functions also support an additional last parameter that allows
one to trigger active pullup. In case on sets this parameter to true
and active pullup has been enabled during compilation an output pin
is pulled immediately after the write. One has to disable active pullup
after the desired time by ```activePullupDisable()``` to deassert the
data line. Note that no device should be in a state where it tries to
pull the bus low - in that case active pullup may damage devices on the
bus. If you don't disable active pullup and start a read or write cycle
the master may be damaged in case it's not capable of sinking all supplied
current and data transmission will not be working correctly.

```
wire1->writeByte(0x00, false);
```

```
wire1->writeByte(0x00, true);
delayMicroseconds(250);
wire1->activePullupDisable();
```

```
uint8_t message[3] = { 0x00, 0x01, 0x03 };

wire1->writeBytes(message, sizeof(message), false);
```

```
uint8_t message[3] = { 0x00, 0x01, 0x03 };

wire1->writeBytes(message, sizeof(message), true);
delayMicroseconds(250);
wire1->activePullupDisable();
```

```
uint8_t readByte = wire1->readByte();
```

```
uint8_t buffer[9];

wire1->readBytes(buffer, 9);
```

### CRC checking

Because there are many devices that implement CRC checksums following the
iButton standard there is an additional routine ```crc8CheckIButton```
that validates such an CRC:

```
uint8_t messageBuffer[9]; // 8 Byte message, 1 Byte CRC
if(wire1->crc8CheckIButton(messageBuffer, 8, crc8CheckIButton[8])) {
   // CRC check successful
}
```
