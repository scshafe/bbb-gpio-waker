#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/time.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>     

// for the cdev
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h> // copy_to_user()

// for waitqueue
#include <linux/wait.h>


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIO driver for detecting PIR motion sensor");

//   locations given by the 4D Cape schematic: https://resources.4dsystems.com
/*
    motion_sensor_detector_device {
                compatible = "scshafe,my_test_driver";

                pir-ms-up-gpios = <0x2f 0x03 0x00>;        // gpio3  == gpio 0_3
                pir-ms-lo-gpios = <0x5a 0x10 0x00>;        // gpio48 == gpio 1_"

                pir-ms-test-led-gpios = <0x5a 0x11 0x00>;  // gpio49 == gpio 1_"
        };
*/

// ----------- LOCK -----------

// struct mutex pir_ms_mutex;
// unsigned long pir_ms_global_variable = 0;

// DEFINE_SPINLOCK(pir_ms_spinlock);
// struct mutex pir_ms_lock;

DECLARE_WAIT_QUEUE_HEAD(wq);
static int waiting_on_trigger;  //   0: ready to read,  1: waiting for GPIO trit

// ----------- CHAR DEVICE -----------

static dev_t dev_numbers = 0;
static struct class *dev_class;
static struct cdev pir_ms_cdev;
static char buf_state;            // either '1' or '0'


// -----------    GPIOS    -----------

static struct gpio_desc *my_led; // for testing
static struct gpio_desc *pir_ms_up, *pir_ms_lo;  // rising edges and falling eds
static int pir_ms_detected_irq;
static int pir_ms_undetected_irq;

static const struct of_device_id gpiod_dt_ids[] = {
    { .compatible = "scshafe,my_test_driver", },
    { /* sentinel */ }
};

static int      pir_ms_open(struct inode *inode, struct file *file);
static int      pir_ms_release(struct inode *inode, struct file *file);
static ssize_t  pir_ms_read(struct file *filp, char __user *buf, size_t len, loff_t *off);


static struct file_operations fops =
{
    .owner      = THIS_MODULE,
    .read       = pir_ms_read,
    .open       = pir_ms_open,
    .release    = pir_ms_release,
};

static void set_motion_value(int i)
{
// #ifdef DEBUG
    gpiod_set_value(my_led, i);
// #endif
    buf_state = (i == 1 ? '1' : '0');                                           
    waiting_on_trigger = 0;                                                     
    wake_up_interruptible(&wq);                                                 
                                                                                
}                                                                               
                                                                                
// static char get_motion_value()                                               
// {                                                                            
//     return buf_state;                                                        
// }                                                                            
                                                                                
static int pir_ms_open(struct inode *inode, struct file *file)                  
{                                                                               
    pr_info("YEET: open\n");                                                    
    return 0;                                                                   
}                                                                               
/*                                                                              
** This function will be called when we close the Device file                   
*/                                                                              
static int pir_ms_release(struct inode *inode, struct file *file)               
{                                                                               
    pr_info("YEET: release\n");                                                 
    return 0;                                                                   
}                                                                               


static ssize_t pir_ms_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{                                                                               
    pr_info("YEET:  reading len: [%d]  offset: [%d]\n", len, *off);          
    ssize_t retval;                                                             
    // if (mutex_lock_interruptible(&dev->lock))                                
        //              return -ERESTARTSYS;                                      �����   ������������������������������                     

    wait_event_interruptible(wq, waiting_on_trigger == 0);                      
    if (waiting_on_trigger == 2)                                                
    {                                                                           
        pr_info("Event exiting module\n");                                      
        return 0;                                                               
    }                                                                           
    waiting_on_trigger = 1;                                                     

    if (*off >= len)                                                            
    {                                                                           
        retval = 0;                                                             
    }                                                                           
    else                                                                        
    {                                                                           
        put_user(buf_state, &buf[*off++]);                                      
        retval = 1;                                                             
    }                                                                           
    return retval;                                                              
}                                                                               


static irqreturn_t motion_detected_irq(int irq, void *dev_id)                   
{
    pr_info("YEET: motion    detected\n");                                                                                
    set_motion_value(1);                                                        
    return IRQ_HANDLED;                                                         
}                                                                               

static irqreturn_t motion_undetected_irq(int irq, void *dev_id)                 
{
    pr_info("YEET: motion un-detected\n");                                                                                
    set_motion_value(0);                                                        
    return IRQ_HANDLED;                                                         
}                                                                               


