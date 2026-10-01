#define _POSIX_C_SOURCE 200809L

#include "sdwire3.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/usbdevice_fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SYS_USB_DEVICES "/sys/bus/usb/devices"
#define SYS_BLOCK_CLASS "/sys/class/block"
#define SYS_USB_DRIVERS_PROBE "/sys/bus/usb/drivers_probe"
#define DEV_USB_FMT "/dev/bus/usb/%03u/%03u"
#define STATE_POLL_INTERVAL_NS 50000000L
#define STATE_POLL_ATTEMPTS 100

static int
read_text(const char *path, char *buf, size_t size)
{
    FILE *fp;
    size_t len;

    if (size == 0) {
        errno = EINVAL;
        return -1;
    }

    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;

    if (fgets(buf, (int)size, fp) == NULL) {
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0)
        return -1;

    len = strlen(buf);
    while (len != 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[len - 1] = '\0';
        len--;
    }

    return 0;
}

static int
read_uint(const char *path, int base, unsigned int *value)
{
    char buf[64];
    char *end;
    unsigned long v;

    if (read_text(path, buf, sizeof(buf)) < 0)
        return -1;

    errno = 0;
    v = strtoul(buf, &end, base);
    if (errno != 0 || *buf == '\0' || *end != '\0' || v > UINT_MAX) {
        errno = EINVAL;
        return -1;
    }

    *value = (unsigned int)v;
    return 0;
}

static int
write_text(const char *path, const char *text)
{
    int fd;
    size_t len;
    ssize_t n;

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;

    len = strlen(text);
    n = write(fd, text, len);
    if (n < 0 || (size_t)n != len) {
        int saved;

        saved = errno;
        close(fd);
        errno = n < 0 ? saved : EIO;
        return -1;
    }

    if (close(fd) < 0)
        return -1;

    return 0;
}

