#define _XOPEN_SOURCE 700

#include "sdwire3.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/usbdevice_fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define USB_DEVICES "bus/usb/devices"
#define BLOCK_CLASS "class/block"
#define STATE_POLL_INTERVAL_NS 50000000L
#define STATE_POLL_ATTEMPTS 100

#if defined(__GNUC__)
#define PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define PRINTF_LIKE(fmt, args)
#endif

/* The test suite points these at a fake tree. */
static const char *sysfs_root = "/sys";
static const char *usbfs_root = "/dev/bus/usb";

static int format_path(char *dst, size_t size, const char *fmt, ...)
    PRINTF_LIKE(3, 4);

static int
format_path(char *dst, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(dst, size, fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

static int
read_text(const char *path, char *buf, size_t size)
{
    FILE *fp;
    size_t len;
    int saved;

    if (size < 2 || size > INT_MAX) {
        errno = EINVAL;
        return -1;
    }

    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;

    if (fgets(buf, (int)size, fp) == NULL) {
        saved = ferror(fp) ? errno : ENODATA;
        fclose(fp);
        errno = saved;
        return -1;
    }

    len = strlen(buf);
    if (len == size - 1 && buf[len - 1] != '\n') {
        int c;

        c = fgetc(fp);
        if (c != EOF && c != '\n' && c != '\r') {
            fclose(fp);
            errno = EOVERFLOW;
            return -1;
        }
    }

    if (fclose(fp) != 0)
        return -1;

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
device_attr_path(char *dst, size_t size, const char *sys_name,
    const char *attr)
{
    return format_path(dst, size, "%s/" USB_DEVICES "/%s/%s", sysfs_root,
        sys_name, attr);
}

static int
find_interface0(const char *sys_name, char *result, size_t result_size)
{
    DIR *dir;
    struct dirent *de;
    char dir_path[PATH_MAX];
    char prefix[SDWIRE3_NAME_MAX + 2];
    size_t prefix_len;
    int found;

    if (format_path(prefix, sizeof(prefix), "%s:", sys_name) < 0)
        return -1;
    prefix_len = strlen(prefix);

    if (format_path(dir_path, sizeof(dir_path), "%s/" USB_DEVICES,
        sysfs_root) < 0)
        return -1;

    dir = opendir(dir_path);
    if (dir == NULL)
        return -1;

    found = 0;
    while ((de = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        char value[32];

        if (strncmp(de->d_name, prefix, prefix_len) != 0)
            continue;

        if (device_attr_path(path, sizeof(path), de->d_name,
            "bInterfaceNumber") < 0)
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
    char path[PATH_MAX];
    char usb_real[PATH_MAX];
    size_t usb_len;

    result[0] = '\0';

    if (format_path(path, sizeof(path), "%s/" USB_DEVICES "/%s", sysfs_root,
        sys_name) < 0)
        return -1;

    if (realpath(path, usb_real) == NULL)
        return -1;

    usb_len = strlen(usb_real);

    if (format_path(path, sizeof(path), "%s/" BLOCK_CLASS, sysfs_root) < 0)
        return -1;

    dir = opendir(path);
    if (dir == NULL)
        return -1;

    while ((de = readdir(dir)) != NULL) {
        char block_real[PATH_MAX];

        if (de->d_name[0] == '.')
            continue;

        if (format_path(path, sizeof(path), "%s/" BLOCK_CLASS "/%s/partition",
            sysfs_root, de->d_name) < 0)
            continue;
        if (access(path, F_OK) == 0)
            continue;

        if (format_path(path, sizeof(path), "%s/" BLOCK_CLASS "/%s",
            sysfs_root, de->d_name) < 0)
            continue;

        if (realpath(path, block_real) == NULL)
            continue;

        if (strncmp(block_real, usb_real, usb_len) != 0)
            continue;
        if (block_real[usb_len] != '/' && block_real[usb_len] != '\0')
            continue;

        if (format_path(result, result_size, "/dev/%s", de->d_name) < 0) {
            result[0] = '\0';
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
    char path[PATH_MAX];

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
usbfs_driver_ioctl(int fd, int code)
{
    struct usbdevfs_ioctl cmd;

    cmd.ifno = 0;
    cmd.ioctl_code = code;
    cmd.data = NULL;

    return ioctl(fd, USBDEVFS_IOCTL, &cmd);
}

/*
 * The same usbfs requests that sdwire-cli makes through libusb: detach
 * (target) or attach (host) the kernel driver of interface 0, then reset
 * the device.
 */
static int
switch_driver(const struct sdwire3_device *dev, enum sdwire3_state state)
{
    char path[PATH_MAX];
    int fd;
    int rc;
    int saved;

    if (format_path(path, sizeof(path), "%s/%03u/%03u", usbfs_root,
        dev->busnum, dev->devnum) < 0)
        return -1;

    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;

    if (state == SDWIRE3_STATE_TARGET) {
        rc = usbfs_driver_ioctl(fd, USBDEVFS_DISCONNECT);
    } else {
        rc = usbfs_driver_ioctl(fd, USBDEVFS_CONNECT);
        if (rc == 0) {
            /* No kernel driver accepted the interface. */
            errno = ENXIO;
            rc = -1;
        }
    }

    if (rc >= 0)
        rc = ioctl(fd, USBDEVFS_RESET, 0);

    saved = errno;
    if (close(fd) < 0 && rc >= 0)
        return -1;

    if (rc < 0) {
        errno = saved;
        return -1;
    }

    return 0;
}

static int
wait_for_state(struct sdwire3_device *dev, enum sdwire3_state state)
{
    enum sdwire3_state current;
    int attempt;

    for (attempt = 0; attempt < STATE_POLL_ATTEMPTS; attempt++) {
        if (attempt != 0) {
            struct timespec delay;

            delay.tv_sec = 0;
            delay.tv_nsec = STATE_POLL_INTERVAL_NS;
            while (nanosleep(&delay, &delay) < 0) {
                if (errno != EINTR)
                    return -1;
            }
        }

        /* A reset may re-enumerate the device, so look it up again. */
        if (sdwire3_refresh(dev) == 0 &&
            sdwire3_get_state(dev, &current) == 0 && current == state)
            return 0;
    }

    errno = ETIMEDOUT;
    return -1;
}

int
sdwire3_refresh(struct sdwire3_device *dev)
{
    char path[PATH_MAX];

    if (dev == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (find_interface0(dev->sys_name, dev->interface_name,
        sizeof(dev->interface_name)) < 0)
        return -1;

    if (device_attr_path(path, sizeof(path), dev->sys_name, "serial") < 0 ||
        read_text(path, dev->serial, sizeof(dev->serial)) < 0)
        dev->serial[0] = '\0';

    if (refresh_bus_address(dev) < 0)
        return -1;

    if (find_blockdev(dev->sys_name, dev->blockdev,
        sizeof(dev->blockdev)) < 0)
        dev->blockdev[0] = '\0';

    return 0;
}

static int
compare_devices(const void *a, const void *b)
{
    const struct sdwire3_device *da = a;
    const struct sdwire3_device *db = b;

    return strcmp(da->sys_name, db->sys_name);
}

int
sdwire3_scan(struct sdwire3_device **devices, size_t *count)
{
    DIR *dir;
    struct dirent *de;
    struct sdwire3_device *list;
    char dir_path[PATH_MAX];
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

    if (format_path(dir_path, sizeof(dir_path), "%s/" USB_DEVICES,
        sysfs_root) < 0)
        return -1;

    dir = opendir(dir_path);
    if (dir == NULL) {
        /* A kernel without USB support has no devices to offer. */
        if (errno == ENOENT)
            return 0;
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        char path[PATH_MAX];
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

        if (sdwire3_refresh(dev) < 0)
            continue;

        used++;
    }

    closedir(dir);

    if (used > 1)
        qsort(list, used, sizeof(*list), compare_devices);

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
sdwire3_match(const struct sdwire3_device *dev, const char *id)
{
    const char *serial;
    const char *ports;
    size_t len;

    if (dev == NULL || id == NULL || id[0] == '\0')
        return 0;

    if (strcmp(id, dev->sys_name) == 0)
        return 1;
    if (dev->serial[0] != '\0' && strcmp(id, dev->serial) == 0)
        return 1;

    /* sdwire-cli's ID is "<serial>.<ports>", e.g. "<serial>.1.3" for 2-1.3. */
    ports = strchr(dev->sys_name, '-');
    if (ports == NULL)
        return 0;

    serial = dev->serial[0] != '\0' ? dev->serial : "unknown";
    len = strlen(serial);

    return strncmp(id, serial, len) == 0 && id[len] == '.' &&
        strcmp(id + len + 1, ports + 1) == 0;
}

int
sdwire3_get_state(const struct sdwire3_device *dev, enum sdwire3_state *state)
{
    char path[PATH_MAX];
    char driver[PATH_MAX];
    const char *name;
    struct stat st;
    ssize_t n;

    if (dev == NULL || state == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (dev->interface_name[0] == '\0') {
        errno = ENODEV;
        return -1;
    }

    if (device_attr_path(path, sizeof(path), dev->interface_name,
        "driver") < 0)
        return -1;

    n = readlink(path, driver, sizeof(driver) - 1);
    if (n >= 0) {
        driver[n] = '\0';
        name = strrchr(driver, '/');
        name = name != NULL ? name + 1 : driver;

        /* Like libusb, do not count a usbfs claim as a kernel driver. */
        if (strcmp(name, "usbfs") == 0)
            *state = SDWIRE3_STATE_TARGET;
        else
            *state = SDWIRE3_STATE_HOST;
        return 0;
    }

    if (errno != ENOENT)
        return -1;

    /* No driver link: target mode, unless the interface itself is gone. */
    if (format_path(path, sizeof(path), "%s/" USB_DEVICES "/%s", sysfs_root,
        dev->interface_name) < 0)
        return -1;

    if (stat(path, &st) < 0) {
        if (errno == ENOENT)
            errno = ENODEV;
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        errno = ENODEV;
        return -1;
    }

    *state = SDWIRE3_STATE_TARGET;
    return 0;
}

int
sdwire3_set_state(struct sdwire3_device *dev, enum sdwire3_state state)
{
    enum sdwire3_state current;

    if (dev == NULL ||
        (state != SDWIRE3_STATE_HOST && state != SDWIRE3_STATE_TARGET)) {
        errno = EINVAL;
        return -1;
    }

    if (sdwire3_refresh(dev) < 0)
        return -1;

    if (sdwire3_get_state(dev, &current) < 0)
        return -1;

    if (current == state)
        return 0;

    if (switch_driver(dev, state) < 0)
        return -1;

    return wait_for_state(dev, state);
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
