/*
 * Input Monitor for qt-mpv-wrapper
 * Monitors mouse/touch input events and sends quit command to mpv via IPC socket
 * Distinguishes between mouse movement and actual button clicks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <errno.h>

#define BUFFER_SIZE 256

/* Bit manipulation macros for testing device capabilities */
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define OFF(x)  ((x) % BITS_PER_LONG)
#define BIT(x)  (1UL << OFF(x))
#define LONG(x) ((x) / BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

/* Check if device supports button events (mouse/touch capabilities) */
int device_has_buttons(const char *device_path) {
    int fd;
    unsigned long evbit[NBITS(EV_MAX)];
    unsigned long keybit[NBITS(KEY_MAX)];

    fd = open(device_path, O_RDONLY);
    if (fd < 0)
        return 0;

    memset(evbit, 0, sizeof(evbit));
    memset(keybit, 0, sizeof(keybit));

    /* Get event types supported */
    if (ioctl(fd, EVIOCGBIT(0, EV_MAX), evbit) < 0) {
        close(fd);
        return 0;
    }

    /* Check if device supports KEY events */
    if (test_bit(EV_KEY, evbit)) {
        /* Get supported key/button events */
        if (ioctl(fd, EVIOCGBIT(EV_KEY, KEY_MAX), keybit) < 0) {
            close(fd);
            return 0;
        }

        /* Check for mouse buttons or touch events */
        if (test_bit(BTN_LEFT, keybit) ||
            test_bit(BTN_RIGHT, keybit) ||
            test_bit(BTN_MIDDLE, keybit) ||
            test_bit(BTN_MOUSE, keybit) ||
            test_bit(BTN_TOUCH, keybit)) {
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}

/* Find first mouse or touch input device */
int find_input_device(char *device_path, size_t path_size) {
    DIR *dir;
    struct dirent *entry;
    char name_path[256];
    char test_path[256];
    char name[256];
    FILE *fp;

    dir = opendir("/dev/input");
    if (!dir) {
        perror("Cannot open /dev/input");
        return -1;
    }

    printf("Scanning for input devices with button capabilities...\n");

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(test_path, sizeof(test_path), "/dev/input/%s", entry->d_name);
        snprintf(name_path, sizeof(name_path), "/sys/class/input/%s/device/name", entry->d_name);

        /* Get device name */
        fp = fopen(name_path, "r");
        if (fp) {
            if (fgets(name, sizeof(name), fp)) {
                name[strcspn(name, "\n")] = 0;
            } else {
                strcpy(name, "Unknown");
            }
            fclose(fp);
        } else {
            strcpy(name, "Unknown");
        }

        printf("  Checking %s: %s ... ", test_path, name);

        /* Check if device has button capabilities */
        if (device_has_buttons(test_path)) {
            printf("✓ HAS BUTTONS\n");
            snprintf(device_path, path_size, "%s", test_path);
            printf("Selected input device: %s (%s)\n", name, device_path);
            closedir(dir);
            return 0;
        } else {
            printf("✗ no buttons\n");
        }
    }

    closedir(dir);

    fprintf(stderr, "No suitable input device found with button capabilities\n");
    return -1;
}

/* Send quit command to mpv via IPC socket */
int send_quit_command(const char *socket_path) {
    int sock;
    struct sockaddr_un addr;
    const char *cmd = "{ \"command\": [\"quit\"] }\n";
    char response[BUFFER_SIZE];
    ssize_t n;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    if (write(sock, cmd, strlen(cmd)) < 0) {
        perror("write");
        close(sock);
        return -1;
    }

    /* Read response (optional) */
    n = read(sock, response, sizeof(response) - 1);
    if (n > 0) {
        response[n] = '\0';
        printf("mpv response: %s", response);
    }

    close(sock);
    printf("Quit command sent to mpv\n");
    return 0;
}

/* Check if socket file exists */
int socket_exists(const char *path) {
    return access(path, F_OK) == 0;
}

int main(int argc, char *argv[]) {
    int fd;
    struct input_event ev;
    char device_path[256];
    const char *socket_path;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mpv_socket_path>\n", argv[0]);
        return 1;
    }

    socket_path = argv[1];

    /* Wait for socket to be created (up to 10 seconds) */
    printf("Waiting for mpv socket: %s\n", socket_path);
    for (int i = 0; i < 100; i++) {
        if (socket_exists(socket_path))
            break;
        usleep(100000); /* 100ms */
    }

    if (!socket_exists(socket_path)) {
        fprintf(stderr, "mpv socket not found: %s\n", socket_path);
        fprintf(stderr, "Waited 10 seconds but socket was not created\n");
        return 1;
    }
    printf("Socket found, proceeding to find input device\n");

    /* Find input device */
    if (find_input_device(device_path, sizeof(device_path)) < 0) {
        fprintf(stderr, "No input device found\n");
        return 1;
    }

    /* Open input device */
    fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("Cannot open input device");
        fprintf(stderr, "Try: sudo usermod -a -G input $USER\n");
        fprintf(stderr, "Then logout and login again\n");
        return 1;
    }

    printf("Monitoring for button clicks/taps...\n");
    printf("Click or tap to quit video\n");

    /* Read input events */
    while (socket_exists(socket_path)) {
        ssize_t n = read(fd, &ev, sizeof(ev));

        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read");
            break;
        }

        if (n != sizeof(ev))
            continue;

        /* Filter for button press events only */
        if (ev.type == EV_KEY) {
            /* BTN_LEFT = 0x110 (272), BTN_RIGHT = 0x111, BTN_MIDDLE = 0x112 */
            /* BTN_TOUCH = 0x14a (330) */
            if ((ev.code >= BTN_MOUSE && ev.code < BTN_MOUSE + 16) ||
                (ev.code == BTN_TOUCH)) {

                /* Only act on button press (value=1), not release (value=0) */
                if (ev.value == 1) {
                    printf("Button press detected (code=%d)\n", ev.code);

                    if (send_quit_command(socket_path) == 0) {
                        break;
                    }
                }
            }
        }

        /* Ignore EV_REL (mouse movement) and EV_ABS (absolute position) */
    }

    close(fd);
    printf("Input monitor exiting\n");
    return 0;
}
