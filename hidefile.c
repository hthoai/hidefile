#include <linux/init.h>    // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>  // Core header for loading LKMs into the kernel
#include <linux/device.h>  // Header to support the kernel Driver Model
#include <linux/kernel.h>  // Contains types, macros, functions for the kernel
#include <linux/fs.h>      // Header for the Linux file system support
#include <linux/uaccess.h> // Required for the copy to user function
#include <linux/namei.h>
#define DEVICE_NAME "hidefile" ///< The device will appear at /dev/hidefile using this value
#define CLASS_NAME "hide"      ///< The device class -- this is a character device driver
#define SUCCESS 0

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hawliet");
MODULE_DESCRIPTION("A simple Linux char driver to hide file");
MODULE_VERSION("0.1");

static int g_device_open = 0;
static int majorNumber;                      ///< Stores the device number -- determined automatically
static struct class *hidefileClass = NULL;   ///< The device-driver class struct pointer
static struct device *hidefileDevice = NULL; ///< The device-driver device struct pointer

// The prototype functions for the character driver -- must come before the struct definition
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

void allocate_memmory(void);
void reallocate_memmory(void);
unsigned long backup_functions(void);
unsigned long hook_functions(const char *file_path);

/** @brief Devices are represented as file structure in the kernel. The file_operations structure from
 *  /linux/fs.h lists the callback functions that you wish to associated with your file operations
 *  using a C99 syntax structure. char devices usually implement open, read, write and release calls
 */
static struct file_operations fops =
    {
        .owner = THIS_MODULE,
        .open = dev_open,
        .write = dev_write,
        .release = dev_release,
};

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point.
 *  @return returns 0 if successful
 */
static int __init hidefile_init(void)
{
    printk(KERN_INFO "Hidefile: Initializing the hidefile LKM\n");

    // Try to dynamically allocate a major number for the device -- more difficult but worth it
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber < 0)
    {
        printk(KERN_ALERT "hidefile failed to register a major number\n");
        return majorNumber;
    }
    printk(KERN_INFO "Hidefile: registered correctly with major number %d\n", majorNumber);

    // Register the device class
    hidefileClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(hidefileClass))
    { // Check for error and clean up if there is
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(hidefileClass); // Correct way to return an error on a pointer
    }
    printk(KERN_INFO "Hidefile: device class registered correctly\n");

    // Register the device driver
    hidefileDevice = device_create(hidefileClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(hidefileDevice))
    {                                 // Clean up if there is an error
        class_destroy(hidefileClass); // Repeated code but the alternative is goto statements
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(hidefileDevice);
    }
    printk(KERN_INFO "Hidefile: device class created correctly\n"); // Made it! device was initialized
    return 0;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit hidefile_exit(void)
{
    backup_functions();
    device_destroy(hidefileClass, MKDEV(majorNumber, 0)); // remove the device
    class_unregister(hidefileClass);                      // unregister the device class
    class_destroy(hidefileClass);                         // remove the device class
    unregister_chrdev(majorNumber, DEVICE_NAME);          // unregister the major number
    printk(KERN_INFO "Hidefile: Goodbye from the LKM!\n");
}

