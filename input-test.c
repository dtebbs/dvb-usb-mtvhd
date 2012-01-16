
#include <rc-core.h>

static struct rc_dev *myrc = 0;
static struct delayed_work myrc_delayed_work;
static const int           myrc_interval = 1000;

static void input_test_idle(struct rc_dev *dev, bool enable)
{

	printk(KERN_INFO "input_test_idle: (dev %p, enable %s)\n",
	       dev, (enable)?("true"):("false"));
}

static void input_test_poll(struct work_struct *work)
{
	printk(KERN_INFO "input_test_poll:\n");

	schedule_delayed_work(&myrc_delayed_work,
			      msecs_to_jiffies(myrc_interval));
}

static int __init input_test_init(void)
{
	int err;

        printk(KERN_INFO "input_test_init:\n");

	myrc = rc_allocate_device();
	if (!myrc) {
		printk(KERN_INFO " nomem\n");
		return -ENOMEM;
	}

	myrc->driver_name = "d-input-test-driver";
	myrc->input_name = "d-input-test-input";
	myrc->map_name = RC_MAP_EMPTY;
	myrc->priv = (void *)0x1234;

	myrc->s_idle = input_test_idle;

	err = rc_register_device(myrc);
	if (err) {
		printk(KERN_INFO " reg failed\n");
		rc_free_device(myrc);
		myrc = 0;
		return err;
	}
	printk(KERN_INFO "registered\n");

	INIT_DELAYED_WORK(&myrc_delayed_work, input_test_poll);
	schedule_delayed_work(&myrc_delayed_work,
			      msecs_to_jiffies(myrc_interval));

        return 0;
}

static void __exit input_test_exit(void)
{
        printk(KERN_INFO "input_test_exit:\n");

	if (myrc) {
		cancel_delayed_work(&myrc_delayed_work);

		printk(KERN_INFO "unregistering\n");
		rc_unregister_device(myrc);
		myrc = 0;
	}
}

module_init(input_test_init);
module_exit(input_test_exit);

MODULE_AUTHOR("Duncan Tebbs");
MODULE_DESCRIPTION("Driver for SKnet MonsterTV Series Remote Control");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
