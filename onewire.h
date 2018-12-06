#ifndef __is_included__2FDA970B_EF5C_4CFF_BA43_D42710DDE745
#define __is_included__2FDA970B_EF5C_4CFF_BA43_D42710DDE745 1

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

		ONEWIRE_SUPPORT_OVERDRIVE
			If set overdrive mode is supported via
			appropriate function calls. Note that
			overdrive support is CURRENTLY NOT IMPLEMENTED!
*/

#define ONEWIRE_SUPPORT_ENUMERATION 1


#include <stdint.h>

#if ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
	#include "pins_arduino.h"
#endif

/*
	ONEWIRE_RETRY_RESETWAITHIGH defines how many 5 us cycles
	the bus should wait to go to idle (high) state before a
	reset pulse. If the bus does not reach idle during this
	period the reset routine fails.
*/
#ifndef ONEWIRE_RETRY_RESETWAITHIGH
	#define ONEWIRE_RETRY_RESETWAITHIGH 200
#endif

/*
	Definition for the disovered device callback. This callback
	is called during bus search for every located ROM ID. The
	64 bit rom id is passed as argument (8 bytes)
*/
typedef void (*lpfnInterfaceOneWire_DiscoveredDevice)(
	uint8_t* romId
);

class InterfaceOneWire {
	public:
		/*
			Constructor initializes all state variables and translates pin numbers to
			port register adresses and bitmasks
		*/
		InterfaceOneWire(uint8_t ioPin, uint8_t activePullupPin);
		~InterfaceOneWire();

		/*
			Perform a bus reset and detect if any devices are attached to the bus. If any
			device is present this function returns true. In case of an error (bus was not
			idle or no devices have been found) the function returns false.
		*/
		bool resetAndPresenceDetection();

		/*
			Write a single byte.

			If pullup is set to true either the data line is kept high instead of tristate if active
			pullup is not compiled OR active pullup is set. In this case interrupts stay disabled!
			The application has to call
				activePullupDisable();
			after the active pullup period. This has to happen BEFORE any device tries a pulldown (this
			would damage the device by overcurrent).
		*/
		void writeByte(uint8_t byte, bool pullup);

		/*
			Write multiple bytes

			If pullup is set to true either the data line is kept high instead of tristate if active
			pullup is not compiled OR active pullup is set. In this case interrupts stay disabled!
			The application has to call
				activePullupDisable();
			after the active pullup period. This has to happen BEFORE any device tries a pulldown (this
			would damage the device by overcurrent).
		*/
		void writeBytes(uint8_t* bytes, unsigned int length, bool pullup);

		/*
			Disable active pullup and re-enable interrupts.
		*/
		void activePullupDisable();

		/*
			Read a single byte
		*/
		uint8_t readByte();
		/*
			Read a sequence of bytes
		*/
		void readBytes(uint8_t* bytes, unsigned int length);

		/*
			Write a single bit. If keepInterruptsDisabled has been
			set interrupts are disabled and the pin is set to
			input after the read. The caller would have to call
			interrupts() again afterwards. This allows implementation
			of active pullup
		*/
		void writeBit(uint8_t value, bool keepInterruptsDisabled);
		/*
			Read a single bit from the 1-wire bus system. If the read
			bit has been 1 the return value != 0, else the return
			value is 0.
		*/
		uint8_t readBit();

		#ifdef ONEWIRE_SUPPORT_ENUMERATION
			/*
				Perform a 1-wire ROM search. For every found device the
				callback is invoked (with the found 8 byte ROM ID as
				argument). If alarmSearch is set to true, only devices
				that are currently in an alarm state (signalling) are
				returned (only these devices respond. For a long bus containing
				many devices the discovery process is speed up significantly
				to locate devices which have triggered into alarm state).

				Normally about 70 devices can be located per second.
			*/
			unsigned int discoverDevices(lpfnInterfaceOneWire_DiscoveredDevice callback, bool alarmSearch);
		#endif

		/*
			ROM Commands (Except scan)
		*/
		
