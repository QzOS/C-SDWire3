/*
 * Library tests against a fake sysfs tree. The library source is included
 * directly so that its sysfs and usbfs roots can be pointed at the fake tree;
 * no real USB device is ever touched.
 */
#include "../src/sdwire3.c"

#include <ftw.h>

#define HCD "sys/devices/pci0000:00/0000:00:14.0/usb2"
#define SERIAL "20120501030900000"

#define CHECK(cond) check((cond), #cond, __LINE__)

static char root[PATH_MAX];
static char fake_sysfs[PATH_MAX];
static char fake_usbfs[PATH_MAX];
static int failures;

static void
check(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "test_sdwire3.c:%d: check failed: %s\n", line, expr);
        failures++;
    }
}

static void
die(const char *what, const char *path)
{
    fprintf(stderr, "test setup: %s %s: %s\n", what, path, strerror(errno));
    exit(2);
}

static void
full_path(char *dst, const char *rel)
{
    if (format_path(dst, PATH_MAX, "%s/%s", root, rel) < 0)
        die("path", rel);
}

/* Creates every missing directory of an absolute path below root. */
static void
make_dirs_abs(char *path)
{
    char *p;

    for (p = path + strlen(root) + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(path, 0755) < 0 && errno != EEXIST)
            die("mkdir", path);
        *p = '/';
    }
    if (mkdir(path, 0755) < 0 && errno != EEXIST)
        die("mkdir", path);
}

static void
make_dirs(const char *rel)
{
    char path[PATH_MAX];

    full_path(path, rel);
    make_dirs_abs(path);
}

static void
make_parent(const char *path)
{
    char parent[PATH_MAX];
    char *slash;

    strcpy(parent, path);
    slash = strrchr(parent, '/');
    *slash = '\0';
    make_dirs_abs(parent);
}

static void
put(const char *rel, const char *text)
{
    char path[PATH_MAX];
    FILE *fp;

    full_path(path, rel);
    make_parent(path);
    fp = fopen(path, "w");
    if (fp == NULL || fputs(text, fp) == EOF || fclose(fp) != 0)
        die("write", path);
}

static void
attr(const char *dir, const char *name, const char *value)
{
    char rel[PATH_MAX];
    char text[256];

    if (format_path(rel, sizeof(rel), "%s/%s", dir, name) < 0 ||
        format_path(text, sizeof(text), "%s\n", value) < 0)
        die("path", name);
    put(rel, text);
}

/* Makes rel a symlink to target, both relative to root. */
static void
link_to(const char *target, const char *rel)
{
    char from[PATH_MAX];
    char to[PATH_MAX];

    full_path(to, target);
    full_path(from, rel);
    make_parent(from);
    if (symlink(to, from) < 0)
        die("symlink", from);
}

static void
remove_path(const char *rel)
{
    char path[PATH_MAX];

    full_path(path, rel);
    if (unlink(path) < 0)
        die("unlink", path);
}

static void
add_device(const char *name, const char *vid, const char *pid,
    const char *serial, const char *devnum, const char *driver)
{
    char dev[PATH_MAX];
    char intf[PATH_MAX];
    char rel[PATH_MAX];

    if (format_path(dev, sizeof(dev), HCD "/%s", name) < 0 ||
        format_path(intf, sizeof(intf), "%s/%s:1.0", dev, name) < 0)
        die("path", name);

    attr(dev, "idVendor", vid);
    attr(dev, "idProduct", pid);
    attr(dev, "serial", serial);
    attr(dev, "busnum", "2");
    attr(dev, "devnum", devnum);
    attr(intf, "bInterfaceNumber", "00");

    if (format_path(rel, sizeof(rel), "sys/" USB_DEVICES "/%s", name) < 0)
        die("path", name);
    link_to(dev, rel);
    if (format_path(rel, sizeof(rel), "sys/" USB_DEVICES "/%s:1.0", name) < 0)
        die("path", name);
    link_to(intf, rel);

    if (driver != NULL) {
        if (format_path(rel, sizeof(rel), "sys/bus/usb/drivers/%s",
            driver) < 0 ||
            format_path(intf, sizeof(intf), "%s/%s:1.0/driver", dev, name) < 0)
            die("path", driver);
        link_to(rel, intf);
    }
}

static void
add_block(const char *dir, const char *name, int partition)
{
    char rel[PATH_MAX];
    char link[PATH_MAX];

    if (format_path(rel, sizeof(rel), "%s/%s", dir, name) < 0 ||
        format_path(link, sizeof(link), "sys/" BLOCK_CLASS "/%s", name) < 0)
        die("path", name);
    make_dirs(rel);
    if (partition)
        attr(rel, "partition", "1");
    link_to(rel, link);
}

