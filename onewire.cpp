/*
	1-wire master implementation using bit-banging
	on any arbitrary pin. This library has been
	tested on arduino pro mini (16 MHz; 3.3v and 5v)

	It provides basic access to the 1wire bus system
	as well as optional active pullup driving via an
	external FET as well as enumeration / search
	support.

	The following conditionals can be set:
		ONEWIRE_SUPPORT_ENUMERATION
			Enables support for 1-wire bus search
			via discoverDevices function

		ONEWIRE_ACTIVE_PULLUP
			If defined, active pullup via an external
			FET is supported after write cycles. If
			used the application has to disable active
			pullup prior to next use of the bus.
*/

#include <stdint.h>

#include "./onewire.h"

/*
	Initialize one wire interface. We start with our pin mode
	set to input (idle) and calculate port offset and pin mask
	for 1-wire I/O pin and active pullup pin (if support is
	enabled).
*/
InterfaceOneWire::InterfaceOneWire(uint8_t ioPin, uint8_t activePullupPin) {
	this->ioRegister 		= portInputRegister(digitalPinToPort(ioPin));
	this->ioRegisterMask 	= digitalPinToBitMask(ioPin);
	#ifdef ONEWIRE_ACTIVE_PULLUP
		if(activePullupPin != ~0) {
			this->pullupRegister 		= portInputRegister(digitalPinToPort(activePullupPin));
			this->pullupRegisterMask 	= digitalPinToBitMask(activePullupPin);
		} else {
			this->pullupRegister		= ~0;
			this->pullupRegisterMask	= 0;
		}
	#endif

	/* Setup pins */
	pinHigh();
	pinModeInput();

	if(activePullupPin != ~0) {
		#ifdef ONEWIRE_ACTIVE_PULLUP
			pullupInitialize();
		#endif
	}
}

/*
	On destruction we set pin mode to input and
	disable active pullup (if supported)
*/
InterfaceOneWire::~InterfaceOneWire() {
	#ifdef ONEWIRE_ACTIVE_PULLUP
		if(this->pullupRegister != ~0) {
			pullupShutdown();
		}
	#endif
	pinModeInput();
}

/*
	Perform a reset pulse and detect if any devices are present
	on the 1-wire bus.

	The reset sequence is performed by:
		480 us < 10T < 640 us		Pull bus LOW
		15 us < T < 60 us			Let bus recover & wait for devices pulling the data line low (Set pin to input)
		0 us < T < 60 us			Devices pull the bus low. If any device is present, it sets "low"
		240 us						Let bus recovery & parasitic capacitors recharge ...
*/
bool InterfaceOneWire::resetAndPresenceDetection() {
	uint8_t retryCount;
	uint8_t result;

	noInterrupts();

	/*
		First wait till our line reaches high (idle) - just
		in case it has not settled till now ...

		We wait for at least ONEWIRE_RETRY_RESETWAITHIGH*5 microseconds.
		If the line has not reached high state till then we abort
		and report no found devices. This may be caused by a missing
		or defect pullup, a short circuit, etc.
	*/
	pinModeInput();
	retryCount = ONEWIRE_RETRY_RESETWAITHIGH;
	do {
		if((retryCount = retryCount - 1) == 0) {
			return false;
		}

		delayMicroseconds(5);
	} while(pinRead() == 0);

	/*
		Pull line low for 480us, the minimum amount of time
		(the delay of function calls will lead to a slightly larger time)
	*/
	pinLow();
	pinModeOutput();
	interrupts(); 							/* Allow interrupts during wait, the delay is not so critical; Just ensure ISRs
											   will take less than 160 us to complete or disable release during wait here */
	delayMicroseconds(480);
	/* Now try to detect if any device set's the presence pulse ... */
	noInterrupts();
	pinModeInput();
	delayMicroseconds(60); 					/* Wait for the devices to set response; Devices take 15-60 us to assert the
											   line for another 60-240 us (i.e. between 75 us and 300 us is  the "end") */
	result = pinRead();
	interrupts(); 							/* Allow interrupts during second wait. Timing is nearly irrelevant if extended ... */
	delayMicroseconds(420);

	return (result == 0) ? true : false; 	/* If the line has been pulled to low -> we have found devices on the bus */
}

