S_PATH=..

bin_PROGRAMS = r_attenu r_attenuc

AM_CXXFLAGS = $(INTI_CFLAGS)

r_attenu_SOURCES = r_attenu.c \
		$(S_PATH)/config_file.c $(S_PATH)/config_file.h \
		$(S_PATH)/dump_config.c $(S_PATH)/dump_config.h \
		$(S_PATH)/input_map.c $(S_PATH)/input_map.h \
		$(S_PATH)/transmit.c $(S_PATH)/transmit.h 

r_attenu_CFLAGS = -I$(S_PATH)/ -lwiringPi -llirc_client -lm -O0
r_attenu_LDADD = $(S_PATH)/libhw_module.a @hw_module_libs@ @receive@
r_attenu_DEPENDENCIES = @receive@


r_attenuc_SOURCES = r_attenuc.c