static void
build_fixture(void)
{
    /* The root hub and its interface are neither SDWire3 nor devices. */
    attr(HCD, "idVendor", "1d6b");
    attr(HCD, "idProduct", "0003");
    link_to(HCD, "sys/" USB_DEVICES "/usb2");
    attr(HCD "/2-0:1.0", "bInterfaceNumber", "00");
    link_to(HCD "/2-0:1.0", "sys/" USB_DEVICES "/2-0:1.0");

    make_dirs("sys/bus/usb/drivers/usb-storage");
    make_dirs("sys/bus/usb/drivers/usbfs");

    /* Two SDWire3 sharing a serial: 2-2 in host mode with sdb and sdb1. */
    add_device("2-2", "0bda", "0316", SERIAL, "6", "usb-storage");
    add_device("2-1", "0bda", "0316", SERIAL, "5", NULL);
    add_block(HCD "/2-2/2-2:1.0/host3/target3:0:0/3:0:0:0/block", "sdb", 0);
    add_block(HCD "/2-2/2-2:1.0/host3/target3:0:0/3:0:0:0/block/sdb",
        "sdb1", 1);

    /* A USB stick on port 2-10, whose path starts with that of 2-1. */
    add_device("2-10", "0781", "5567", "4C530001", "7", "usb-storage");
    add_block(HCD "/2-10/2-10:1.0/host4/target4:0:0/4:0:0:0/block", "sdc", 0);

    add_block("sys/devices/pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/"
        "0:0:0:0/block", "sda", 0);

    /* usbfs node of 2-2; a plain file rejects every ioctl with ENOTTY. */
    put("dev/bus/usb/002/006", "");
}

static void
load(struct sdwire3_device *dev, const char *sys_name)
{
    memset(dev, 0, sizeof(*dev));
    strcpy(dev->sys_name, sys_name);
    CHECK(sdwire3_refresh(dev) == 0);
}

static void
test_read_helpers(void)
{
    char path[PATH_MAX];
    char buf[8];
    unsigned int value;

    put("misc/crlf", "abc\r\n");
    full_path(path, "misc/crlf");
    CHECK(read_text(path, buf, sizeof(buf)) == 0 && strcmp(buf, "abc") == 0);

    put("misc/exact", "0123456\n");
    full_path(path, "misc/exact");
    CHECK(read_text(path, buf, sizeof(buf)) == 0 &&
        strcmp(buf, "0123456") == 0);

    put("misc/long", "01234567\n");
    full_path(path, "misc/long");
    errno = 0;
    CHECK(read_text(path, buf, sizeof(buf)) < 0 && errno == EOVERFLOW);

    put("misc/empty", "");
    full_path(path, "misc/empty");
    errno = 0;
    CHECK(read_text(path, buf, sizeof(buf)) < 0 && errno == ENODATA);

    put("misc/hex", "0bda\n");
    full_path(path, "misc/hex");
    CHECK(read_uint(path, 16, &value) == 0 && value == 0x0bda);

    put("misc/junk", "12ab\n");
    full_path(path, "misc/junk");
    errno = 0;
    CHECK(read_uint(path, 10, &value) < 0 && errno == EINVAL);
}

static void
test_scan(void)
{
    struct sdwire3_device *devices;
    size_t count;

    CHECK(sdwire3_scan(&devices, &count) == 0);
    CHECK(count == 2);
    if (count == 2) {
        CHECK(strcmp(devices[0].sys_name, "2-1") == 0);
        CHECK(strcmp(devices[0].interface_name, "2-1:1.0") == 0);
        CHECK(strcmp(devices[0].serial, SERIAL) == 0);
        CHECK(devices[0].busnum == 2 && devices[0].devnum == 5);
        CHECK(devices[0].blockdev[0] == '\0');

        CHECK(strcmp(devices[1].sys_name, "2-2") == 0);
        CHECK(strcmp(devices[1].interface_name, "2-2:1.0") == 0);
        CHECK(devices[1].busnum == 2 && devices[1].devnum == 6);
        CHECK(strcmp(devices[1].blockdev, "/dev/sdb") == 0);
    }
    sdwire3_free(devices);
}

static void
test_state(void)
{
    struct sdwire3_device dev;
    enum sdwire3_state state;

    load(&dev, "2-1");
    CHECK(sdwire3_get_state(&dev, &state) == 0 &&
        state == SDWIRE3_STATE_TARGET);

    link_to("sys/bus/usb/drivers/usbfs", HCD "/2-1/2-1:1.0/driver");
    CHECK(sdwire3_get_state(&dev, &state) == 0 &&
        state == SDWIRE3_STATE_TARGET);
    remove_path(HCD "/2-1/2-1:1.0/driver");

    load(&dev, "2-2");
    CHECK(sdwire3_get_state(&dev, &state) == 0 &&
        state == SDWIRE3_STATE_HOST);
}