/*
	Write a single byte.

	If pullup is set to true either the data line is kept high instead of tristate if active
	pullup is not compiled OR active pullup is set. In this case interrupts stay disabled!
	The application has to call
		activePullupDisable();
	after the active pullup period. This has to happen BEFORE any device tries a pulldown
	(this would damage the device by overcurrent).
*/
void InterfaceOneWire::writeByte(uint8_t byte, bool pullup) {
	uint8_t mask = 0x01;
	do {
		writeBit(byte & mask, (mask == 0x80) ? pullup : false);
		mask = mask << 1;
	} while(mask != 0x00);

	if(pullup) {
		#ifdef ONEWIRE_ACTIVE_PULLUP
			if(this->pullupRegister != ~0) {
				pinModeInput();
				pullupEnable();
			} else {
				pinHigh();
				pinModeOutput();
			}
		#else
			pinHigh();
			pinModeOutput();
		#endif
	} else {
		pinModeInput();
	}
}
/*
	Write multiple bytes

	If pullup is set to true either the data line is kept high instead of tristate if active
	pullup is not compiled OR active pullup is set. In this case interrupts stay disabled!
	The application has to call
		activePullupDisable();
	after the active pullup period. This has to happen BEFORE any device tries a pulldown
	(this would damage the device by overcurrent).
*/
void InterfaceOneWire::writeBytes(uint8_t* bytes, unsigned int length, bool pullup) {
	unsigned int i;
	for(i = 0; i < length; i=i+1) {
		writeByte(bytes[i], pullup);
	}
}
/*
	Disable active pullup and re-enable interrupts.
*/
void InterfaceOneWire::activePullupDisable() {
	#ifdef ONEWIRE_ACTIVE_PULLUP
		if(this->pullupRegister != ~0) {
			pullupDisable();
		} else {
			pinModeInput();
		}
	#else
		pinModeInput();
	#endif
	interrupts();
}

/*
	Read one byte by repeatedly reading bits via read sequences.
*/
uint8_t InterfaceOneWire::readByte() {
	// unsigned int i;
	uint8_t res = 0x00;
	uint8_t mask = 0x01;

	do {
		if(readBit() != 0) {
			res = res | mask;
		}
		mask = mask << 1;
	} while(mask != 0);
	return res;
}
/*
	Read multiple bytes
*/
void InterfaceOneWire::readBytes(uint8_t* bytes, unsigned int length) {
	unsigned int i;
	for(i = 0; i < length; i=i+1) {
		bytes[i] = readByte();
	}
}

/*
	Select the only existing ROM on the bus.
*/
void InterfaceOneWire::romCommand_ROMSingle() {
	resetAndPresenceDetection();
	writeByte(0x33, false);							// Read ROM command
}
/*
	Select a specific ROM on the bus
*/
void InterfaceOneWire::romCommand_ROMSelect(uint8_t* romAdress) {
	resetAndPresenceDetection();
	writeByte(0x55, false);							// Match ROM command
	writeBytes(romAdress, 8, false);
}
/*
	Broadcast next transmissions to all ROMs on the bus.
*/
void InterfaceOneWire::romCommand_ROMBroadcast() {
	resetAndPresenceDetection();
	writeByte(0xCC, false);							// Skip ROM command
}

#ifdef ONEWIRE_SUPPORT_OVERDRIVE
	void InterfaceOneWire::romCommand_ROMSingleOverdrive() {
		resetAndPresenceDetection();
		writeByte(0x3C, false);						// Skip ROM overdrive (Switch to overdrive mode afterwards!)
	}
	void InterfaceOneWire::romCommand_ROMSelectOverdrive(uint8_* romAdress) {
		resetAndPresenceDetection();
		writeByte(0x69, false);						// Match ROM overdrive (Switch to overdrive mode afterwards!)
		writeBytes(romAddress, 8, false);
	}
