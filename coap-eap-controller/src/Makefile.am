AM_CPPFLAGS+= -D CONFIGDIR=\"$(sysconfdir)\" ## Sets the config dir
## wpa_supplicant source directory
WPA_SRC =$(srcdir)/wpa_supplicant/src
# Rafa: Añadido este opción de compilación #
############################################
INCLUDES		      = -I$(WPA_SRC) -I$(WPA_SRC)/utils
AM_CXX=clang++	           
AM_CXXFLAGS=-Wall -g -v -DDEBUG -std=c++11
### user programs ###
bin_PROGRAMS = coapeapcontroller 

### source declarations ###
coapeapcontroller_SOURCES               = mainserver.cpp \
				state_machines/coap_eap_session.c \
 				prf_plus.c \
				panamessages.c \
				lalarm.c \
				panautils.c \
				loadconfig.c \
				aes.c \
				eax.c \
				cantcoap-master/nethelper.c

coapeapcontroller_CPPFLAGS 	      = $(AM_CPPFLAGS) 
coapeapcontroller_CFLAGS		      = $(AM_CFLAGS) -DISSERVER
coapeapcontroller_LDFLAGS		      = $(DBG_LINKOPT)

### library dependencies ###

coapeapcontroller_LDADD                     = libeapstack/libeap.a \
				    cantcoap-master/libcantcoap.a
		




## Adding PANA header files to distribute using "make dist"
EXTRA_DIST =  *.h state_machines/*.h config.xml

## Extra config files needed
sysconf_DATA = \
		config.xml 