/** @brief The device open function that is called each time the device is opened
 *  This will only increment the numberOpens counter in this case.
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_open(struct inode *inodep, struct file *filep)
{
    if (g_device_open)
        return -EBUSY;

    g_device_open++;
    try_module_get(THIS_MODULE);

    return SUCCESS;
}

/** @brief This function is called whenever the device is being written to from user space i.e.
 *  data is sent to the device from the user. The data is copied to the message[] array in this
 *  LKM using the sprintf() function along with the length of the string.
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t dev_write(struct file *file_ptr, const char *buffer, size_t length, loff_t *offset)
{
    char *pFile_Path;

    pFile_Path = (char *)kmalloc(sizeof(char *) * length, GFP_KERNEL);

    if (strncpy_from_user(pFile_Path, buffer, length) == -EFAULT)
    {

        printk(KERN_NOTICE "Entered in fault get_user state");

        length = -1;
        goto finish;
    }

    if (strstr(pFile_Path, "\n"))
    {
        pFile_Path[length - 1] = 0;

        printk(KERN_NOTICE "Entered in end line filter");
    }

    printk(KERN_NOTICE "File path is %s without EOF", pFile_Path);

    if (hook_functions(pFile_Path) == -1)
    {
        length = -2;
    }
finish:
    kfree(pFile_Path);

    return length;
}

/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep)
{
    g_device_open--;

    module_put(THIS_MODULE);
    return SUCCESS;
}

struct dentry *g_parent_dentry;
struct nameidata g_root_nd;

unsigned long *g_inode_numbers;
int g_inode_count = 0;

void **g_old_inode_pointer;
void **g_old_fop_pointer;
void **g_old_iop_pointer;

void **g_old_parent_inode_pointer;
void **g_old_parent_fop_pointer;

filldir_t real_filldir;

static int new_filldir(void *buf, const char *name, int namelen, loff_t offset, u64 ux64, unsigned ino)
{
    unsigned int i = 0;
    struct dentry *pDentry;
    struct qstr Current_Name;

    Current_Name.name = name;
    Current_Name.len = namelen;
    Current_Name.hash = full_name_hash(name, namelen);

    pDentry = d_lookup(g_parent_dentry, &Current_Name);

    if (pDentry != NULL)
    {
        for (i = 0; i <= g_inode_count - 1; i++)
        {
            if (g_inode_numbers[i] == pDentry->d_inode->i_ino)
            {
                return 0;
            }
        }
    }

    return real_filldir(buf, name, namelen, offset, ux64, ino);
}

int parent_readdir(struct file *file, void *dirent, filldir_t filldir)
{
    g_parent_dentry = file->f_dentry;

    real_filldir = filldir;

    return g_root_nd.path.dentry->d_inode->i_fop->readdir(file, dirent, new_filldir);
}

static struct file_operations new_parent_fop =
    {
        .owner = THIS_MODULE,
        .readdir = parent_readdir,
};

int new_mmap(struct file *file, struct vm_area_struct *area)
{
    printk(KERN_ALERT "Entered in new_mmap\n");
    return -2;
}

ssize_t new_read(struct file *file1, char __user *u, size_t t, loff_t *ll)
{
    printk(KERN_ALERT "Entered in new_read\n");
    return -2;
}

ssize_t new_write(struct file *file1, const char __user *u, size_t t, loff_t *ll)
{
    printk(KERN_ALERT "Entered in new_write\n");
    return -2;
}

int new_release(struct inode *new_inode, struct file *file)
{
    printk(KERN_ALERT "Entered in new_release \n");
    return -2;
}

int new_flush(struct file *file, fl_owner_t id)
{
    printk(KERN_ALERT "Entered in new_flush \n");
    return -2;
}

int new_readdir(struct file *file, void *dirent, filldir_t filldir)
{
    printk(KERN_ALERT "Entered in new_readdir \n");
    return -2;
}

int new_open(struct inode *old_inode, struct file *old_file)
{
    printk(KERN_ALERT "Entered in new_open \n");
    return -2;
}

static struct file_operations new_fop =
    {
        .owner = THIS_MODULE,
        .readdir = new_readdir,
        .release = new_release,
        .open = new_open,
        .read = new_read,
        .write = new_write,
        .mmap = new_mmap,
};

int new_rmdir(struct inode *new_inode, struct dentry *new_dentry)
{
    printk(KERN_ALERT "Entered in new_rmdir \n");
    return -2;
}

int new_getattr(struct vfsmount *mnt, struct dentry *new_dentry, struct kstat *ks)
{
    printk(KERN_ALERT "Entered in new_getatr \n");
    return -2;
}

static struct inode_operations new_iop =
    {
        .getattr = new_getattr,
        .rmdir = new_rmdir,
};

/*Allocate memmory for arrays*/
void allocate_memmory(void)
{
    g_old_inode_pointer = (void *)kmalloc(sizeof(void *), GFP_KERNEL);
    g_old_fop_pointer = (void *)kmalloc(sizeof(void *), GFP_KERNEL);
    g_old_iop_pointer = (void *)kmalloc(sizeof(void *), GFP_KERNEL);

    g_old_parent_inode_pointer = (void *)kmalloc(sizeof(void *), GFP_KERNEL);
    g_old_parent_fop_pointer = (void *)kmalloc(sizeof(void *), GFP_KERNEL);

    g_inode_numbers = (unsigned long *)kmalloc(sizeof(unsigned long), GFP_KERNEL);
}