#endif

static uint8_t crcUpdate8(uint8_t crc, uint8_t data) {
	uint8_t i;
	crc = crc ^ data;
	for (i = 0; i < 8; i=i+1) {
		if ((crc & 0x01) != 0) {
			crc = (crc >> 1) ^ 0x8C;
		} else {
			crc = crc >> 1;
		}
	}
	return crc;
}

/*
	Validate an 8 bit CRC checksum. This is used during discovery
	and by some devices during data read.
*/
bool InterfaceOneWire::crc8CheckIButton(uint8_t* lpData, unsigned int dwLen, uint8_t crcToCheck) {
	uint8_t i;
	uint8_t crc = 0;
	for(i = 0; i < dwLen; i=i+1) {
		crc = crcUpdate8(crc, lpData[i]);
	}
	crc = crcUpdate8(crc, crcToCheck);
	return (crc == 0);
}


#ifdef ONEWIRE_SUPPORT_ENUMERATION
	/*
		Executes enumeration sequence on the bus and calls the callback for every discovered
		device. If alarmSearch is set only devices in alarm state are disovered.
	*/
	unsigned int InterfaceOneWire::discoverDevices(lpfnInterfaceOneWire_DiscoveredDevice callback, bool alarmSearch) {
		this->discoveredDevices = 0;
		this->discoverCallback = callback;

		/* Abort if we don't get a callback passed */
		if(callback == NULL) {
			return 0;
		}

		for(uint8_t i = 0; i < sizeof(adrCurrent); i=i+1) {
			adrCurrent[i] = 0x00;
		}

		/* Send first command */
		if(!this->resetAndPresenceDetection()) {
			return 0;
		}

		if(!alarmSearch) {
			writeByte(0xF0, false); // Issue Search ROM command
		} else {
			writeByte(0xEC, false); // Issue Alarm Search ROM command (only devices in alarm state will respond)
		}

		this->discoveryDevicesRecursive(0, alarmSearch);
	}
#endif

/*
	=========================
	=	Private routines	=
	=========================
*/

