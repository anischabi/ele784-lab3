#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define READWRITE_BUFSIZE 16
#define DEFAULT_BUFSIZE 256

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Anis Chabi");
MODULE_DESCRIPTION("Pilote ring buffer - ELE784 Lab3");

/* Global device numbers */
int buf_major = 0;  // 0 means dynamic allocation
int buf_minor = 0;  // starting minor number


/* Déclarations des fonctions du pilote */
int buf_init(void);
void buf_exit(void);
int buf_open(struct inode *inode, struct file *filp);
int buf_release(struct inode *inode, struct file *filp);
ssize_t buf_read(struct file *filp, char __user *ubuf,size_t count, loff_t *f_pos);
ssize_t buf_write(struct file *filp, const char __user *ubuf,size_t count, loff_t *f_pos);
long buf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
module_init(buf_init);
module_exit(buf_exit);

/* Structure du tampon circulaire */
struct BufStruct {
  unsigned int InIdx; /* Index d'écriture */
  unsigned int OutIdx; /* Index de lecture */
  unsigned short BufFull; /* Drapeau: tampon plein */
  unsigned short BufEmpty; /* Drapeau: tampon vide */
  unsigned int BufSize; /* Taille du tampon */
  unsigned short *Buffer; /* Pointeur vers les données */
} Buffer;

/* Structure du dispositif */
struct Buf_Dev {
  unsigned short *ReadBuf; /* Tampon local lecture */
  unsigned short *WriteBuf; /* Tampon local écriture */
  struct semaphore SemBuf; /* Sémaphore de protection */
  wait_queue_head_t InQueue; /* File attente écriture */
  wait_queue_head_t OutQueue; /* File attente lecture */
  unsigned short numWriter; /* Nombre d'écrivains */
  unsigned short numReader; /* Nombre de lecteurs */
  dev_t dev; /* Numéro de device  (major,minor)*/ 
  struct cdev cdev; /* Structure cdev (Character device structure) */
  struct class *class; /* Classe du device :  Device class (/sys/class)*/
} BDev; //The single instance of the buffer character device managed by this driver.


/* Table des opérations */
struct file_operations Buf_fops = {
  .owner = THIS_MODULE,
  .open = buf_open,
  .release = buf_release,
  .read = buf_read,
  .write = buf_write,
  .unlocked_ioctl = buf_ioctl,
};

/* Function prototypes */
int BufIn(struct BufStruct *Buf, unsigned short *Data);
int BufOut(struct BufStruct *Buf, unsigned short *Data);

/* Fonction d'insertion dans le buffer (une donnée à la fois) */
int BufIn(struct BufStruct *Buf, unsigned short *Data) {
  //Vérifier si le buffer est plein
  if (Buf->BufFull)
    return -1; // Si le buffer est plein, on ne peut rien ajouter : retourne -1

  //Dès qu'on ajoute une donnée, le buffer n'est plus vide 
  Buf->BufEmpty = 0;
  // Insérer la donnée , Copie la valeur pointée par Data dans le buffer à la position InIdx
  Buf->Buffer[Buf->InIdx] = *Data;
  //Avancer l'index (circulaire) : L'opération % BufSize rend le buffer circulaire !
  Buf->InIdx = (Buf->InIdx + 1) % Buf->BufSize;

  // Vérifier si le buffer est maintenant plein :  Quand le buffer est-il plein ?
  // Quand l'index d'écriture (InIdx) rattrape l'index de lecture (OutIdx)
  if (Buf->InIdx == Buf->OutIdx)
    Buf->BufFull = 1;

  return 0;
}

/* Fonction d'extraction du buffer (une donnée à la fois) */
int BufOut(struct BufStruct *Buf, unsigned short *Data) {
  //Vérifier si le buffer est vide
  if (Buf->BufEmpty)
    return -1; //Si le buffer est vide, on ne peut rien lire : retourne -1

  // Dès qu'on retire une donnée, il y a de la place : le buffer ne peut plus être plein !
  Buf->BufFull = 0;
  //Extraire la donnée : Copie la valeur du buffer (à la position OutIdx) vers la variable pointée par Data
  *Data = Buf->Buffer[Buf->OutIdx];
  //Avancer l'index de lecture (circulaire)
  Buf->OutIdx = (Buf->OutIdx + 1) % Buf->BufSize;

  //Vérifier si le buffer est maintenant vide
  if (Buf->OutIdx == Buf->InIdx)
    Buf->BufEmpty = 1;
  
  return 0;
}


