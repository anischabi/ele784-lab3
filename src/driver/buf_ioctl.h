#ifndef BUF_IOCTL_H
#define BUF_IOCTL_H

#include <linux/ioctl.h>  // _IOR, _IOW, _IORW : macros are used to define IOCTL command numbers and their directions (read, write, or both).
//we need these macros to create the IOCTL constants that userspace programs and the driver both understand.

/* Magic number for our device */
#define BUF_IOC_MAGIC 'b'
// Each IOCTL device has a magic number, a unique character, to avoid conflicts with other devices.
// Ensures the kernel can check that the IOCTL request is meant for this device and not another.

// _IOR : Read from kernel (user space reads, kernel writes data to user space).
// _IOW : Write to kernel (user space writes, kernel reads data from user space).
// Each macro has three parameters:
// 1. Magic number : your device’s magic number. (BUF_IOC_MAGIC here)
// 2. Command number : unique command numbers for each IOCTL (0, 1, 2, 3 here)
// 3. Data type : the type of data being passed (int here).
#define BUF_IOCGETNUMDATA    _IOR(BUF_IOC_MAGIC, 0, int)  /* user program reads number of data items in the buffer.*/
#define BUF_IOCGETNUMREADER  _IOR(BUF_IOC_MAGIC, 1, int)  /* user reads how many readers are currently open.*/
#define BUF_IOCGETBUFSIZE    _IOR(BUF_IOC_MAGIC, 2, int)  /* user reads current buffer size.*/
#define BUF_IOCSETBUFSIZE    _IOW(BUF_IOC_MAGIC, 3, int)  /* user writes new buffer size to kernel.*/

// The maximum command number defined for this device.
// Useful in your buf_ioctl() function to validate commands
// Ensures the user doesn’t call undefined IOCTL commands.
#define BUF_IOC_MAXNR 3 /* highest command number */

#endif /* BUF_IOCTL_H */