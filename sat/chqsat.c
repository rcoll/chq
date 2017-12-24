#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <MQTTClient.h>
#include <wiringPi.h>

#define MAXBUF 1024
#define DELIM "="
#define CONFIGFILE "/etc/chqsat/chqsat.conf"
#define LOG_PATH "/var/log/chqsat.log"

#define l( ... ) logPrint( __FILE__, __LINE__, __VA_ARGS__ )

static volatile int runLoop = 1;

FILE *logfile;
static int LOG_SESSION;

MQTTClient client;
MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
MQTTClient_deliveryToken deliveredToken;

struct config {
	char mqttaddress[MAXBUF];
	char mqttclientid[MAXBUF];
	char mqttqos[MAXBUF];
	char mqtttimeout[MAXBUF];
	char mqttprefix[MAXBUF];
	char switchpins[MAXBUF];
	char relaypins[MAXBUF];
};

void logPrint( char* filename, int line, char *fmt, ... ) {
	va_list list;
	char *p, *r, *timebuf;
	int e, size = 0;
	time_t t;

	t = time( NULL );

	char *timestr = asctime( localtime( &t ) );
	timestr[ strlen( timestr ) - 1 ] = 0;
	size = strlen( timestr ) + 1 + 2;
	timebuf = ( char* ) malloc( size );
	memset( timebuf, 0x0, size );
	snl( timebuf, size, "[%s]", timestr );

	if ( LOG_SESSION > 0 ) {
		logfile = fopen ( LOG_PATH, "a+" );
	} else {
		logfile = fopen ( LOG_PATH, "w" );
	}
	 
	fl( logfile, "%s [%s:%d] ", timebuf, filename, line );
	va_start( list, fmt );
 
	for ( p = fmt ; *p ; ++p ) {
		if ( *p != '%' ) {
			fputc( *p,logfile );
		} else {
			switch ( *++p ) {
				case 's': {
					r = va_arg( list, char * );
					fl(logfile,"%s", r);
					continue;
				}
				case 'd': {
					e = va_arg( list, int );
					fl(logfile,"%d", e);
					continue;
				}
				default: {
					fputc( *p, logfile );
				}
			}
		}
	}

	va_end( list );
	fputc( '\n', logfile );
	LOG_SESSION++;
	fclose( logfile );
}

char** str_split( char* a_str, const char a_delim ) {
	char** result = 0;
	size_t count = 0;
	char* tmp = a_str;
	char* last_comma = 0;
	char delim[2];
	delim[0] = a_delim;
	delim[1] = 0;

	while ( *tmp ) {
		if ( a_delim == *tmp ) {
			count++;
			last_comma = tmp;
		}
		tmp++;
	}

	count += last_comma < ( a_str + strlen( a_str ) - 1 );
	count++;

	result = malloc( sizeof( char* ) * count );

	if ( result ) {
		size_t idx  = 0;
		char* token = strtok( a_str, delim );

		while ( token ) {
			assert( idx < count );
			*( result + idx++ ) = strdup( token );
			token = strtok( 0, delim );
		}
	
		assert( idx == count - 1 );
		*( result + idx ) = 0;
	}

	return result;
}

struct config get_config( char *filename ) {
	struct config configstruct;
	FILE *file = fopen( filename, "r" );

	if ( file != NULL ) {
		char line[MAXBUF];
		int i = 0;

		while ( fgets( line, sizeof( line), file ) != NULL ) {
			char *cfline;
			cfline = strstr( ( char * ) line, DELIM );
			cfline = cfline + strlen( DELIM );

			if ( i == 0 ) {
				memcpy( configstruct.mqttaddress, cfline, strlen( cfline ) );
			} else if ( i == 1 ) {
				memcpy( configstruct.mqttclientid, cfline, strlen( cfline ) );
			} else if ( i == 2 ) {
				memcpy( configstruct.mqttqos, cfline, strlen( cfline ) );
			} else if ( i == 3 ) {
				memcpy( configstruct.mqtttimeout, cfline, strlen( cfline ) );
			} else if ( i == 4 ) {
				memcpy( configstruct.mqttprefix, cfline, strlen( cfline ) );
			} else if ( i == 5 ) {
				memcpy( configstruct.switchpins, cfline, strlen( cfline ) );
			} else if ( i == 6 ) {
				memcpy( configstruct.relaypins, cfline, strlen( cfline ) );
			}

			i++;
		}

		fclose( file );
	}

