#include "sdwire3.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(FILE *out)
{
    fprintf(out,
        "usage:\n"
        "  csdwire3 list\n"
        "  csdwire3 state [-s serial-or-port]\n"
        "  csdwire3 switch [-s serial-or-port] host|target\n");
}

static struct sdwire3_device *
select_device(struct sdwire3_device *devices, size_t count, const char *id)
{
    size_t i;

    if (id == NULL) {
        if (count == 1)
            return &devices[0];
        if (count == 0)
            fprintf(stderr, "csdwire3: no SDWire3 device found\n");
        else
            fprintf(stderr,
                "csdwire3: multiple devices found; select one with -s\n");
        return NULL;
    }

    for (i = 0; i < count; i++) {
        if (strcmp(id, devices[i].serial) == 0 ||
            strcmp(id, devices[i].sys_name) == 0)
            return &devices[i];
    }

    fprintf(stderr, "csdwire3: device '%s' not found\n", id);
    return NULL;
}

static int
parse_selector(int argc, char **argv, int start, const char **selector,
    int *next)
{
    *selector = NULL;
    *next = start;

    if (*next + 1 < argc && strcmp(argv[*next], "-s") == 0) {
        *selector = argv[*next + 1];
        *next += 2;
    }

    return 0;
}

static int
command_list(struct sdwire3_device *devices, size_t count)
{
    size_t i;

    printf("%-24s %-12s %-10s %s\n", "ID", "USB", "STATE", "BLOCK");

    for (i = 0; i < count; i++) {
        enum sdwire3_state state;
        char usb[32];
        const char *state_name;

        if (sdwire3_get_state(&devices[i], &state) < 0)
            state_name = "unknown";
        else
            state_name = sdwire3_state_name(state);

        snprintf(usb, sizeof(usb), "%u:%u",
            devices[i].busnum, devices[i].devnum);

        printf("%-24s %-12s %-10s %s\n",
            sdwire3_id(&devices[i]),
            usb,
            state_name,
            devices[i].blockdev[0] != '\0' ? devices[i].blockdev : "-");
    }

    return 0;
}

static int
command_state(struct sdwire3_device *devices, size_t count,
    const char *selector)
{
    struct sdwire3_device *dev;
    enum sdwire3_state state;

    dev = select_device(devices, count, selector);
    if (dev == NULL)
        return 1;

    if (sdwire3_get_state(dev, &state) < 0) {
        perror("csdwire3: state");
        return 1;
    }

    printf("%s\n", sdwire3_state_name(state));
    return 0;
}

static int
command_switch(struct sdwire3_device *devices, size_t count,
    const char *selector, const char *mode)
{
    struct sdwire3_device *dev;
    enum sdwire3_state state;

    dev = select_device(devices, count, selector);
    if (dev == NULL)
        return 1;

    if (strcmp(mode, "host") == 0 || strcmp(mode, "ts") == 0)
        state = SDWIRE3_STATE_HOST;
    else if (strcmp(mode, "target") == 0 || strcmp(mode, "dut") == 0)
        state = SDWIRE3_STATE_TARGET;
    else {
        fprintf(stderr, "csdwire3: invalid mode '%s'\n", mode);
        return 1;
    }

    if (sdwire3_set_state(dev, state) < 0) {
        perror("csdwire3: switch");
        return 1;
    }

    return 0;
}

int
main(int argc, char **argv)
{
    struct sdwire3_device *devices;
    size_t count;
    const char *selector;
    int next;
    int status;

    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    if (sdwire3_scan(&devices, &count) < 0) {
        perror("csdwire3: scan");
        return 1;
    }

    status = 0;

    if (strcmp(argv[1], "list") == 0) {
        if (argc != 2) {
            usage(stderr);
            status = 2;
        } else {
            status = command_list(devices, count);
        }
    } else if (strcmp(argv[1], "state") == 0) {
        parse_selector(argc, argv, 2, &selector, &next);
        if (next != argc) {
            usage(stderr);
            status = 2;
        } else {
            status = command_state(devices, count, selector);
        }
    } else if (strcmp(argv[1], "switch") == 0) {
        parse_selector(argc, argv, 2, &selector, &next);
        if (next + 1 != argc) {
            usage(stderr);
            status = 2;
        } else {
            status = command_switch(devices, count, selector, argv[next]);
        }
    } else {
        usage(stderr);
        status = 2;
    }

    sdwire3_free(devices);
    return status;
}