int buf_init(void) {
  //If you set scull_major manually (e.g., in module parameters),
  //this function uses that fixed number; if not, it will dynamically allocate one in few lines.
  dev_t devno = MKDEV(buf_major, buf_minor);
  int result;

  //Case 1 — Static Major : If buf_major is already set (non-zero), we assume the developer chose a fixed major number (e.g., 240).
  if (buf_major) { 
    // macro that combines the major and minor numbers into a single dev_t value.
    // devno now represents the full device number for your driver instance.
    devno = MKDEV(buf_major, buf_minor); 
    //tells the kernel that this major/minor number range is now owned by your driver.
    result = register_chrdev_region(devno, 1, "buf");
  } else { 
    // case 2 : dynamic major number allocation (buf_major == 0)
    // That means the user did not specify a major number, so the kernel must pick a free one automatically
    //This function asks the kernel: “Please give me a free major number and reserve a range of minor numbers for my device.”
    result = alloc_chrdev_region(&devno, buf_minor, 1, "buf");
    //After this call, devno contains the full device number assigned by the kernel, e.g., (245, 0).
    // extracts the major number from the dev_t returned by the kernel.
    buf_major = MAJOR(devno);
  }

  // Negative = some error (e.g., -EBUSY, -EINVAL, -ENOMEM)
  // Did the registration/allocation fail?
  if (result < 0) {
    printk(KERN_WARNING "buf : (buf_init) can't get major %d\n", buf_major);
    return result;
  }

  // --- Initialize the Buffer structure ---
  Buffer.InIdx = 0;
  Buffer.OutIdx = 0;
  Buffer.BufFull = 0;
  Buffer.BufEmpty = 1;
  Buffer.BufSize = DEFAULT_BUFSIZE;
  //Allocate memory for the actual storage of the buffer.
  Buffer.Buffer = kmalloc(Buffer.BufSize * sizeof(unsigned short), GFP_KERNEL);
  // Check if the memory allocation failed.
  if (!Buffer.Buffer) { //If kmalloc returns NULL, allocation failed (not enough memory).
      //We clean up by unregistering the device numbers
      unregister_chrdev_region(devno, 1);
      // standard Linux error code for "out of memory"
      printk(KERN_WARNING "buf : (buf_init) memory allocation error for Buffer.Buffer\n");
      return -ENOMEM;
  }

  // --- Initialize BDev structure ---
  //Initializes the semaphore SemBuf inside BDev. 1 means it’s a binary semaphore (can act like a mutex)
  sema_init(&BDev.SemBuf, 1); 
  //initializes the wait queues for reading/writing.
  init_waitqueue_head(&BDev.InQueue); // processes waiting to write when the buffer is full.
  init_waitqueue_head(&BDev.OutQueue); // processes waiting to read when the buffer is empty.
  // initialize counters for the number of readers/writers currently using the device.
  // Useful for bookkeeping and possibly for multi-process access management.
  BDev.numReader = 0;
  BDev.numWriter = 0;
  //Stores the device number (major + minor) that was allocated or registered earlier in the BDev structure.
  //This is used later when creating the cdev and device in /dev.
  BDev.dev = devno;

  //  Initialize ReadBuf and WriteBuf to NULL (lazy allocation)
  BDev.ReadBuf = NULL;   // Will be allocated in buf_read() if needed
  BDev.WriteBuf = NULL;  // Will be allocated in buf_write() if needed


  // --- Create device class. Purpose: Prepare the kernel infrastructure so /dev/buf0 can exist.---
  //Creates a device class in the kernel.
  //This class is used to group devices in /sys/class/ and allows udev to automatically create /dev entries
  // First argument: THIS_MODULE → ties the class lifetime to your module.
  // Second argument: "buf_class" → name of the class in /sys/class/
  BDev.class = class_create("buf_class");
  //Checks if class_create returned an error pointer. 
  //Kernel functions often return pointers for success and “error pointers” for failures.
  if (IS_ERR(BDev.class)) { 
      //If it failed, you clean up: free the buffer and unregister the device number.
      kfree(Buffer.Buffer);
      unregister_chrdev_region(devno, 1);
      // Converts the error pointer to a negative error code (-ENOMEM, -EINVAL, etc.) to return from buf_init().
      printk(KERN_WARNING "buf: (buf_init) error to create buf_class\n");
      return PTR_ERR(BDev.class);
  }

  // --- Create the device /dev/buf0 . Purpose: Make the device accessible to user-space programs via /dev/buf0.---
  //device_create() : creates the device entry in /dev/
  // Arguments:
  // BDev.class -> the class we just created
  // NULL       -> parent device (none here)
  // devno      -> the device number (major+minor)
  // NULL       -> device-specific data (not used here)
  // "buf0"     -> the name of the device in /dev/
  if (!device_create(BDev.class, NULL, devno, NULL, "buf0")) { //device_create() returns NULL on failure
      // If it fails, we clean up everything we created so far: destroy the class, free the buffer, unregister device number.
      class_destroy(BDev.class);
      kfree(Buffer.Buffer);
      unregister_chrdev_region(devno, 1);
      //Return -EINVAL to indicate failure.
      printk(KERN_WARNING "buf: (buf_init) error to create device buf0\n");
      return -EINVAL;
  }


  //the heart of connecting your kernel module to the Linux device system
  //This initializes a struct cdev (character device) inside your BDev structure.
  //It sets up the link: Kernel → knows which functions to call when user-space does open/read/write on /dev/buf0.
  cdev_init(&BDev.cdev, &Buf_fops);
  //This marks your module as the owner of this device.
  BDev.cdev.owner = THIS_MODULE;
  BDev.cdev.ops = &Buf_fops;
  //This registers the device with the kernel.
  //The kernel links your /dev/buf0 to your struct cdev.
  //devno is the (major, minor) pair that uniquely identifies your device.
  // The last argument (1) is the count → number of consecutive minor numbers to register (here only 1 device).
  //So after this call, the kernel officially knows: “Major X, minor Y -> use these operations (Buf_fops) when accessed.”
  result = cdev_add (&BDev.cdev, devno, 1);
  if (result) {
      printk(KERN_NOTICE "buf: Error %d adding cdev\n", result);
      /* undo device/class/buffer/major allocation done previously */
      device_destroy(BDev.class, devno); // removes /dev/buf0
      class_destroy(BDev.class);// removes /sys/class/buf_class
      kfree(Buffer.Buffer); // free kernel buffer memory
      unregister_chrdev_region(devno, 1); // release major/minor numbers
      printk(KERN_WARNING "buf: (buf_init) error to add the char driver\n");
      return result; // propagate kernel-style error code up
  }

  /* success */
  printk(KERN_INFO "buf: module loaded, major=%d\n", buf_major);
  return 0;
}