static int
path_join(char *dst, size_t size, const char *a, const char *b)
{
    int n;

    n = snprintf(dst, size, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

static int
device_attr_path(char *dst, size_t size, const char *sys_name,
    const char *attr)
{
    int n;

    n = snprintf(dst, size, "%s/%s/%s", SYS_USB_DEVICES, sys_name, attr);
    if (n < 0 || (size_t)n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

static int
find_interface0(const char *sys_name, char *result, size_t result_size)
{
    DIR *dir;
    struct dirent *de;
    char prefix[SDWIRE3_NAME_MAX + 2];
    size_t prefix_len;
    int found;

    if (snprintf(prefix, sizeof(prefix), "%s:", sys_name) >= (int)sizeof(prefix)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    prefix_len = strlen(prefix);

    dir = opendir(SYS_USB_DEVICES);
    if (dir == NULL)
        return -1;

    found = 0;
    while ((de = readdir(dir)) != NULL) {
        char path[SDWIRE3_PATH_MAX];
        char value[32];

        if (strncmp(de->d_name, prefix, prefix_len) != 0)
            continue;

        if (snprintf(path, sizeof(path), "%s/%s/bInterfaceNumber",
            SYS_USB_DEVICES, de->d_name) >= (int)sizeof(path))
            continue;

        if (read_text(path, value, sizeof(value)) < 0)
            continue;

        if (strcmp(value, "00") != 0)
            continue;

        if (strlen(de->d_name) >= result_size) {
            closedir(dir);
            errno = ENAMETOOLONG;
            return -1;
        }

        strcpy(result, de->d_name);
        found = 1;
        break;
    }

    closedir(dir);

    if (!found) {
        errno = ENODEV;
        return -1;
    }

    return 0;
}

static int
find_blockdev(const char *sys_name, char *result, size_t result_size)
{
    DIR *dir;
    struct dirent *de;
    char usb_path[SDWIRE3_PATH_MAX];
    char usb_real[SDWIRE3_PATH_MAX];
    size_t usb_len;

    result[0] = '\0';

    if (path_join(usb_path, sizeof(usb_path), SYS_USB_DEVICES, sys_name) < 0)
        return -1;

    if (realpath(usb_path, usb_real) == NULL)
        return -1;

    usb_len = strlen(usb_real);
    dir = opendir(SYS_BLOCK_CLASS);
    if (dir == NULL)
        return -1;

    while ((de = readdir(dir)) != NULL) {
        char class_path[SDWIRE3_PATH_MAX];
        char block_real[SDWIRE3_PATH_MAX];
        char partition_path[SDWIRE3_PATH_MAX];
        int n;

        if (de->d_name[0] == '.')
            continue;

        n = snprintf(partition_path, sizeof(partition_path), "%s/%s/partition",
            SYS_BLOCK_CLASS, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(partition_path))
            continue;
        if (access(partition_path, F_OK) == 0)
            continue;

        n = snprintf(class_path, sizeof(class_path), "%s/%s",
            SYS_BLOCK_CLASS, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(class_path))
            continue;

        if (realpath(class_path, block_real) == NULL)
            continue;

        if (strncmp(block_real, usb_real, usb_len) != 0)
            continue;
        if (block_real[usb_len] != '/' && block_real[usb_len] != '\0')
            continue;

        n = snprintf(result, result_size, "/dev/%s", de->d_name);
        if (n < 0 || (size_t)n >= result_size) {
            closedir(dir);
            errno = ENAMETOOLONG;
            return -1;
        }

        closedir(dir);
        return 0;
    }

    closedir(dir);
    return 0;
}

static int
refresh_bus_address(struct sdwire3_device *dev)
{
    char path[SDWIRE3_PATH_MAX];

    if (device_attr_path(path, sizeof(path), dev->sys_name, "busnum") < 0)
        return -1;
    if (read_uint(path, 10, &dev->busnum) < 0)
        return -1;

    if (device_attr_path(path, sizeof(path), dev->sys_name, "devnum") < 0)
        return -1;
    if (read_uint(path, 10, &dev->devnum) < 0)
        return -1;

    return 0;
}

static int
usb_reset(struct sdwire3_device *dev)
{
    char path[128];
    int fd;
    int error;

    if (refresh_bus_address(dev) < 0)
        return -1;

    if (snprintf(path, sizeof(path), DEV_USB_FMT, dev->busnum, dev->devnum)
        >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;

    error = ioctl(fd, USBDEVFS_RESET, 0);
    if (error < 0) {
        int saved;

        saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    return close(fd);
}

static int
unbind_interface(const char *interface_name)
{
    char link_path[SDWIRE3_PATH_MAX];
    char driver_real[SDWIRE3_PATH_MAX];
    char unbind_path[SDWIRE3_PATH_MAX];
    int n;

    n = snprintf(link_path, sizeof(link_path), "%s/%s/driver",
        SYS_USB_DEVICES, interface_name);
    if (n < 0 || (size_t)n >= sizeof(link_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (realpath(link_path, driver_real) == NULL)
        return -1;

    n = snprintf(unbind_path, sizeof(unbind_path), "%s/unbind", driver_real);
    if (n < 0 || (size_t)n >= sizeof(unbind_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return write_text(unbind_path, interface_name);
}

static int
probe_interface(const char *interface_name)
{
    return write_text(SYS_USB_DRIVERS_PROBE, interface_name);
}

int
sdwire3_refresh(struct sdwire3_device *dev)
{
    if (dev == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (find_interface0(dev->sys_name, dev->interface_name,
        sizeof(dev->interface_name)) < 0)
        return -1;

    if (refresh_bus_address(dev) < 0)
        return -1;

    if (find_blockdev(dev->sys_name, dev->blockdev,
        sizeof(dev->blockdev)) < 0)
        dev->blockdev[0] = '\0';

    return 0;
}

int
sdwire3_scan(struct sdwire3_device **devices, size_t *count)
{
    DIR *dir;
    struct dirent *de;
    struct sdwire3_device *list;
    size_t used;
    size_t capacity;

    if (devices == NULL || count == NULL) {
        errno = EINVAL;
        return -1;
    }

    *devices = NULL;
    *count = 0;
    list = NULL;
    used = 0;
    capacity = 0;

    dir = opendir(SYS_USB_DEVICES);
    if (dir == NULL)
        return -1;

    while ((de = readdir(dir)) != NULL) {
        char path[SDWIRE3_PATH_MAX];
        unsigned int vid;
        unsigned int pid;
        struct sdwire3_device *tmp;
        struct sdwire3_device *dev;

        if (de->d_name[0] == '.')
            continue;
        if (strchr(de->d_name, ':') != NULL)
            continue;

        if (device_attr_path(path, sizeof(path), de->d_name, "idVendor") < 0)
            continue;
        if (read_uint(path, 16, &vid) < 0)
            continue;

        if (device_attr_path(path, sizeof(path), de->d_name, "idProduct") < 0)
            continue;
        if (read_uint(path, 16, &pid) < 0)
            continue;

        if (vid != SDWIRE3_VID || pid != SDWIRE3_PID)
            continue;

        if (used == capacity) {
            size_t new_capacity;

            new_capacity = capacity == 0 ? 4 : capacity * 2;
            tmp = realloc(list, new_capacity * sizeof(*list));
            if (tmp == NULL) {
                int saved;

                saved = errno;
                free(list);
                closedir(dir);
                errno = saved;
                return -1;
            }
            list = tmp;
            capacity = new_capacity;
        }

        dev = &list[used];
        memset(dev, 0, sizeof(*dev));

        if (strlen(de->d_name) >= sizeof(dev->sys_name))
            continue;
        strcpy(dev->sys_name, de->d_name);

        if (device_attr_path(path, sizeof(path), dev->sys_name, "serial") == 0) {
            if (read_text(path, dev->serial, sizeof(dev->serial)) < 0)
                dev->serial[0] = '\0';
        }

        if (sdwire3_refresh(dev) < 0)
            continue;

        used++;
    }

    closedir(dir);

    *devices = list;
    *count = used;
    return 0;
}

void
sdwire3_free(struct sdwire3_device *devices)
{
    free(devices);
}

int
sdwire3_get_state(const struct sdwire3_device *dev, enum sdwire3_state *state)
{
    char interface_path[SDWIRE3_PATH_MAX];
    char path[SDWIRE3_PATH_MAX];
    struct stat st;
    int n;

    if (dev == NULL || state == NULL) {
        errno = EINVAL;
        return -1;
    }

    n = snprintf(interface_path, sizeof(interface_path), "%s/%s",
        SYS_USB_DEVICES, dev->interface_name);
    if (n < 0 || (size_t)n >= sizeof(interface_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (stat(interface_path, &st) < 0)
        return -1;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENODEV;
        return -1;
    }

    n = snprintf(path, sizeof(path), "%s/driver", interface_path);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (access(path, F_OK) == 0) {
        *state = SDWIRE3_STATE_HOST;
        return 0;
    }

    if (errno == ENOENT) {
        *state = SDWIRE3_STATE_TARGET;
        return 0;
    }

    return -1;
}

int
sdwire3_set_state(struct sdwire3_device *dev, enum sdwire3_state state)
{
    enum sdwire3_state current;
    int attempt;

    if (dev == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (sdwire3_refresh(dev) < 0)
        return -1;

    if (sdwire3_get_state(dev, &current) < 0)
        return -1;

    if (current == state)
        return 0;

    if (state == SDWIRE3_STATE_TARGET) {
        if (unbind_interface(dev->interface_name) < 0)
            return -1;
    } else if (state == SDWIRE3_STATE_HOST) {
        if (probe_interface(dev->interface_name) < 0)
            return -1;
    } else {
        errno = EINVAL;
        return -1;
    }

    if (usb_reset(dev) < 0)
        return -1;

    if (sdwire3_refresh(dev) < 0)
        return -1;

    for (attempt = 0; attempt < STATE_POLL_ATTEMPTS; attempt++) {
        if (sdwire3_get_state(dev, &current) == 0 && current == state)
            return 0;

        if (attempt + 1 < STATE_POLL_ATTEMPTS) {
            struct timespec delay;

            delay.tv_sec = 0;
            delay.tv_nsec = STATE_POLL_INTERVAL_NS;
            while (nanosleep(&delay, &delay) < 0) {
                if (errno != EINTR)
                    return -1;
            }
        }
    }

    errno = ETIMEDOUT;
    return -1;
}

const char *
sdwire3_state_name(enum sdwire3_state state)
{
    if (state == SDWIRE3_STATE_HOST)
        return "host";
    if (state == SDWIRE3_STATE_TARGET)
        return "target";
    return "unknown";
}

const char *
sdwire3_id(const struct sdwire3_device *dev)
{
    if (dev == NULL)
        return "";
    if (dev->serial[0] != '\0')
        return dev->serial;
    return dev->sys_name;
}
