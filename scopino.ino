//-----------------------------------------------------------------------------
// Girino.ino
//-----------------------------------------------------------------------------
// Copyright 2012 Cristiano Lino Fontana
// Copyright 2014 Eric Trepanier
//
// This file is part of Girino.
//
//	Girino is free software: you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	Girino is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with Girino.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------

#include "settings.h"
#include "isr.h"
#include "interface.h"
#include "inits.h"
#include "scopino.h"

//-----------------------------------------------------------------------------
// Global Constants
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

volatile  boolean wait;
volatile  boolean crossdown;
          boolean enabletrig;
		 uint16_t waitDuration;
volatile uint16_t stopIndex;
volatile uint16_t ADCCounter;
volatile  uint8_t ADCBuffer[ADCBUFFERSIZE];
volatile  boolean freeze;

          uint8_t prescaler;
          uint8_t triggerEvent;
          uint8_t threshold;

             char commandBuffer[COMBUFFERSIZE+1];

// Signals a message using the onboard LED on pin 13
void message(int nb)
{
    for (int i=0; i<nb; i++)
    {
        digitalWrite(errorPin, HIGH);
        delay(10);
        digitalWrite(errorPin, LOW);
        delay(90);
    }
}

//-----------------------------------------------------------------------------
// Main routines
//-----------------------------------------------------------------------------
//
// The setup function initializes registers.
//
void setup (void) {		// Setup of the microcontroller
	// Open serial port with a baud rate of BAUDRATE b/s
	Serial.begin(BAUDRATE);

	dshow("# setup()");
	// Clear buffers
	memset( (void *)ADCBuffer, 0, sizeof(ADCBuffer) );
	memset( (void *)commandBuffer, 0, sizeof(commandBuffer) );

	ADCCounter = 0;
	wait = false;
    crossdown = false;
    enabletrig = false;
	threshold = 10;
	waitDuration = ADCBUFFERSIZE - 32;
	stopIndex = -1;
	freeze = false;

	prescaler = 128;
	triggerEvent = 3; // not used

	// Activate interrupts
	sei();

	initPins();
	initADC();
	initAnalogComparator();

    // Generate calibrating signal on Digital Pin 2
    //tone(2, 1000);

    while(!Serial)
    {
        message(3);
    }
    Serial.println("Girino ready");
    printStatus();
}

