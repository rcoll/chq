#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <MQTTClient.h>
#include <wiringPi.h>

#define MAXBUF 1024
#define DELIM "="
#define CONFIGFILE "/etc/chqsat/chqsat.conf"

struct config {
	char mqttaddress[MAXBUF];
	char mqttclientid[MAXBUF];
	char mqttqos[MAXBUF];
	char mqtttimeout[MAXBUF];
	char mqttprefix[MAXBUF];
	char gpios[MAXBUF];
};

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
				memcpy( configstruct.gpios, cfline, strlen( cfline ) );
			}

			i++;
		}

		fclose( file );
	}

	size_t length = strlen( configstruct.mqttprefix );
	if ( configstruct.mqttprefix[ length - 1 ] == '\n' ) {
		configstruct.mqttprefix[ length - 1 ] = '\0';
	}

	return configstruct;
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

int mqttPublish( char topic[], char command[] ) {
	struct config conf;
	conf = get_config( CONFIGFILE );

	MQTTClient client;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	MQTTClient_deliveryToken token;
	int rc;

	MQTTClient_create( &client, conf.mqttaddress, conf.mqttclientid, MQTTCLIENT_PERSISTENCE_NONE, NULL );
	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;

	if ( ( rc = MQTTClient_connect( client, &conn_opts ) ) != MQTTCLIENT_SUCCESS ) {
		exit( -1 );
	}

	pubmsg.payload = command;
	pubmsg.payloadlen = strlen( command );
	pubmsg.qos = atoi( conf.mqttqos );
	pubmsg.retained = 0;

	MQTTClient_publishMessage( client, topic, &pubmsg, &token );
	rc = MQTTClient_waitForCompletion( client, token, 10000L );
	MQTTClient_disconnect( client, 10000 );
	MQTTClient_destroy( &client );
	
	return rc;
}

int main( void ) {
	struct config conf;
	int gpio, i;
	char **gpios, topic[MAXBUF], triggerid[MAXBUF];
	
	conf = get_config( CONFIGFILE );
	gpios = str_split( conf.gpios, ',' );
	
	if ( gpios ) {
		wiringPiSetup();

		for ( i = 0; *( gpios + i ); i++ ) {
			gpio = atoi( *( gpios + i ) );
			printf( "Setting up GPIO %d\n", gpio );
			pinMode( gpio, INPUT );
		}
	}

	while ( 1 ) {
		for ( i = 0; *( gpios + i ); i++ ) {
			gpio = atoi( * ( gpios + i ) );
			
			if ( digitalRead( gpio ) == 0 ) {

				sprintf( topic, "switches/%s%d", conf.mqttprefix, gpio );
				printf( "PUSH: %s\n", topic );
				mqttPublish( topic, "PUSH" );
				usleep( 300000 );
			}
		}
	}

	return 0;
}