void buf_exit(void) {
  dev_t devno = BDev.dev;
  /* --- Remove character device --- */
  cdev_del(&BDev.cdev);
  /* --- Destroy device node /dev/buf0 --- */
  device_destroy(BDev.class, devno);
  /* --- Destroy device class --- */
  class_destroy(BDev.class);
  /* --- Free allocated buffer memory --- */
  kfree(Buffer.Buffer);

  /* Free ReadBuf and WriteBuf if they were allocated --- */
  if (BDev.ReadBuf) {
    kfree(BDev.ReadBuf);
    BDev.ReadBuf = NULL;  // Good practice
  }
  if (BDev.WriteBuf) {
    kfree(BDev.WriteBuf);
    BDev.WriteBuf = NULL;  // Good practice
  }
  /* --- Release major/minor numbers --- */
  unregister_chrdev_region(devno, 1);
  printk(KERN_INFO "buf: module unloaded\n");
}

  
int buf_open(struct inode *inode, struct file *filp) {

  // 1. Extract the access mode from f_flags
  int mode = filp->f_flags & O_ACCMODE;
  // 2. Acquire the semaphore to protect shared data (BDev counters)
  if (down_interruptible(&BDev.SemBuf)){
    printk(KERN_WARNING "buf: (buf_open) interrupted while waiting for semaphore\n");
    return -ERESTARTSYS;
  }
  // 3. Writer access control
  if (mode == O_WRONLY || mode == O_RDWR) {
    if (BDev.numWriter > 0) {
      // Only one writer allowed at a time
      up(&BDev.SemBuf); // release semaphore before returning
      printk(KERN_WARNING "buf: (buf_open) already opened in writing\n");
      return -EBUSY;    // device busy
    }
    BDev.numWriter++; // increment writer count
    // Allocate WriteBuf if not already allocated
    if (!BDev.WriteBuf) {
        BDev.WriteBuf = kmalloc(READWRITE_BUFSIZE * sizeof(unsigned short), GFP_KERNEL);
        if (!BDev.WriteBuf) {
            up(&BDev.SemBuf);
            printk(KERN_WARNING "buf: (buf_open) failed to allocate WriteBuf\n");
            return -ENOMEM;
        }
    }
  }
    // 4. Handle reader access
  if (mode == O_RDONLY || mode == O_RDWR) {
    BDev.numReader++; // increment reader count
    // Allocate ReadBuf if not already allocated
    if (!BDev.ReadBuf) {
      BDev.ReadBuf = kmalloc(READWRITE_BUFSIZE * sizeof(unsigned short), GFP_KERNEL);
      if (!BDev.ReadBuf) {
        up(&BDev.SemBuf);
        printk(KERN_WARNING "buf: (buf_open) failed to allocate ReadBuf\n");
        return -ENOMEM;
      }
    }
  }
  // 5. Store device pointer in private_data for future use in read/write
  // file->private_data allows file operations (read/write/ioctl) to access BDev without global lookup.
  filp->private_data = &BDev;
  // 6. Release the semaphore
  up(&BDev.SemBuf);
  printk(KERN_INFO "buf: open\n");
  return 0;
}

