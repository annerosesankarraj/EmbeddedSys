#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
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

// Function to handle smooth animation for jumping and ducking
void animate_movement(vga_ball_pos_t *pos, int start_y, int target_y) {
    struct timespec start_time, current_time;
    double elapsed_time, animation_duration = 1.0; // 1 second for full animation
    int current_y;
    
    // Get the current time as start time
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    // Animation loop for 1 second
    do {
        // Get current time
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        
        // Calculate elapsed time in seconds
        elapsed_time = (current_time.tv_sec - start_time.tv_sec) + 
                      (current_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
        
        if (elapsed_time < animation_duration / 2) {
            // First half of animation - moving to target position
            double progress = elapsed_time / (animation_duration / 2);
            current_y = start_y + (int)((target_y - start_y) * progress);
        } else if (elapsed_time < animation_duration) {
            // Second half of animation - returning to starting position
            double progress = (elapsed_time - animation_duration / 2) / (animation_duration / 2);
            current_y = target_y + (int)((start_y - target_y) * progress);
        } else {
            // Animation complete
            current_y = start_y;
            break;
        }
        
        // Update ball position
        pos->ycoor = current_y;
        set_pos(pos);
        
        // Small delay to control animation framerate
        usleep(16667); // ~60fps (1/60 second)
        
    } while (elapsed_time < animation_duration);
    
    // Ensure the ball is back at the starting position
    pos->ycoor = start_y;
    set_pos(pos);
}

int main() {
    const char *device = "/dev/vga_ball";
    struct termios orig_tio, raw_tio;
    struct pollfd pfd;
    char buf[8];
    int ret;
    
    // Ball starting coordinates (leftmost, 4 tiles from bottom)
    // Assuming screen is 640x480 and tiles are 32x32
    int x = 16; // Center of leftmost column
    int y = 480 - (4 * 32) - 16; // 4 tiles up from bottom (center of tile)
    
    // Base position (where the ball returns to after jumps/ducks)
    int base_y = y;
    
    // Flag to track if an animation is in progress
    int animation_in_progress = 0;

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

    // Initialize the ball's position
    vga_ball_pos_t pos;
    pos.xcoor = x;
    pos.ycoor = y;
    set_pos(&pos);

    // Configure terminal for raw, non-blocking input
    if (tcgetattr(STDIN_FILENO, &orig_tio) == -1) {
        perror("tcgetattr");
        close(vga_ball_fd);
        return EXIT_FAILURE;
    }
    raw_tio = orig_tio;
    raw_tio.c_lflag &= ~(ICANON | ECHO);
    raw_tio.c_cc[VMIN]  = 0;
    raw_tio.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_tio) == -1) {
        perror("tcsetattr");
        close(vga_ball_fd);
        return EXIT_FAILURE;
    }

    printf("Use Up arrow to jump, Down arrow to duck. Press 'q' to quit.\n");

    // Set up polling on standard input for key presses
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    // Main loop: wait for and handle key presses
    while (1) {
        // Wait for an input event (key press) with a short timeout to allow animations
        ret = poll(&pfd, 1, 10); // 10ms timeout for responsive animations
        
        if (ret < 0) {
            perror("poll failed");
            break;
        }
        
        if (pfd.revents & POLLIN) {
            // Read available input
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EAGAIN) continue;
                perror("read failed");
                break;
            }
            
            // Process input only if no animation is currently running
            if (!animation_in_progress) {
                // Process each byte/sequence in the input buffer
                for (int i = 0; i < n; ++i) {
                    unsigned char c = buf[i];
                    if (c == 0x1B) {
                        // Potential start of an escape sequence (arrow key)
                        if (i <= n - 3 && buf[i] == 0x1B && buf[i+1] == 0x5B) {
                            // We have ESC [ <code>
                            unsigned char code = buf[i+2];
                            if (code == 0x41) {  // 'A' = Up arrow - Jump
                                animation_in_progress = 1;
                                
                                // Jump up 32*32 pixels (actually jumping up by 32 pixels)
                                animate_movement(&pos, base_y, base_y - 32);
                                
                                animation_in_progress = 0;
                            } else if (code == 0x42) {  // 'B' = Down arrow - Duck
                                animation_in_progress = 1;
                                
                                // Duck down 32*32 pixels (actually ducking down by 32 pixels)
                                animate_movement(&pos, base_y, base_y + 32);
                                
                                animation_in_progress = 0;
                            }
                            // Skip the two extra bytes of the escape sequence
                            i += 2;
                        }
                    } else if (c == 'q' || c == 'Q') {
                        // Quit command received
                        printf("Quit command received. Exiting...\n");
                        goto EXIT_LOOP;
                    }
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
