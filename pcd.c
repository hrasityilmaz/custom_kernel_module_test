#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/errno.h> // For error codes like -EINVAL

#undef pr_fmt
#define pr_fmt(fmt) "%s:%s: " fmt, __func__, KBUILD_MODNAME

#define DEV_MEM_SIZE 512

// pseudo device's memory
char device_buffer[DEV_MEM_SIZE];

// hold the device number
dev_t device_number;

struct cdev pcd_cdev;

loff_t pcd_lseek(struct file *filp, loff_t off, int whence);
ssize_t pcd_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos);
ssize_t pcd_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos);
int pcd_open(struct inode *inode, struct file *flip);
int pcd_release(struct inode *inode, struct file *flip);

// File operations structure
struct file_operations pcd_fops =
    {
        .owner = THIS_MODULE,
        .open = pcd_open,
        .read = pcd_read,
        .write = pcd_write,
        .llseek = pcd_lseek,
        .release = pcd_release,
};

struct class *class_pcd;
struct device *device_pcd;

loff_t pcd_lseek(struct file *filp, loff_t off, int whence)
{
    loff_t temp_f_pos = 0; // Use a temporary variable for the new position
    pr_info("pcd_lseek called - Current f_pos: %lld, off: %lld, whence: %d\n",
            filp->f_pos, off, whence);

    switch (whence)
    {
    case SEEK_SET:
        temp_f_pos = off;
        break;

    case SEEK_CUR:
        temp_f_pos = filp->f_pos + off;
        break;

    case SEEK_END:
        temp_f_pos = DEV_MEM_SIZE + off;
        break;

    default:
        return -EINVAL;
    }

    if (temp_f_pos < 0 || temp_f_pos > DEV_MEM_SIZE)
    {
        pr_err("Invalid seek offset. New position %lld out of bounds [0, %d]\n",
               temp_f_pos, DEV_MEM_SIZE);
        return -EINVAL; // Invalid argument
    }

    filp->f_pos = temp_f_pos;
    pr_info("New file position: %lld\n", filp->f_pos);

    return filp->f_pos; // Return the new file position
}

ssize_t pcd_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos)
{
    pr_info("pcd_read called\n");
    pr_info("Current file position: %lld\n", *f_pos);

    if ((*f_pos + count) > DEV_MEM_SIZE)
    {
        count = DEV_MEM_SIZE - *f_pos;
    }

    if (count <= 0)
    {
        pr_info("No bytes to read (at end of device or invalid count).\n");
        return 0; // Return 0 bytes read
    }

    if (copy_to_user(buff, &device_buffer[*f_pos], count))
    {
        pr_err("Failed to copy data to user space.\n");
        return -EFAULT; // Bad address
    }

    // update the file position
    *f_pos += count;

    pr_info("Successfully read %zu bytes\n", count);
    pr_info("Updated file position: %lld\n", *f_pos);

    return count; // Return number of bytes successfully read
}

ssize_t pcd_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
    pr_info("pcd_write called\n");
    pr_info("Current file position: %lld\n", *f_pos);

    // Calculate available space from current position to end of device buffer
    if ((*f_pos + count) > DEV_MEM_SIZE)
    {
        count = DEV_MEM_SIZE - *f_pos;
    }

    if (count <= 0)
    {
        pr_info("No space to write (device full or invalid count).\n");
        return -ENOSPC; // No space left on device
    }

    if (copy_from_user(&device_buffer[*f_pos], buff, count))
    {
        pr_err("Failed to copy data from user space.\n");
        return -EFAULT; // Bad address
    }

    *f_pos += count;

    pr_info("Successfully wrote %zu bytes\n", count);
    pr_info("Updated file position: %lld\n", *f_pos);

    return count; // Return number of bytes successfully written
}

int pcd_open(struct inode *inode, struct file *filp) // Corrected: use filp for consistency
{
    pr_info("pcd_open called\n");

    return 0;
}

int pcd_release(struct inode *inode, struct file *filp) // Corrected: use filp for consistency
{
    pr_info("pcd_release called\n");

    return 0;
}

static int __init pcd_driver_init(void)
{
    int ret;

    pr_info("Initializing Pseudo Character Device (PCD) Driver...\n");

    // 1. Dynamic allocation of a device number
    ret = alloc_chrdev_region(&device_number, 0, 1, "pcd_devices");
    if (ret < 0)
    {
        pr_err("Failed to allocate device number\n");
        return ret;
    }
    pr_info("Device number allocated: %d:%d\n", MAJOR(device_number), MINOR(device_number));

    // 2. Initialize the cdev structure with file operations
    cdev_init(&pcd_cdev, &pcd_fops);
    // pcd_cdev.owner = THIS_MODULE; // Already set in file_operations struct for consistency

    // 3. Add the cdev to the kernel (register with VFS)
    ret = cdev_add(&pcd_cdev, device_number, 1);
    if (ret < 0)
    {
        pr_err("Failed to add cdev\n");
        unregister_chrdev_region(device_number, 1); // Clean up allocated device number
        return ret;
    }
    pr_info("Cdev added successfully.\n");

    // 4. Create device class under /sys/class
    class_pcd = class_create("pcd_class");
    if (IS_ERR(class_pcd))
    { // Check for errors during class creation
        pr_err("Failed to create device class\n");
        cdev_del(&pcd_cdev);                        // Clean up cdev
        unregister_chrdev_region(device_number, 1); // Clean up device number
        return PTR_ERR(class_pcd);                  // Return the error code
    }
    pr_info("Device class created successfully.\n");

    // 5. Create device file under /dev/ (and link to /sys/class/pcd_class/)
    device_pcd = device_create(class_pcd, NULL, device_number, NULL, "pcd");
    if (IS_ERR(device_pcd))
    {
        pr_err("Failed to create device file\n");
        class_destroy(class_pcd);                   // Clean up class
        cdev_del(&pcd_cdev);                        // Clean up cdev
        unregister_chrdev_region(device_number, 1); // Clean up device number
        return PTR_ERR(device_pcd);                 // Return the error code
    }
    pr_info("Device file '/dev/pcd' created successfully.\n");

    pr_info("Module init was successful\n");
    return 0;
}

static void __exit pcd_driver_cleanup(void)
{
    pr_info("Cleaning up Pseudo Character Device (PCD) Driver...\n");

    // 1. Destroy device file
    if (device_pcd)
    { // Check if device was created before destroying
        device_destroy(class_pcd, device_number);
        pr_info("Device file destroyed.\n");
    }

    // 2. Destroy device class
    if (class_pcd)
    { // Check if class was created before destroying
        class_destroy(class_pcd);
        pr_info("Device class destroyed.\n");
    }

    // 3. Delete cdev
    cdev_del(&pcd_cdev);
    pr_info("Cdev deleted.\n");

    // 4. Unregister the device number
    unregister_chrdev_region(device_number, 1);
    pr_info("Device number unregistered.\n");

    pr_info("Module unloaded successfully.\n");
}

module_init(pcd_driver_init);
module_exit(pcd_driver_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hrasity");
MODULE_DESCRIPTION("pseudo character device driver");
MODULE_VERSION("0.1");