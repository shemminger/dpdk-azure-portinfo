include $(RTE_SDK)/mk/rte.vars.mk

APP = dpdk-portinfo

CFLAGS += -O -Wall -W
CFLAGS += -DALLOW_EXPERIMENTAL_API
SRCS-y := main.c netvsc.c failsafe.c json_writer.c

include $(RTE_SDK)/mk/rte.app.mk
