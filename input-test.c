

static int __init input_test_init(void)
{
        printk(KERN_INFO "input_test_init:\n");
        return 0;
}

static void __exit input_test_exit(void)
{
        printk(KERN_INFO "input_test_exit:\n");
}

module_init(input_test_init);
module_exit(input_test_exit);

MODULE_AUTHOR("Duncan Tebbs");
MODULE_DESCRIPTION("Driver for SKnet MonsterTV Series Remote Control");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
