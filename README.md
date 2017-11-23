
#RelayAttenuator

	Relay Attenuator application software is designed for Allo Relay Attenutor. It works with LIRC library to provide IR control to the Attenuator board.
	A client program (r_attenuc) is available to integrate with Player GUIs.

Volume control:

	In Relay-Attenuator, volume is controlled through relays by means of switches, remote controller and client program. Volume up/down and 
	Mute controls can be directed to relays directly or through the player. Play/Pause control is routed to player.

	a) Switches:
	       The relay board can be connected with external push-button switches for Mute, Play/Pause, Volume up and Volume down functions.

	b) IR Remote controller:
		Any remote can be configured to working with Attenuator software.

  	c) client Program:
	       r_attenuc - set/get Volume levels and mute status.
		
		 $ sudo r_attenuc -c GET_VOLUME    // To get current volume level

		 $ sudo r_attenuc -c SET_VOLUME=x  // To get set volume; x = 0 to 63 

		 $ sudo r_attenuc -c GET_MUTE  // To get current mute status

		 $ sudo r_attenuc -c SET_MUTE = x  // To set mute/unmute ; x = 0 to unmute or x = 1 to mute  


Requiments:

	Attenuator software requires  WiringPi & LIRC libraries.

WiringPi(On RPi):

	$ cd /usr/local/
	$ git clone git://git.drogon.net/wiringPi
	$ cd wiringPi
	$ git pull origin
	$ ./build 


LIRC: 

	$ cd /usr/local/
	$ git clone https://github.com/FernetMenta/lirc.git

  Download:

	$ cd /usr/local/lirc/daemons/
	$ git clone https://github.com/allocom/RelayAttenuator.git
	Files will be downloaded at daemons/RelayAttenuator


  Compilation:

     After download, follow below commands:

	$ cd /usr/local/lirc
	
	Add "daemons/RelayAttenuator/Makefile" under "AC_CONFIG_FILES" in the file ./configure.ac
	Add "daemons/RelayAttenuator" under SUBDIRS in the file ./Makefile.am
	
	$ sudo ./autogen.sh
	$ sudo ./configure --with-syslog --with-driver=userspace
	$ sudo make
	$ sudo make install 


  Config files hardware.conf, lircd.conf & lircrc are available in git.

	lircd.conf is generated for Allo remote.

	lircrc is configured for squeezelite, can be changed for other players.
	
	$ cp hardware.conf lircd.conf  lircrc /etc/lirc/


Execution:

	Make sure lircd is running & just execute the server program

	$ r_attenu

	To execute the server on bootup, add "r_attenu > /dev/null 2>&1 " in /etc/rc.local
