#ifndef SDWIRE3_H
#define SDWIRE3_H

#include <stddef.h>

#define SDWIRE3_VID 0x0bda
#define SDWIRE3_PID 0x0316

#define SDWIRE3_NAME_MAX 128
#define SDWIRE3_PATH_MAX 4096

enum sdwire3_state {
    SDWIRE3_STATE_TARGET = 0,
    SDWIRE3_STATE_HOST = 1
};

struct sdwire3_device {
    char sys_name[SDWIRE3_NAME_MAX];       /* USB port, e.g. "2-1.3" */
    char interface_name[SDWIRE3_NAME_MAX]; /* interface 0, e.g. "2-1.3:1.0" */
    char serial[SDWIRE3_NAME_MAX];         /* may be empty or shared */
    char blockdev[SDWIRE3_PATH_MAX];       /* e.g. "/dev/sdb", or empty */
    unsigned int busnum;
    unsigned int devnum;
};

/* Devices are returned sorted by port. */
int sdwire3_scan(struct sdwire3_device **devices, size_t *count);
void sdwire3_free(struct sdwire3_device *devices);

/* Non-zero if id is the device's port, serial or sdwire-cli ID. */
int sdwire3_match(const struct sdwire3_device *dev, const char *id);

int sdwire3_get_state(const struct sdwire3_device *dev, enum sdwire3_state *state);

/*
 * Switches and waits until the new state is visible. Besides system errors,
 * errno is ENXIO when no kernel driver accepts the interface and ETIMEDOUT
 * when the state is not reached after the reset.
 */
int sdwire3_set_state(struct sdwire3_device *dev, enum sdwire3_state state);
int sdwire3_refresh(struct sdwire3_device *dev);

const char *sdwire3_state_name(enum sdwire3_state state);

#endif