	size_t length;

	length = strlen( configstruct.mqttprefix );
	if ( configstruct.mqttprefix[ length - 1 ] == '\n' ) {
		configstruct.mqttprefix[ length - 1 ] = '\0';
	}

	length = strlen( configstruct.mqttaddress );
	if ( configstruct.mqttaddress[ length - 1 ] == '\n' ) {
		configstruct.mqttaddress[ length - 1 ] = '\0';
	}

	return configstruct;
}

int setRelayState( int pin, int state ) {
	if ( state != 1 && state != 0 ) {
		l( "OUTPUT > Requested relay state %d for pin %d is invalid!\n", state, pin );
		return 0;
	}

	digitalWrite( pin, state );
	l( "OUTPUT > Set relay %d to state %d\n", pin, state );
}

void toggleRelay( int pin ) {
	if ( digitalRead( pin ) == 0 ) {
		setRelayState( pin, 1 );
	} else {
		setRelayState( pin, 0 );
	}
}

void handleInterrupt( int sig ) {
	runLoop = 0;
	l( "SETUP > SIGINT\n" );
}

int mqttMessagePublish( char topic[], char command[] ) {
	int rc;
	struct config conf;
	
	MQTTClient_message pubmsg = MQTTClient_message_initializer;

	conf = get_config( CONFIGFILE );

	pubmsg.payload = command;
	pubmsg.payloadlen = strlen( command );
	pubmsg.qos = atoi( conf.mqttqos );
	pubmsg.retained = 0;

	l( "MQTT > Publishing message %s to topic %s\n", command, topic );

	MQTTClient_publishMessage( client, topic, &pubmsg, &deliveredToken );
	rc = MQTTClient_waitForCompletion( client, deliveredToken, 10000L );

	return rc;
}

void mqttMessageDelivered( void *context, MQTTClient_deliveryToken dt ) {
	l( "MQTT > Token %d delivery confirmed\n", dt );
	deliveredToken = dt;
}

int mqttMessageReceived( void *context, char *topicName, int topicLen, MQTTClient_message *message ) {
	int i;
	char *payloadptr, *msg, **command;

	msg = ( char * ) malloc( message->payloadlen );
	payloadptr = message->payload;

	for ( i = 0; i < message->payloadlen; i++ ) {
		msg[i] = *payloadptr++;
	}

	l( "MQTT > Topic %s received %s\n", topicName, msg );

	MQTTClient_freeMessage( &message );
	MQTTClient_free( topicName );

	command = str_split( msg, ',' );
	setRelayState( atoi( command[0] ), atoi( command[1] ) );

	return 1;
}

void mqttConnectionLost( void *context, char *cause ) {
	l( "MQTT > Subscription connection lost - cause: %s\n", cause );
}

void selfTest() {
	struct config conf;
	int gpio;
	char **gpioInputs, **gpioOutputs;

	conf = get_config( CONFIGFILE );
	gpioInputs = str_split( conf.switchpins, ',' );
	gpioOutputs = str_split( conf.relaypins, ',' );

	for ( int i = 0; *( gpioOutputs + i ); i++ ) {
		gpio = atoi( *( gpioOutputs + i ) );
		l( "TEST > Testing output %d on pin %d\n", i, gpio );
		digitalWrite( gpio, 1 );
		usleep( 200000 );
		digitalWrite( gpio, 0 );
		usleep( 100000 );
	}
	
	for ( int i = 0; *( gpioOutputs + i ); i++ ) {
		gpio = atoi( *( gpioOutputs + i ) );
		l( "TEST > Testing output %d on pin %d\n", i, gpio );
		digitalWrite( gpio, 1 );
		usleep( 200000 );
	}
	
	for ( int i = 0; *( gpioOutputs + i ); i++ ) {
		gpio = atoi( *( gpioOutputs + i ) );
		l( "TEST > Testing output %d on pin %d\n", i, gpio );
		digitalWrite( gpio, 0 );
		usleep( 200000 );
	}

	for ( int i = 0; *( gpioOutputs + i ); i++ ) {
		gpio = atoi( *( gpioOutputs + i ) );
		l( "TEST > Testing output %d on pin %d\n", i, gpio );
		digitalWrite( gpio, 1 );
	}

	usleep( 2000000 );

	for ( int i = 0; *( gpioOutputs + i ); i++ ) {
		gpio = atoi( *( gpioOutputs + i ) );
		l( "TEST > Testing output %d on pin %d\n", i, gpio );
		digitalWrite( gpio, 0 );
	}

	for ( int i = 0; *( gpioInputs + i ); i++ ) {
		gpio = atoi( *( gpioInputs + i ) );
		int state = digitalRead( gpio );
		l( "TEST > Tested input %d on pin %d, it reads %d\n", i, gpio, state );
		usleep( 100000 );
	}

	l( "TEST > All tests complete\n" );
}

