#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "vga_ball.h"

int vga_ball_fd;  // File descriptor for /dev/vga_ball

/* Set the background color via ioctl */
void set_background_color(const vga_ball_color_t *color) {
    vga_ball_arg_t arg;
    arg.background = *color;
    if (ioctl(vga_ball_fd, VGA_BALL_WRITE_BACKGROUND, &arg) < 0) {
        perror("ioctl(VGA_BALL_WRITE_BACKGROUND) failed");
    }
}

/* Set the ball position via ioctl */
void set_pos(const vga_ball_pos_t *pos) {
    if (ioctl(vga_ball_fd, VGA_BALL_WRITE_POS, pos) < 0) {
        perror("ioctl(VGA_BALL_WRITE_POS) failed");
    }
}

int main() {
    const char *device = "/dev/vga_ball";
    struct termios orig_tio, raw_tio;
    struct pollfd pfd;
    char buf[8];
    int ret;
    // Ball coordinates (X is fixed, Y will change)
    int x = 10;
    int y = 240;

    printf("VGA ball userspace program started (keyboard control mode)\n");

    // Open the vga_ball device file
    vga_ball_fd = open(device, O_RDWR);
    if (vga_ball_fd == -1) {
        perror("could not open /dev/vga_ball");
        return EXIT_FAILURE;
    }

    // Set the VGA background to black (for contrast with the yellow ball)
    vga_ball_color_t black = {0x00, 0x00, 0x00};
    set_background_color(&black);

    // Initialize the ball's position near the left edge (X fixed, Y centered)
    vga_ball_pos_t pos;
    pos.xcoor = x;
    pos.ycoor = y;
    set_pos(&pos);

    // Configure terminal for raw, non-blocking input (no line buffering, no echo)
    if (tcgetattr(STDIN_FILENO, &orig_tio) == -1) {
        perror("tcgetattr");
        close(vga_ball_fd);
        return EXIT_FAILURE;
    }
    raw_tio = orig_tio;
    raw_tio.c_lflag &= ~(ICANON | ECHO);   // turn off canonical mode and echo
    raw_tio.c_cc[VMIN]  = 0;
    raw_tio.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_tio) == -1) {
        perror("tcsetattr");
        close(vga_ball_fd);
        return EXIT_FAILURE;
    }

    printf("Use Up/Down arrow keys to move the ball (X fixed at %d). Press 'q' to quit.\n", x);

    // Set up polling on standard input for key presses
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    // Main loop: wait for and handle key presses
    while (1) {
        // Wait indefinitely for an input event (key press)
        ret = poll(&pfd, 1, -1);
        if (ret < 0) {
            perror("poll failed");
            break;
        }
        if (pfd.revents & POLLIN) {
            // Read available input bytes (arrow keys produce 3 bytes; others produce 1 byte)
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EAGAIN) continue;  // try again if no data
                perror("read failed");
                break;
            }
            // Process each byte/sequence in the input buffer
            for (int i = 0; i < n; ++i) {
                unsigned char c = buf[i];
                if (c == 0x1B) {
                    // Potential start of an escape sequence (arrow key)
                    if (i <= n - 3 && buf[i] == 0x1B && buf[i+1] == 0x5B) {
                        // We have ESC [ <code>
                        unsigned char code = buf[i+2];
                        if (code == 0x41) {  // 'A' = Up arrow
                            if (y > 0) y--;
                        } else if (code == 0x42) {  // 'B' = Down arrow
                            if (y < 479) y++;
                        }
                        // Update ball position after arrow key
                        pos.xcoor = x;
                        pos.ycoor = y;
                        set_pos(&pos);
                        // Skip the two extra bytes of the escape sequence
                        i += 2;
                    }
                    // (If the sequence was incomplete, it will be handled on the next read)
                } else if (c == 'q' || c == 'Q') {
                    // Quit command received
                    printf("Quit command received. Exiting...\n");
                    goto EXIT_LOOP;
                } else {
                    // Ignore any other character input
                }
            }
        }
    }

EXIT_LOOP:
    // Restore original terminal settings before exiting
    if (tcsetattr(STDIN_FILENO, TCSANOW, &orig_tio) == -1) {
        perror("tcsetattr restore");
    }
    close(vga_ball_fd);
    printf("VGA ball userspace program terminating\n");
    return 0;
}