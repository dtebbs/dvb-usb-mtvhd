
#include <rc-core.h>

static struct rc_dev *myrc = 0;
static struct delayed_work myrc_delayed_work;
static const int           myrc_interval = 1000;
static unsigned int state = 1;

static void input_test_idle(struct rc_dev *dev, bool enable)
{

	printk(KERN_INFO "input_test_idle: (dev %p, enable %s)\n",
	       dev, (enable)?("true"):("false"));
}

static void input_test_poll(struct work_struct *work)
{
	schedule_delayed_work(&myrc_delayed_work,
			      msecs_to_jiffies(myrc_interval));

	u32 keycode = rc_g_keycode_from_table(myrc, KEY_MUTE);
	
	printk(KERN_INFO "input_test_poll: (scan %d, key %d)\n", 
	       KEY_MUTE, keycode);

	//rc_keydown(myrc, KEY_MUTE, 1);
	input_event(myrc->input_dev, EV_MSC, MSC_SCAN, KEY_MUTE);
	input_report_key(myrc->input_dev, KEY_MUTE, state);
	state = 1-state;
	input_sync(myrc->input_dev);
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
	myrc->map_name = RC_MAP_LIRC;
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