int buf_release(struct inode *inode, struct file *filp) {
  // 1. Retrieve BDev from filp->private_data
  struct Buf_Dev *dev = filp->private_data;
  // 2. Acquire the semaphore to protect shared data
  // If another process holds it, the current process sleep s.
  // We use it to prevent race conditions when updating counters.
  if (down_interruptible(&dev->SemBuf)) {
    printk(KERN_WARNING "buf: (buf_release) interrupted while waiting for semaphore\n");
    return -ERESTARTSYS;
  }
  // 3. Decrement numWriter and/or numReader depending on f_mode
  if (filp->f_mode & FMODE_WRITE)
    dev->numWriter--;
  if (filp->f_mode & FMODE_READ)
    dev->numReader--;
  // 4. Release the semaphore. up() increments the semaphore count and wakes any waiting processes.
  up(&dev->SemBuf);
  
  printk(KERN_INFO "buf: release\n");
  return 0;
}

ssize_t buf_read(struct file *filp, char __user *ubuf, size_t count, loff_t *f_pos) {
  struct Buf_Dev *dev = filp->private_data;
  size_t total_bytes_read = 0;           // Total bytes transferred
  size_t requested_bytes_this_iter;            // Bytes to read in current iteration
  int requested_items_this_iter;
  unsigned short data;             // Temporary storage for one data item
  int i, result;
  // Update actual bytes read in this iteration
  int items_read_this_iter;  
  size_t bytes_read_this_iter;
  
  // 1. Check for non-blocking mode
  int nonblocking = filp->f_flags & O_NONBLOCK;

  // Calculate total bytes requested (count is in bytes, data is unsigned short)
  // Make sure count is aligned to unsigned short size
  if (count % sizeof(unsigned short) != 0) {
    printk(KERN_WARNING "buf: (buf_read) Invalid size, must be multiple of sizeof(unsigned short)\n");
    return -EINVAL;  // Invalid size, must be multiple of sizeof(unsigned short)
  }

  // Main loop - continue until all requested data is transferred
  while ( total_bytes_read< count) {

    // 2.a. Attempt to acquire semaphore
    if (down_interruptible(&dev->SemBuf)) {
      printk(KERN_WARNING "buf: (buf_read) interrupted while waiting for semaphore\n");
      // Interrupted by signal
      if ( total_bytes_read> 0)
        return total_bytes_read;  // Return what we've read so far
      return -ERESTARTSYS;
    }
      
    // 2.b. Check if buffer is empty
    if (Buffer.BufEmpty) {
      // Release semaphore
      up(&dev->SemBuf);
      // If non-blocking mode, return immediately
      if (nonblocking) {
        printk(KERN_WARNING "buf: (buf_read) buffer is empty in non-blocking mode, return immediately\n");
        if ( total_bytes_read> 0)
          return total_bytes_read;  // Return what we've read so far
        return -EAGAIN;
      }
      // Blocking mode: sleep until data is available
      // wait_event_interruptible returns 0 if condition became true,
      // or -ERESTARTSYS if interrupted by signal
      if (wait_event_interruptible(dev->OutQueue, !Buffer.BufEmpty)) {
        printk(KERN_WARNING "buf: (buf_read) buffer is empty in blocking mode. Waiting was interrupted by a signal\n");
        // Interrupted by signal
        if ( total_bytes_read> 0)
          return total_bytes_read;  // Return what we've read so far
        return -ERESTARTSYS;
      }
      // Loop back to try again (acquire semaphore and check buffer)
      //Le continue saute immédiatement au début de la boucle while (réévalue la condition).
      continue;
    }

    // 2.c. Buffer has data - fill ReadBuf
    // Determine how many items to read in this iteration
    requested_bytes_this_iter = min(count - total_bytes_read, (size_t)(READWRITE_BUFSIZE * sizeof(unsigned short)));
    requested_items_this_iter = requested_bytes_this_iter / sizeof(unsigned short);

    // Extract data from circular buffer into ReadBuf
    for (i = 0; i < requested_items_this_iter; i++) {
      result = BufOut(&Buffer, &data);
      if (result < 0) {
        // Buffer became empty (shouldn't happen, but handle it)
        printk(KERN_WARNING "buf : (buf_read) Buffer is empty in extraction data from circular buffer\n"); 
        break;
      }
      dev->ReadBuf[i] = data;
    }
    // Update actual bytes read in this iteration
    items_read_this_iter = i;
    bytes_read_this_iter = items_read_this_iter * sizeof(unsigned short);
    // Wake up any waiting writers (buffer now has space)
    wake_up_interruptible(&dev->InQueue);
    // Release semaphore
    up(&dev->SemBuf);

    // 2.d. Copy ReadBuf to user space
    if (bytes_read_this_iter > 0) {// Protège contre le cas où aucune donnée n'a été lue
      if (copy_to_user((void __user *)(ubuf + total_bytes_read), dev->ReadBuf, bytes_read_this_iter)) {
        printk(KERN_WARNING "buf : (buf_read) copy to user space failed\n"); 
        // Copy failed
        if ( total_bytes_read> 0)
            return total_bytes_read;  // Return what we've successfully read
        return -EFAULT;
      }
      total_bytes_read += bytes_read_this_iter; //Mise à jour du compteur : Incrémente le total d'octets transférés avec succès.
    }

    // 2.e. Check if all requested data has been transferred (Vérifier si on doit continuer)
    //Condition 1 : total_bytes_read >= count (On a lu tout ce que l'utilisateur a demandé.)
    // Condition 2 : items_read_this_iter == 0 (Le buffer circulaire est vide, on ne peut plus rien lire.)
    if ( total_bytes_read>= count || items_read_this_iter == 0) {
        break;  // Done or buffer is now empty
    }

    // Continue loop for next block if more data is requested  
  }  
  
  // 3. Return total bytes transferred
  printk(KERN_INFO "buf: (buf_read) read %zu bytes\n", total_bytes_read);
  return total_bytes_read;
}

