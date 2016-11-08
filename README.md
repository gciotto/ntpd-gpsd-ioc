## NTP - GPS EPICS Server and OPI Interfaces

### Author: 

Gustavo CIOTTO PINTON

### Description

This project contains two possible EPICS PV server implementations and some *opi* interfaces capable of reading the provided PVs.

The *python* implementation can be found in `PCASpy` folder and it is based on the [PCASpy](https://pcaspy.readthedocs.io/en/latest/) module. The second implementation consists of a bridge between NTPD and GPSD *daemons* and [Stream IOC](http://git.cnpem.br/eduardo.coelho/stream-ioc). It essentially uses an UNIX socket to exchange data between applications and provides BSMP entities.

### Requirements

1. `GPS-PV-Server.py` requires `pcaspy`, `ntplib` and `libgps` modules.
2. `ntpd_ioc.c` requires NTPD source files and `gpsd_ioc.c` requires `libgps-dev` package. [libbsmp](http://git.cnpem.br/bruno.martins/libbsmp) is also required.
3. A [Control System Studio](http://controlsystemstudio.org/) instance is required to open and run the interfaces.

### Running

1. *python PV server*: edit `GPS-PV-Server.py` with server's correct IP address (`_address`) and variable name prefix (`_prefix`).
2. Using with *Stream IOC*: run `./ioc_main` and start Stream IOC afterwards.
3. `OPI` interfaces: open CSS and run Host-selection.opi. You will need to include or remove buttons and macros in `Host-selection.opi`, depending on how many servers are available.

Refer to [this repository](http://git.cnpem.br/gustavo.pinton/ntp-gps-building-scrips) in order to set everything up.