static int motion_sensor_probe(struct platform_device *pdev)                    
{                                                                               
    int retval;                                                                 
    struct device *dev = &pdev->dev;                                                  
    pr_info("YEET: initalizing GPIOs for motion-sensor\n");                                 

    pir_ms_up = gpiod_get(dev, "pir-ms-up", GPIOD_IN);
    if (IS_ERR(pir_ms_up))
    {
        pr_err("YEET: Error could not get GPIO for up signal\n");
    }
    else
    {
        pr_info("YEET: Successfully initialized GPIO up\n");
    }

    pir_ms_lo = gpiod_get(dev, "pir-ms-lo", GPIOD_IN);   
    if (IS_ERR(pir_ms_lo))
    {
        pr_err("YEET: Error could not get GPIO for lo signal\n");
        // return 1;
    }
    else
    {
        pr_info("YEET: Successfully initialized GPIO lo\n");
    }                    

    set_motion_value(0);                                                        
    waiting_on_trigger = false;                                                 

    // mutex_init(&pir_ms_lock);                                                


    pir_ms_detected_irq = gpiod_to_irq(pir_ms_up);  
    if (pir_ms_detected_irq < 0)
    {
        pr_err("YEET: error getting detected irq number: %d", pir_ms_detected_irq);
        // return 1;
    }
    else
    {
        pr_info("YEET: ms detected irq number: %d", pir_ms_detected_irq);  
    }

    
    retval = request_threaded_irq(pir_ms_detected_irq,
                                  NULL,
                                  motion_detected_irq,
                                  IRQF_TRIGGER_RISING |IRQF_ONESHOT,
                                  "my_test_driver",
                                  NULL);


    pir_ms_undetected_irq = gpiod_to_irq(pir_ms_lo);
    if (pir_ms_undetected_irq < 0)
    {
        pr_err("YEET: error getting undetected irq number: %d", pir_ms_undetected_irq);
        return 1;
    }
    else
    {
        pr_info("YEET: ms undetected irq number: %d", pir_ms_undetected_irq);  
    }

    pr_info("YEET: ms undetected irq number: %d", pir_ms_undetected_irq);  
    retval = request_threaded_irq(pir_ms_undetected_irq, 
                                  NULL,
                                  motion_undetected_irq,
                                  IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                  "my_test_driver", 
                                  NULL);


    if((alloc_chrdev_region(&dev_numbers, 0, 1, "pir_ms_Dev")) <0){
        pr_err("YEET: Cannot allocate major number\n");
        return -1;
    }
    pr_info("YEET: Major = %d Minor = %d \n",MAJOR(dev_numbers), MINOR(dev_numbers));
    /*Creating cdev structure*/
    cdev_init(&pir_ms_cdev,&fops);
    /*Adding character device to the system*/
    if((cdev_add(&pir_ms_cdev,dev_numbers,1)) < 0){
        pr_err("YEET: Cannot add the device to the system\n");
        goto r_class;
    }
    /*Creating struct class*/
    if(IS_ERR(dev_class = class_create(THIS_MODULE,"pir_ms_class"))){
        pr_err("YEET: Cannot create the struct class\n");
        goto r_class;
    }
    /*Creating device*/
    if(IS_ERR(device_create(dev_class,NULL,dev_numbers,NULL,"pir_ms_device"))){
        pr_err("YEET: Cannot create the Device 1\n");
        goto r_device;
    }

    pr_info("YEET: Done initializing ms_gpio_driver\n");
    return 0;

r_device:
    class_destroy(dev_class);
r_class:
    unregister_chrdev_region(dev_numbers,1);
    return -1;

    // initialize gpio led for output
    // printk(KERN_INFO, "YEET: Done initializing\n");
    

}

static int motion_sensor_exit(struct platform_device *pdev)
{
    pr_info("YEET: De-initializing GPIO PIR motion sensor\n");
    free_irq(pir_ms_undetected_irq, NULL);
    free_irq(pir_ms_detected_irq, NULL);
    

    gpiod_put(my_led);
    gpiod_put(pir_ms_up);
    gpiod_put(pir_ms_lo);


    device_destroy(dev_class,dev_numbers);
    class_destroy(dev_class);
    cdev_del(&pir_ms_cdev);
    unregister_chrdev_region(dev_numbers, 1);

    waiting_on_trigger = 2;
    wake_up_interruptible(&wq);

    pr_info("YEET: goodbye\n");
    return 0;
}


static struct platform_driver bbb_ms_driver = {
    .probe      = motion_sensor_probe,
    .remove     = motion_sensor_exit,
    .driver     = {
        .name     = "bbb_motion_sensor",
        .of_match_table = of_match_ptr(gpiod_dt_ids),  
        .owner    = THIS_MODULE,
    },
};

module_platform_driver(bbb_ms_driver);

/*
Will create cdev that can be read by a user process

-when light goes on, driver writes "1" to this cdev
    -user process reads it, activates the screen
    -user process sets count=5, and timer=1min
        -every time timer returns, user process non-blocking polls the cdev
        -if it returns "1", count resets to 5
        -if it returns nothing or "0", count -= 1 and timer starts again

    if count gets to 0, screen turns off and user process tries to read until it receives a "1" again



*/