int main( void ) {
	struct config conf;
	int gpio, i, rc, ch;
	char **gpioInputs, **gpioOutputs, topic[MAXBUF], triggerid[MAXBUF];

	// Handle sigints
	signal( SIGINT, handleInterrupt );

	// Get the configuration settings
	l( "SETUP > Opening config file at %s\n", CONFIGFILE );
	conf = get_config( CONFIGFILE );
	gpioInputs = str_split( conf.switchpins, ',' );
	gpioOutputs = str_split( conf.relaypins, ',' );
	l( "SETUP > Using switch pins %s\n", conf.switchpins );
	l( "SETUP > Using relay pins %s\n", conf.relaypins );

	// Hardware state setup
	l( "SETUP > Running wiringPi setup\n" );
	wiringPiSetup();

	// Create the MQTT client
	l( "SETUP > Creating MQTT client\n" );
	MQTTClient_create( &client, conf.mqttaddress, conf.mqttclientid, MQTTCLIENT_PERSISTENCE_NONE, NULL );
	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;
	MQTTClient_setCallbacks( client, NULL, mqttConnectionLost, mqttMessageReceived, mqttMessageDelivered );

	if ( ( rc = MQTTClient_connect( client, &conn_opts ) ) != MQTTCLIENT_SUCCESS ) {
		l( "SETUP > Failed to connect to MQTT server for subscription. Code %d", rc );
		exit( EXIT_FAILURE );
	}

	l( "SETUP > Connected to MQTT broker at %s\n", conf.mqttaddress );
	sprintf( topic, "relays/%s", conf.mqttprefix );
	MQTTClient_subscribe( client, topic, atoi( conf.mqttqos ) );
	l( "SETUP > Subscribed to MQTT topic %s\n", topic );

	if ( gpioInputs ) {
		for ( i = 0; *( gpioInputs + i ); i++ ) {
			gpio = atoi( *( gpioInputs + i ) );
			l( "SETUP > Setting up GPIO input on pin %d\n", gpio );
			pinMode( gpio, INPUT );
			pullUpDnControl( gpio, PUD_UP );
		}
	}

	if ( gpioOutputs ) {
		for ( i = 0; *( gpioOutputs + i ); i++ ) {
			gpio = atoi( *( gpioOutputs + i ) );
			l( "SETUP > Setting up GPIO output on pin %d\n", gpio );
			pinMode( gpio, OUTPUT );
		}
	}

	selfTest( gpioInputs, gpioOutputs );

	while ( runLoop ) {
		for ( i = 0; *( gpioInputs + i ); i++ ) {
			gpio = atoi( * ( gpioInputs + i ) );

			// Handle Dual/Multi-Push

			// Handle Long-Push

			// Handle short-push
			if ( digitalRead( gpio ) == 0 ) {
				sprintf( topic, "switches/%s/%d", conf.mqttprefix, gpio );
				l( "INPUT > Button %d pushed\n", gpio );
				mqttMessagePublish( topic, "PUSH" );
				usleep( 300000 );
			}

		}

		usleep( 30000 );
	}

	MQTTClient_disconnect( client, 10000 );
	MQTTClient_destroy( &client );

	return 0;
}