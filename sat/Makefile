chqsat: chqsat.c
	gcc -o chqsat.o chqsat.c -l paho-mqtt3c -l wiringPi -Wall
	mkdir bin
	mv chqsat.o bin/chqsat

install:
	gcc -o /usr/bin/chqsat chqsat.c -l paho-mqtt3c -l wiringPi -Wall
	cp init.sh /etc/init.d/chqsat
	chmod +x /etc/init.d/chqsat

uninstall:
	rm /usr/bin/chqsat
	rm /etc/init.d/chqsat

clean:
	rm -Rf bin
	rm /var/log/chqsat.log