void reallocate_memmory(void)
{
    /*Realloc memmory for inode number*/
    g_inode_numbers = (unsigned long *)krealloc(g_inode_numbers, sizeof(unsigned long *) * (g_inode_count + 1), GFP_KERNEL);

    /*Realloc memmory for old pointers*/
    g_old_inode_pointer = (void *)krealloc(g_old_inode_pointer, sizeof(void *) * (g_inode_count + 1), GFP_KERNEL);
    g_old_fop_pointer = (void *)krealloc(g_old_fop_pointer, sizeof(void *) * (g_inode_count + 1), GFP_KERNEL);
    g_old_iop_pointer = (void *)krealloc(g_old_iop_pointer, sizeof(void *) * (g_inode_count + 1), GFP_KERNEL);

    g_old_parent_inode_pointer = (void *)krealloc(g_old_parent_inode_pointer, sizeof(void *) * (g_inode_count + 1), GFP_KERNEL);
    g_old_parent_fop_pointer = (void *)krealloc(g_old_parent_fop_pointer, sizeof(void *) * (g_inode_count + 1), GFP_KERNEL);
}

unsigned long hook_functions(const char *file_path)
{
    int error = 0;
    struct nameidata nd;

    error = path_lookup("/root", 0, &g_root_nd);

    if (error)
    {
        printk(KERN_ALERT "Can't access root\n");

        return -1;
    }

    error = path_lookup(file_path, 0, &nd);

    if (error)
    {
        printk(KERN_ALERT "Can't access file\n");

        return -1;
    }

    if (g_inode_count == 0)
    {
        allocate_memmory();
    }

    if (g_inode_numbers == NULL)
    {
        printk(KERN_ALERT "Not enought memmory in buffer\n");

        return -1;
    }

    /************************Old pointers**********************************/
    /*Save pointers*/
    g_old_inode_pointer[g_inode_count] = nd.path.dentry->d_inode;
    g_old_fop_pointer[g_inode_count] = (void *)nd.path.dentry->d_inode->i_fop;
    g_old_iop_pointer[g_inode_count] = (void *)nd.path.dentry->d_inode->i_op;

    g_old_parent_inode_pointer[g_inode_count] = nd.path.dentry->d_parent->d_inode;
    g_old_parent_fop_pointer[g_inode_count] = (void *)nd.path.dentry->d_parent->d_inode->i_fop;

    /*Save inode number*/
    g_inode_numbers[g_inode_count] = nd.path.dentry->d_inode->i_ino;
    g_inode_count = g_inode_count + 1;

    reallocate_memmory();

    /*filldir hook*/
    nd.path.dentry->d_parent->d_inode->i_fop = &new_parent_fop;

    /* Hook of commands for file*/
    nd.path.dentry->d_inode->i_op = &new_iop;
    nd.path.dentry->d_inode->i_fop = &new_fop;

    return 0;
}

unsigned long backup_functions(void)
{
    int i = 0;
    struct inode *pInode;
    struct inode *pParentInode;

    for (i = 0; i < g_inode_count; i++)
    {
        pInode = g_old_inode_pointer[(g_inode_count - 1) - i];
        pInode->i_fop = (void *)g_old_fop_pointer[(g_inode_count - 1) - i];
        pInode->i_op = (void *)g_old_iop_pointer[(g_inode_count - 1) - i];

        pParentInode = g_old_parent_inode_pointer[(g_inode_count - 1) - i];
        pParentInode->i_fop = (void *)g_old_parent_fop_pointer[(g_inode_count - 1) - i];
    }

    kfree(g_old_inode_pointer);
    kfree(g_old_fop_pointer);
    kfree(g_old_iop_pointer);

    kfree(g_old_parent_inode_pointer);
    kfree(g_old_parent_fop_pointer);

    kfree(g_inode_numbers);

    return 0;
}

/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which
 *  identify the initialization function at insertion time and the cleanup function (as
 *  listed above)
 */
module_init(hidefile_init);
module_exit(hidefile_exit);