static void
test_match(void)
{
    struct sdwire3_device a;
    struct sdwire3_device b;
    struct sdwire3_device hub;

    load(&a, "2-1");
    load(&b, "2-2");

    CHECK(sdwire3_match(&a, "2-1"));
    CHECK(!sdwire3_match(&b, "2-1"));
    CHECK(sdwire3_match(&a, SERIAL) && sdwire3_match(&b, SERIAL));
    CHECK(sdwire3_match(&a, SERIAL ".1"));
    CHECK(!sdwire3_match(&b, SERIAL ".1"));
    CHECK(sdwire3_match(&b, SERIAL ".2"));
    CHECK(!sdwire3_match(&a, SERIAL "."));
    CHECK(!sdwire3_match(&a, SERIAL ".1.1"));
    CHECK(!sdwire3_match(&a, "2-"));
    CHECK(!sdwire3_match(&a, ""));
    CHECK(!sdwire3_match(&a, NULL));

    memset(&hub, 0, sizeof(hub));
    strcpy(hub.sys_name, "3-1.4");
    CHECK(sdwire3_match(&hub, "unknown.1.4"));
    CHECK(!sdwire3_match(&hub, ""));
    strcpy(hub.serial, SERIAL);
    CHECK(sdwire3_match(&hub, SERIAL ".1.4"));
    CHECK(!sdwire3_match(&hub, "unknown.1.4"));
}

static void
test_set_state(void)
{
    struct sdwire3_device dev;

    load(&dev, "2-2");
    errno = 0;
    CHECK(sdwire3_set_state(&dev, (enum sdwire3_state)7) < 0 &&
        errno == EINVAL);

    /* Already in host mode, so usbfs must not be used. */
    CHECK(sdwire3_set_state(&dev, SDWIRE3_STATE_HOST) == 0);

    errno = 0;
    CHECK(sdwire3_set_state(&dev, SDWIRE3_STATE_TARGET) < 0 &&
        errno == ENOTTY);

    load(&dev, "2-1");
    errno = 0;
    CHECK(sdwire3_set_state(&dev, SDWIRE3_STATE_HOST) < 0 &&
        errno == ENOENT);
}

static void
test_absent_device(void)
{
    struct sdwire3_device dev;
    enum sdwire3_state state;

    memset(&dev, 0, sizeof(dev));
    errno = 0;
    CHECK(sdwire3_get_state(&dev, &state) < 0 && errno == ENODEV);

    load(&dev, "2-1");
    remove_path("sys/" USB_DEVICES "/2-1:1.0");

    errno = 0;
    CHECK(sdwire3_get_state(&dev, &state) < 0 && errno == ENODEV);
    errno = 0;
    CHECK(sdwire3_refresh(&dev) < 0 && errno == ENODEV);

    link_to(HCD "/2-1/2-1:1.0", "sys/" USB_DEVICES "/2-1:1.0");
}

static void
test_no_usb(void)
{
    struct sdwire3_device *devices;
    char empty[PATH_MAX];
    size_t count;

    make_dirs("empty");
    full_path(empty, "empty");
    sysfs_root = empty;

    count = 1;
    CHECK(sdwire3_scan(&devices, &count) == 0 && count == 0);
    sdwire3_free(devices);

    sysfs_root = fake_sysfs;
}

static int
remove_entry(const char *path, const struct stat *st, int type,
    struct FTW *ftw)
{
    (void)st;
    (void)type;
    (void)ftw;
    return remove(path);
}

static void
cleanup(void)
{
    if (root[0] != '\0')
        nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}

int
main(void)
{
    const char *tmp;

    tmp = getenv("TMPDIR");
    if (tmp == NULL || tmp[0] == '\0')
        tmp = "/tmp";
    if (format_path(root, sizeof(root), "%s/csdwire3-test.XXXXXX", tmp) < 0 ||
        mkdtemp(root) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    atexit(cleanup);

    full_path(fake_sysfs, "sys");
    full_path(fake_usbfs, "dev/bus/usb");
    sysfs_root = fake_sysfs;
    usbfs_root = fake_usbfs;

    build_fixture();

    test_read_helpers();
    test_scan();
    test_state();
    test_match();
    test_set_state();
    test_absent_device();
    test_no_usb();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    printf("test_sdwire3: all checks passed\n");
    return 0;
}