		void romCommand_ROMSingle(); 						/* Select the only single device present on the bus */
		void romCommand_ROMSelect(uint8_t* romAdress);		/* Select the device with the given adress */
		void romCommand_ROMBroadcast();						/* Broadcast to all devices on the bus or use a single one */
		#ifdef ONEWIRE_SUPPORT_OVERDRIVE
			void romCommand_ROMSingleOverdrive();
			void romCommand_ROMSelectOverdrive(uint8_t* romAdress);
		#endif

		bool crc8CheckIButton(uint8_t* lpData, unsigned int dwLen, uint8_t crcToCheck); /* Performs a CRC check on the given data */
	private:
		#ifdef ONEWIRE_SUPPORT_ENUMERATION
			void discoveryDevicesRecursive(uint8_t level, bool alarmSearch); /* This routine is used during recursive lookup */
		#endif

		/*
			Here we keep the references to our I/O and optionally active pullup registers.
			The I/O register is the only onewire register directly used for input, output
			and optionally for parasitic power supply.
			
			The active pullup register can be connected to an active pullup FET that provides
			power to the bus devices in case they are parasitically powered and require more
			power than a simple 4.7kOhm pullup resistor can supply.
		*/
		volatile uint8_t*			ioRegister;			/*
															Base adress of the I/O Port registers:
																[0]		Current input values as a bitfield. 0 for low, 1 for high
																[1]		Mode selection. 0 for input, 1 for output
																[2]		Output values. 0 for low, 1 for high
														*/
		uint8_t						ioRegisterMask;		/* Mask for the I/O Port register for the I/O pin used. This mask "masks" the bit used for the 1-wire data pin */

		#ifdef ONEWIRE_ACTIVE_PULLUP
			volatile uint8_t*		pullupRegister;
			uint8_t					pullupRegisterMask;
		#endif

		/*
			State variables used by bus enumeration.
		*/
		#ifdef ONEWIRE_SUPPORT_ENUMERATION
			lpfnInterfaceOneWire_DiscoveredDevice	discoverCallback;
			uint8_t									adrCurrent[8];		/* 64 Bit Address */
			unsigned int							discoveredDevices;
		#endif

		/*
			Hardware I/O routines; They work by directly accessing
				DDR[n] (Data Direction Register) at ioRegister[1]
				PIN[n] (Port INput register) at ioRegister[0]
				PORT[n] (PORT output register) at ioRegister[2].
				
				Notice that the PORT[n] register also determines the usage of internal pullup!
				0 disabled internal pullup, 1 enabled it.
		*/
		inline uint8_t pinRead() 			{ return ((ioRegister[0] & ioRegisterMask) != 0) ? 1 : 0; 		}																		/* Read current I/O pin value */
		inline void pinLow() 				{ ioRegister[2] = ioRegister[2] & (~ioRegisterMask); 			}
		inline void pinHigh() 				{ ioRegister[2] = ioRegister[2] | ioRegisterMask; 				}

		inline void pinModeInput() 			{ ioRegister[1] = ioRegister[1] & (~ioRegisterMask); pinLow();	}
		inline void pinModeOutput() 		{ ioRegister[1] = ioRegister[1] | ioRegisterMask; 				}
		
		#ifdef ONEWIRE_ACTIVE_PULLUP
			inline void pullupInitialize() 	{ pullupRegister[1] = pullupRegister[1] | pullupRegisterMask; pullupRegister[2] = pullupRegister[2] & (~pullupRegisterMask); } 		/* Set mode to output, disable active pullup */
			inline void pullupEnable()		{ pullupRegister[2] = pullupRegister[2] | pullupRegisterMask; } 																	/* Enable active pullup */
			inline void pullupDisable()		{ pullupRegister[2] = pullupRegister[2] & (~pullupRegisterMask); } 																	/* Disable active pullup */
			inline void pullupShutdown()	{ pullupRegister[2] = pullupRegister[2] & (~pullupRegisterMask); pullupRegister[1] = pullupRegister[1] & (~pullupRegisterMask); } 	/* Set mode to input, set output bit to 0 */
		#endif
};

#endif