#ifdef ONEWIRE_SUPPORT_ENUMERATION
	/*
		This routine performs the recursive onewire search. It may
		recurse up to 65 times and requires 3 bytes local variables
		as well as 2 bytes of instruction pointer per recursion in
		the most compact possible way. This leads to at least 5 bytes
		required memory per recursion => 325 bytes of local memory
		have to be available for a complete device discovery by this
		method!
	*/
	void InterfaceOneWire::discoveryDevicesRecursive(
		uint8_t level,
		bool alarmSearch
	) {
		if(level == 8*8) {
			/* Check CRC */
			if(crc8CheckIButton(this->adrCurrent, sizeof(this->adrCurrent)-1, this->adrCurrent[sizeof(this->adrCurrent)-1])) {
				/*
					We found one device ... in case of CRC error we silently drop
					ToDo: Find out why every device repsonds to the LAST bit in both configurations
				*/
				this->discoverCallback(this->adrCurrent);
			}
			return;
		}

		uint8_t a;
		uint8_t b;
		uint8_t i;
		/*
			At each level: Read Bit & Bit complement.
			If decided -> next level
			if not decided -> select either one or the other
			else -> determined address

			before each recursion update buffer of "current" adress
		*/
		a = readBit();
		b = readBit();

		if((a == 1) && (b == 1)) {
			/* No devices with this adress on the bus ... end search on this "tree". */
			return;
		} else if((a == 0) && (b == 1)) {
			/* Only one device has a 0 in the corresponding bit location - take 0 path */
			this->adrCurrent[level / 8] &= (~(0x01 << (level % 8)));
			writeBit(0, false);

			/* recurse ... */
			discoveryDevicesRecursive(level + 1, alarmSearch);
		} else if((a == 1) && (b == 0)) {
			/* Only one device has a 1 in the corresponding bit location - take 1 path */
			this->adrCurrent[level / 8] |= (0x01 << (level % 8));
			writeBit(1, false);

			/* recurse ... */
			discoveryDevicesRecursive(level + 1, alarmSearch);
		} else { /* Both bits are pulled to low ... */
			/* Conflict situation - do both ... */
			/* Take 0 branch, discover all devices below */
			this->adrCurrent[level / 8] &= (~(0x01 << (level % 8)));
			writeBit(0, false);
			discoveryDevicesRecursive(level + 1, alarmSearch);

			/*
				And now take the 1 branch. We have to reset the bus and re-send ALL bits
				before to do ...
			*/
			if(!this->resetAndPresenceDetection()) {
				return; /* Abort scan if devices have vanished from the bus ... */
			}
			if(!alarmSearch) {
				writeByte(0xF0, false); /* Issue Search ROM command */
			} else {
				writeByte(0xEC, false); /* Issue Alarm search ROM command */
			}
			for(i = 0; i < level; i=i+1) { /* Now write bits that have been written in the other branch */
				readBit(); /* Discard bit ... */
				readBit(); /* Discard complement ... */
				writeBit(((this->adrCurrent[i / 8] & (0x01 << (i % 8))) == 0) ? 0 : 1, false);
			}

			/* And now take 1 branch */
			this->adrCurrent[level / 8] |= (0x01 << (level % 8));

			readBit(); /* Discard bit ... this was the conflicting bit */
			readBit(); /* Discard complement ... this was the complement of the conflicting bit */
			writeBit(1, false);
			discoveryDevicesRecursive(level + 1, alarmSearch);
		}
	}
#endif

/*
	Write a single bit to the 1-wire bus.

	Writing a 1 to the bus:
		- Pull the line low for < 15 us
		- Let the bus recovery to high (or drive high) for the remaining timeslot (45 us)
	Writing a 0 to the bus:
		- Pull the line low for the whole timeslot (60 us)
		- After the write a short time will allow the bus to recovery via pullup (5 us)
		  or active pullup will be enabled.
*/
void InterfaceOneWire::writeBit(uint8_t value, bool keepInterruptsDisabled) {
	if(value != 0) {
		noInterrupts();
		/* Pull line low for ~ 10 us (< 15 us) */
		pinLow();
		pinModeOutput();
		delayMicroseconds(10);
		/* Pull high the remaining timeslot (50 us) */
		pinHigh();
		delayMicroseconds(55);
		/* Set drivers floating again */
		pinModeInput();
		if(!keepInterruptsDisabled) {
			interrupts();
		}
	} else {
		noInterrupts();
		/* Pull low for whole timeslot */
		pinLow();
		pinModeOutput();
		delayMicroseconds(65);
		/* Allow a 5 us charging interval for parasitic devices */
		pinHigh();
		delayMicroseconds(5);
		pinModeInput();
		if(!keepInterruptsDisabled) {
			interrupts();
		}
	}
}

/*
	Read a single bit from the 1-wire bussystem.

	The read is initiated by pulling the data line low for approx. 6 us
	After additional 9 us (we round up to 10 us) the master should sample again
	The remaining 55 us of the timeslot & recovery period the master sleeps
*/
uint8_t InterfaceOneWire::readBit() {
	uint8_t result;

	noInterrupts();
	/* Short pull low */
	pinLow();
	pinModeOutput();
	delayMicroseconds(5);
	/* Pin floating, wait additional 10 us for slaves to assert signal & line to charge */
	pinModeInput();
	delayMicroseconds(10);
	/* Sample input and wait for remaining timeslot plus charging interval to finish */
	result = pinRead();
	interrupts(); /* Timeslice after this point is not critical if missed since we specify the timing ... */
	delayMicroseconds(55);

	return result;
}