void loop (void) {
    static boolean restart = false;
    //dprint(ADCCounter);
    //dprint(stopIndex);
    //dprint(wait);
    //dprint(freeze);
#if DEBUG == 1
    //Serial.println( ADCSRA, BIN );
    //Serial.println( ADCSRB, BIN );
#endif

    // Warn the user that the serial port is not opened
    // and wait for a connection
    while(!Serial)
    {
        message(3);
    }

	// If freeze flag is set, then it is time to send the buffer to the serial port
	if ( freeze )
	{
        //dshow("# Frozen");

        //dprint(ADCCounter);
        //dprint(stopIndex);
        //dprint(wait);
        //dprint(freeze);
    
#if DEBUG == 1
        // Display the buffer in human readable form
        //for (int i=ADCCounter; i<ADCBUFFERSIZE; i++)
        //{
        //  Serial.print("i;"); Serial.print(i); Serial.print(";b;");
        //  //Serial.print(i); Serial.print(";");
        //  Serial.println(ADCBuffer[i], DEC);
        //}
        //for (int i=0; i<ADCCounter; i++)
        //{
        //  Serial.print("i;"); Serial.print(i); Serial.print(";b;");
        //  //Serial.print(i); Serial.print(";");
        //  Serial.println(ADCBuffer[i], DEC);
        //}
#endif

        // Send buffer through serial port in the right order
        Serial.write(255);
        Serial.write( (uint8_t *)ADCBuffer + ADCCounter, ADCBUFFERSIZE - ADCCounter );
        Serial.write( (uint8_t *)ADCBuffer, ADCCounter );

		// Ensure ERRORPIN is off
		cbi(PORTC,PORTC7);

        // Reset flags
        wait = false;
        crossdown = false;
        enabletrig = false;
        freeze = false;

        // If restart is on, automatically start another acquisition
        if (restart)
        {
            // Clear buffer
            memset( (void *)ADCBuffer, 0, sizeof(ADCBuffer) );

            startADC();
            // Let the ADC fill the buffer a little bit (holdoff)
            delay(10);
            // Look for a trigger
            startAnalogComparator();
        }

#if DEBUG == 1
        // delay(3000);
#endif
	}

    // Commands polling
	if ( Serial.available() > 0 ) {
		// Read the incoming byte
		char theChar = Serial.read();
		// Parse character
		switch (theChar) {
			case 's':			// 's' for starting ADC conversions
				Serial.println("ADC conversions started");

                // Clear buffer
                memset( (void *)ADCBuffer, 0, sizeof(ADCBuffer) );

                startADC();
                // Let the ADC fill the buffer a little bit (holdoff)
                delay(10);
                // Look for a trigger
                startAnalogComparator();

                // Activate auto restart
                restart = true;
                break;

            case 'x':			// 'x' for stopping ADC conversions
                Serial.println("ADC conversions stopped");
                
                stopAnalogComparator();
                stopADC();
                cbi(PORTC,PORTC7);
                break;

			case 'p':			// 'p' for new prescaler setting
			case 'P': 
				// Wait for COMMANDDELAY ms to be sure that the Serial buffer is filled
				delay(COMMANDDELAY);

				fillBuffer( commandBuffer, COMBUFFERSIZE );

				// Convert buffer to integer
				uint8_t newP = atoi( commandBuffer );

				// Display moving status indicator
				Serial.print("Setting prescaler to: ");
				Serial.println(newP);

				prescaler = newP;
				setADCPrescaler(newP);
				break;

			case 'r':			// 'r' for new voltage reference setting
			case 'R': 
				// Wait for COMMANDDELAY ms to be sure that the Serial buffer is filled
				delay(COMMANDDELAY);

				fillBuffer( commandBuffer, COMBUFFERSIZE );

				// Convert buffer to integer
				uint8_t newR = atoi( commandBuffer );

				// Display moving status indicator
				Serial.print("Setting voltage reference to: ");
				Serial.println(newR);

				setVoltageReference(newR);
				break;

			case 'e':			// 'e' for new trigger event setting
			case 'E': 
				// Wait for COMMANDDELAY ms to be sure that the Serial buffer is filled
				delay(COMMANDDELAY);

				fillBuffer( commandBuffer, COMBUFFERSIZE );

				// Convert buffer to integer
				uint8_t newE = atoi( commandBuffer );

				// Display moving status indicator
				Serial.print("Setting trigger event to: ");
				Serial.println(newE);

				triggerEvent = newE;
				setTriggerEvent(newE);
				break;

			case 'w':			// 'w' for new wait setting
			case 'W': 
				// Wait for COMMANDDELAY ms to be sure that the Serial buffer is filled
				delay(COMMANDDELAY);

				fillBuffer( commandBuffer, COMBUFFERSIZE );

				// Convert buffer to integer
				uint8_t newW = atoi( commandBuffer );

				// Display moving status indicator
				Serial.print("Setting waitDuration to: ");
				Serial.println(newW);

				waitDuration = newW;
				break;

			case 't':			// 't' for new threshold setting
			case 'T': 
				// Wait for COMMANDDELAY ms to be sure that the Serial buffer is filled
				delay(COMMANDDELAY);

				fillBuffer( commandBuffer, COMBUFFERSIZE );

				// Convert buffer to integer
				uint8_t newT = atoi( commandBuffer );

				// Display moving status indicator
				Serial.print("Setting threshold to: ");
				Serial.println(newT);

				threshold = newT;
				break;

			case 'd':			// 'd' for display status
			case 'D':
				printStatus();
				break;

			default:
				// Display error message
				Serial.print("ERROR: Command not found, it was: ");
				Serial.println(theChar);
				error();
		}
	}
}
