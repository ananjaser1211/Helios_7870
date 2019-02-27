#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/hall_notifier.h>

#define SET_HALL_NOTIFIER_BLOCK(nb, fn, dev) do {	\
		(nb)->notifier_call = (fn);		\
		(nb)->priority = (dev);			\
	} while (0)

#define DESTROY_HALL_NOTIFIER_BLOCK(nb)			\
		SET_HALL_NOTIFIER_BLOCK(nb, NULL, -1)

static struct hall_notifier_struct hall_notifier;

static void __set_noti_cxt(int hall_state)
{
	hall_notifier.hall_state = hall_state;
}

int hall_notifier_register(struct notifier_block *nb, notifier_fn_t notifier,
			hall_notifier_device_t listener)
{
	int ret = 0;
	pr_info("%s: listener=%d register\n", __func__, listener);
	
	SET_HALL_NOTIFIER_BLOCK(nb, notifier, listener);
	ret = blocking_notifier_chain_register(&(hall_notifier.notifier_call_chain), nb);
	if (ret < 0)
		pr_err("%s: blocking_notifier_chain_register error(%d)\n",
				__func__, ret);

	nb->notifier_call(nb, hall_notifier.hall_state, NULL);
	
	return ret;
}

int hall_notifier_unregister(struct notifier_block *nb)
{
	int ret = 0;

	pr_info("%s: listener=%d unregister\n", __func__, nb->priority);

	ret = blocking_notifier_chain_unregister(&(hall_notifier.notifier_call_chain), nb);
	if (ret < 0)
		pr_err("%s: blocking_notifier_chain_unregister error(%d)\n",
				__func__, ret);
	DESTROY_HALL_NOTIFIER_BLOCK(nb);

	return ret;
}

static int hall_notifier_notify(void)
{
	int ret = 0;
	
	pr_info("%s: hall state= %d\n", __func__, hall_notifier.hall_state);
	ret = blocking_notifier_call_chain(&(hall_notifier.notifier_call_chain),
			hall_notifier.hall_state, NULL);

	switch (ret) {
	case NOTIFY_STOP_MASK:
	case NOTIFY_BAD:
		pr_err("%s: notify error occur(0x%x)\n", __func__, ret);
		break;
	case NOTIFY_DONE:
	case NOTIFY_OK:
		pr_info("%s: notify done(0x%x)\n", __func__, ret);
		break;
	default:
		pr_info("%s: notify status unknown(0x%x)\n", __func__, ret);
		break;
	}

	return ret;
}

void hall_notifier_hall_state(int hall_state)
{
	if (hall_notifier.hall_state != hall_state) {

		pr_info("%s: previous state(%d) != current state(%d)\n", __func__, hall_notifier.hall_state, hall_state);

		__set_noti_cxt(hall_state);

		/* hall's state broadcast */
		hall_notifier_notify();
	}
}

static int __init hall_notifier_init(void)
{
	int ret = 0;
	pr_info("%s\n", __func__);

	BLOCKING_INIT_NOTIFIER_HEAD(&(hall_notifier.notifier_call_chain));
	__set_noti_cxt(HALL_UNKNOWN);
	
	return ret;
}
device_initcall(hall_notifier_init);