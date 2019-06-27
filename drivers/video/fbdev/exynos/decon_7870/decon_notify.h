#include <linux/notifier.h>

int decon_register_notifier(struct notifier_block *nb);
int decon_unregister_notifier(struct notifier_block *nb);
int decon_notifier_call_chain(unsigned long val, void *v);

