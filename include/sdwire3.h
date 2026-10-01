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
    char sys_name[SDWIRE3_NAME_MAX];
    char interface_name[SDWIRE3_NAME_MAX];
    char serial[SDWIRE3_NAME_MAX];
    char blockdev[SDWIRE3_PATH_MAX];
    unsigned int busnum;
    unsigned int devnum;
};

int sdwire3_scan(struct sdwire3_device **devices, size_t *count);
void sdwire3_free(struct sdwire3_device *devices);

int sdwire3_get_state(const struct sdwire3_device *dev, enum sdwire3_state *state);
int sdwire3_set_state(struct sdwire3_device *dev, enum sdwire3_state state);
int sdwire3_refresh(struct sdwire3_device *dev);

const char *sdwire3_state_name(enum sdwire3_state state);
const char *sdwire3_id(const struct sdwire3_device *dev);

#endif
