// #include <stdio.h>
// #include <stdlib.h>
// #include <fcntl.h>
// #include <unistd.h>
// #include <errno.h>

// #define DEVICE_PATH "/dev/buf0"

// // Function to read 2 unsigned short values
// void read_data(int fd) {
//     unsigned short data[2];
//     ssize_t n = read(fd, data, sizeof(data));
//     if (n < 0) {
//         perror("Read failed");
//     } else if (n == 0) {
//         printf("No data available\n");
//     } else {
//         printf("Read %zd bytes: %hu %hu\n", n, data[0], data[1]);
//     }
// }

// // Function to write 2 unsigned short values
// void write_data(int fd) {
//     unsigned short data[2];
//     printf("Enter 2 unsigned short values: ");
//     if (scanf("%hu %hu", &data[0], &data[1]) != 2) {
//         while(getchar() != '\n'); // flush invalid input
//         printf("Invalid input\n");
//         return;
//     }

//     ssize_t n = write(fd, data, sizeof(data));
//     if (n < 0) {
//         perror("Write failed");
//     } else {
//         printf("Wrote %zd bytes\n", n);
//     }
// }

// int main() {
//     int fd = -1;
//     int choice, mode;
//     int access;

//     while (1) {
//         printf("\n--- BUF DRIVER TEST ---\n");
//         printf("1. Read\n");
//         printf("2. Write\n");
//         printf("3. Read/Write\n");
//         printf("0. Exit\n");
//         printf("Choice: ");
//         if (scanf("%d", &choice) != 1) { while(getchar() != '\n'); continue; }

//         if (choice == 0) break;

//         switch (choice) {
//             case 1: access = O_RDONLY; break;
//             case 2: access = O_WRONLY; break;
//             case 3: access = O_RDWR; break;
//             default: 
//                 printf("Invalid choice\n"); 
//                 continue;
//         }

//         printf("Select mode:\n1. Blocking\n2. Non-blocking\nChoice: ");
//         if (scanf("%d", &mode) != 1) { while(getchar() != '\n'); continue; }
//         if (mode == 2) access |= O_NONBLOCK;

//         fd = open(DEVICE_PATH, access);
//         if (fd < 0) { perror("Open failed"); continue; }

//         printf("Device opened ");
//         if (choice == 1) printf("for reading");
//         else if (choice == 2) printf("for writing");
//         else printf("for reading/writing");
//         printf(" %s\n", (mode == 2) ? "non-blocking" : "blocking");

//         if (choice == 1) read_data(fd);
//         else if (choice == 2) write_data(fd);
//         else {
//             // Read then write
//             read_data(fd);
//             write_data(fd);
//         }

//         close(fd);
//         fd = -1;
//         printf("Device closed\n");
//     }

//     return 0;
// }
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "../driver/buf_ioctl.h"

#define DEVICE_PATH "/dev/buf0"

// Function to read 2 unsigned short values
void read_data(int fd) {
    unsigned short data[2];
    ssize_t n = read(fd, data, sizeof(data));
    if (n < 0) {
        perror("Read failed");
    } else if (n == 0) {
        printf("No data available\n");
    } else {
        printf("Read %zd bytes: %hu %hu\n", n, data[0], data[1]);
    }
}

// Function to write 2 unsigned short values
void write_data(int fd) {
    unsigned short data[2];
    printf("Enter 2 unsigned short values: ");
    if (scanf("%hu %hu", &data[0], &data[1]) != 2) {
        while(getchar() != '\n'); // flush invalid input
        printf("Invalid input\n");
        return;
    }

    ssize_t n = write(fd, data, sizeof(data));
    if (n < 0) {
        perror("Write failed");
    } else {
        printf("Wrote %zd bytes\n", n);
    }
}

// Function to handle IOCTL queries
void ioctl_test(int fd) {
    int value;

    // Number of data items in the buffer
    if (ioctl(fd, BUF_IOCGETNUMDATA, &value) == 0)
        printf("Number of data items in buffer: %d\n", value);
    else
        perror("BUF_IOCGETNUMDATA failed");

    // Number of readers
    if (ioctl(fd, BUF_IOCGETNUMREADER, &value) == 0)
        printf("Number of readers: %d\n", value);
    else
        perror("BUF_IOCGETNUMREADER failed");

    // Buffer size
    if (ioctl(fd, BUF_IOCGETBUFSIZE, &value) == 0)
        printf("Buffer size: %d\n", value);
    else
        perror("BUF_IOCGETBUFSIZE failed");

    // Optionally resize the buffer
    printf("Enter new buffer size (0 to skip): ");
    if (scanf("%d", &value) != 1) { while(getchar() != '\n'); return; }
    if (value > 0) {
        if (ioctl(fd, BUF_IOCSETBUFSIZE, &value) == 0)
            printf("Buffer resized to %d\n", value);
        else
            perror("BUF_IOCSETBUFSIZE failed");
    }
}

int main() {
    int fd = -1;
    int choice, mode;
    int access;

    while (1) {
        printf("\n--- BUF DRIVER TEST ---\n");
        printf("1. Read\n");
        printf("2. Write\n");
        printf("3. Read/Write\n");
        printf("4. IOCTL Test\n");
        printf("0. Exit\n");
        printf("Choice: ");
        if (scanf("%d", &choice) != 1) { while(getchar() != '\n'); continue; }

        if (choice == 0) break;

        switch (choice) {
            case 1: access = O_RDONLY; break;
            case 2: access = O_WRONLY; break;
            case 3: access = O_RDWR; break;
            case 4: access = O_RDWR; break; // IOCTL needs at least read/write access
            default: 
                printf("Invalid choice\n"); 
                continue;
        }

        printf("Select mode:\n1. Blocking\n2. Non-blocking\nChoice: ");
        if (scanf("%d", &mode) != 1) { while(getchar() != '\n'); continue; }
        if (mode == 2) access |= O_NONBLOCK;

        fd = open(DEVICE_PATH, access);
        if (fd < 0) { perror("Open failed"); continue; }

        printf("Device opened ");
        if (choice == 1) printf("for reading");
        else if (choice == 2) printf("for writing");
        else if (choice == 3) printf("for reading/writing");
        else printf("for IOCTL");
        printf(" %s\n", (mode == 2) ? "non-blocking" : "blocking");

        switch (choice) {
            case 1: read_data(fd); break;
            case 2: write_data(fd); break;
            case 3: 
                read_data(fd);
                write_data(fd);
                break;
            case 4: 
                ioctl_test(fd);
                break;
        }

        close(fd);
        fd = -1;
        printf("Device closed\n");
    }

    return 0;
}