#ifndef _SEC_ARGOS_H
#define _SEC_ARGOS_H

extern int irq_set_affinity(unsigned int irq, const struct cpumask *mask);

extern int sec_argos_register_notifier(struct notifier_block *n, char *label);
extern int sec_argos_unregister_notifier(struct notifier_block *n, char *label);

#endif
