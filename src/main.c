#include "sdwire3.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum command {
    COMMAND_LIST,
    COMMAND_STATE,
    COMMAND_SWITCH
};

static void
usage(FILE *out)
{
    fprintf(out,
        "usage:\n"
        "  csdwire3 list\n"
        "  csdwire3 state [-s id]\n"
        "  csdwire3 switch [-s id] host|target\n"
        "\n"
        "  -s, --serial id  USB port from 'list' (e.g. 2-1.3), serial number\n"
        "                   or sdwire-cli ID; optional with one SDWire3\n"
        "  ts and dut are aliases for host and target\n");
}

static void
print_ports(const struct sdwire3_device *devices, size_t count,
    const char *id)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (id == NULL || sdwire3_match(&devices[i], id))
            fprintf(stderr, " %s", devices[i].sys_name);
    }
    fprintf(stderr, "\n");
}

static struct sdwire3_device *
select_device(struct sdwire3_device *devices, size_t count, const char *id)
{
    struct sdwire3_device *dev;
    size_t matches;
    size_t i;

    if (id == NULL) {
        if (count == 1)
            return &devices[0];
        if (count == 0) {
            fprintf(stderr, "csdwire3: no SDWire3 device found\n");
        } else {
            fprintf(stderr,
                "csdwire3: multiple devices found; select one with -s:");
            print_ports(devices, count, NULL);
        }
        return NULL;
    }

    dev = NULL;
    matches = 0;
    for (i = 0; i < count; i++) {
        if (!sdwire3_match(&devices[i], id))
            continue;
        if (dev == NULL)
            dev = &devices[i];
        matches++;
    }

    if (matches == 0) {
        fprintf(stderr, "csdwire3: device '%s' not found\n", id);
        return NULL;
    }

    if (matches > 1) {
        fprintf(stderr,
            "csdwire3: '%s' matches %zu devices; select one by port:",
            id, matches);
        print_ports(devices, count, id);
        return NULL;
    }

    return dev;
}

static int
parse_selector(int argc, char **argv, int *next, const char **selector)
{
    *selector = NULL;

    if (*next >= argc || (strcmp(argv[*next], "-s") != 0 &&
        strcmp(argv[*next], "--serial") != 0))
        return 0;

    if (*next + 1 >= argc)
        return -1;

    *selector = argv[*next + 1];
    *next += 2;
    return 0;
}

static int
parse_mode(const char *mode, enum sdwire3_state *state)
{
    if (strcmp(mode, "host") == 0 || strcmp(mode, "ts") == 0)
        *state = SDWIRE3_STATE_HOST;
    else if (strcmp(mode, "target") == 0 || strcmp(mode, "dut") == 0)
        *state = SDWIRE3_STATE_TARGET;
    else
        return -1;

    return 0;
}

static int
command_list(struct sdwire3_device *devices, size_t count)
{
    size_t i;

    printf("%-12s %-24s %-8s %s\n", "PORT", "SERIAL", "STATE", "BLOCK");

    for (i = 0; i < count; i++) {
        enum sdwire3_state state;
        const char *state_name;

        if (sdwire3_get_state(&devices[i], &state) < 0)
            state_name = "unknown";
        else
            state_name = sdwire3_state_name(state);

        printf("%-12s %-24s %-8s %s\n",
            devices[i].sys_name,
            devices[i].serial[0] != '\0' ? devices[i].serial : "-",
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
    const char *selector, enum sdwire3_state state)
{
    struct sdwire3_device *dev;
    int err;

    dev = select_device(devices, count, selector);
    if (dev == NULL)
        return 1;

    if (sdwire3_set_state(dev, state) == 0)
        return 0;

    err = errno;
    if (err == ENXIO)
        fprintf(stderr, "csdwire3: switch: no kernel driver accepted %s\n",
            dev->interface_name);
    else if (err == ETIMEDOUT)
        fprintf(stderr, "csdwire3: switch: %s did not reach %s mode\n",
            dev->sys_name, sdwire3_state_name(state));
    else
        fprintf(stderr, "csdwire3: switch: %s\n", strerror(err));

    if (err == EACCES || err == EPERM)
        fprintf(stderr, "csdwire3: switching needs write access to "
            "/dev/bus/usb/%03u/%03u; run as root or see README\n",
            dev->busnum, dev->devnum);

    return 1;
}

int
main(int argc, char **argv)
{
    struct sdwire3_device *devices;
    enum command command;
    enum sdwire3_state state;
    const char *selector;
    size_t count;
    int next;
    int status;

    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }

    if (strcmp(argv[1], "list") == 0) {
        command = COMMAND_LIST;
    } else if (strcmp(argv[1], "state") == 0) {
        command = COMMAND_STATE;
    } else if (strcmp(argv[1], "switch") == 0) {
        command = COMMAND_SWITCH;
    } else {
        usage(stderr);
        return 2;
    }

    selector = NULL;
    next = 2;
    if (command != COMMAND_LIST &&
        parse_selector(argc, argv, &next, &selector) < 0) {
        usage(stderr);
        return 2;
    }

    state = SDWIRE3_STATE_TARGET;
    if (command == COMMAND_SWITCH) {
        if (next + 1 != argc) {
            usage(stderr);
            return 2;
        }
        if (parse_mode(argv[next], &state) < 0) {
            fprintf(stderr, "csdwire3: invalid mode '%s'\n", argv[next]);
            return 2;
        }
    } else if (next != argc) {
        usage(stderr);
        return 2;
    }

    if (sdwire3_scan(&devices, &count) < 0) {
        perror("csdwire3: scan");
        return 1;
    }

    if (command == COMMAND_LIST)
        status = command_list(devices, count);
    else if (command == COMMAND_STATE)
        status = command_state(devices, count, selector);
    else
        status = command_switch(devices, count, selector, state);

    sdwire3_free(devices);

    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "csdwire3: error writing output\n");
        return 1;
    }

    return status;
}
