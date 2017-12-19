#include <stdio.h>
#include <wiringPi.h>
#include <sr595.h>

void setRelayState( int relayId, int relayState ) {
	digitalWrite( 100 + relayId, relayState );
}

void selfTest() {
	int i;

	for ( i = 0; i < 8; i++ ) {
		setRelayState( i, 1 );
		delay( 250 );
		setRelayState( i, 0 );
		delay( 250 );
	}

	for ( i = 0; i < 8; i++ ) {
		setRelayState( i, 1 );
		delay( 500 );
	}

	for ( i = 0; i < 8; i++ ) {
		setRelayState( i, 0 );
		delay( 500 );
	}

	for ( i = 0; i < 8; i++ ) {
		setRelayState( i, 1 );
	}

	delay( 1000 );

	for ( i = 0; i < 8; i++ ) {
		setRelayState( i, 0 );
	}
}

int main( void ) {
	wiringPiSetup();
	sr595Setup( 100, 8, 0, 1, 2 );

	selfTest();
}
