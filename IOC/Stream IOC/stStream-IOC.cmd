#!../bin/linux-arm/streamApp

# Environment variables.

epicsEnvSet("EPICS_BASE", "/root/base-3.14.12.5")
epicsEnvSet("ASYN", "/root/asyn4-30")
epicsEnvSet("TOP", "/root/stream-ioc")
epicsEnvSet("ARCH", "linux-arm")
epicsEnvSet ("STREAM_PROTOCOL_PATH", "$(TOP)/protocol")

# Load database definition file.

cd ${TOP}
dbLoadDatabase("dbd/streamApp.dbd")
streamApp_registerRecordDeviceDriver(pdbbase)

# Beginning of device configurations. In each block below, the communication to a specific device
# and respective record loading are specified.

# Communication with a NTPD and GPSD query program is done via a unix socket
drvAsynIPPortConfigure("UNIX_SOCKET", "unix:///tmp/ntp.socket")

# ntpd-gpsd.db defines all variables with can be requested from a NTP server via libntpq and a GPS receiver via GPSD daemon
dbLoadRecords("database/ntpd-gpsd.db", "PREFIX = Cont:LNLS191, PORT = UNIX_SOCKET")

# End of device configurations.

# Effectively initializes the IOC.

cd iocBoot
iocInit