ssize_t buf_write(struct file *filp, const char __user *ubuf, size_t count, loff_t *f_pos) {

  struct Buf_Dev *dev = filp->private_data;
  size_t total_bytes_written = 0; // total bytes transferred
  size_t requested_bytes_this_iter;
  int requested_items_this_iter;
  unsigned short data;
  int result;
  size_t items_written_this_iter;
  size_t bytes_written_this_iter;

  // Check for non-blocking mode
  int nonblocking = filp->f_flags & O_NONBLOCK;

  // Validate alignment: count must be multiple of sizeof(unsigned short)
  if (count % sizeof(unsigned short) != 0) {
    printk(KERN_WARNING "buf: (buf_write) Invalid size, must be multiple of sizeof(unsigned short)\n");
    return -EINVAL;
  }

  // Main loop: continue until all user data is written
  while (total_bytes_written < count) {
    // Step 1: Copy a chunk from user space into WriteBuf
    // Determine how many bytes to copy this iteration (max = WRITEBUF_SIZE)
    requested_bytes_this_iter = min(count - total_bytes_written,(size_t)(READWRITE_BUFSIZE * sizeof(unsigned short)));
    requested_items_this_iter = requested_bytes_this_iter / sizeof(unsigned short);
    // Copy
    if (copy_from_user(dev->WriteBuf, (const void __user *)(ubuf + total_bytes_written), requested_bytes_this_iter)) {
      printk(KERN_WARNING "buf: (buf_write) copy from user space failed\n");
      if (total_bytes_written > 0)
        return total_bytes_written;
      return -EFAULT;
    }

    items_written_this_iter = 0;

    // Step 2: Loop to insert data from WriteBuf into circular buffer
    while (items_written_this_iter < requested_items_this_iter) {
      // 2.a. Acquire semaphore
      if (down_interruptible(&dev->SemBuf)) {
        printk(KERN_WARNING "buf: (buf_write) interrupted while waiting for semaphore\n");
        if (total_bytes_written > 0)
          return total_bytes_written;
        return -ERESTARTSYS;
      }

      // 2.b. Check if circular buffer is full
      if (Buffer.BufFull) {
        // 2.b.1 Release semaphore
        up(&dev->SemBuf);
        // 2.b.2 nonblocking mode: return immediately
        if (nonblocking) {
          printk(KERN_WARNING "buf: (buf_write) buffer full in non-blocking mode. return immediately\n");
          return total_bytes_written > 0 ? total_bytes_written : -EAGAIN;
        }
        // 2.b.3 Blocking mode: sleep until buffer has space
        if (wait_event_interruptible(dev->InQueue, !Buffer.BufFull)) {
          printk(KERN_WARNING "buf: (buf_write) buffer is full in blocking mode. Waiting was interrupted by a signal\n");
          return total_bytes_written > 0 ? total_bytes_written : -ERESTARTSYS;
        }
        continue; // retry acquiring semaphore
      }


      // 2.c. Buffer has space: 
      // 2.c.1 insert data from WriteBuf into circular buffer
      while (items_written_this_iter < requested_items_this_iter && !Buffer.BufFull) {
        data = dev->WriteBuf[items_written_this_iter];
        result = BufIn(&Buffer, &data);
        if (result < 0) {
          // Should not happen, but safety check
          printk(KERN_WARNING "buf: (buf_write) Buffer full during insertion\n");
          break;
        }
        items_written_this_iter++;
      }

      // 2.c.2 Wake up any readers waiting
      wake_up_interruptible(&dev->OutQueue);

      // 2.c.3 Release semaphore
      up(&dev->SemBuf);
    }

    // Update total bytes transferred
    bytes_written_this_iter = items_written_this_iter * sizeof(unsigned short);
    total_bytes_written += bytes_written_this_iter;

  }
  
  printk(KERN_INFO "buf: (buf_write) write %zu bytes\n", total_bytes_written);
  return total_bytes_written;
}

long buf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
  return 0; // stub
}
