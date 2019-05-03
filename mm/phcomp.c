#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/cpu.h>
#include <linux/swap.h>
#include <linux/types.h> 
#include <linux/device.h>
#include <linux/mmzone.h>
#include <asm/atomic.h>
#include <linux/sec_debug.h>
#include <linux/sched.h>
#include <linux/compaction.h>
#include <linux/of.h>
#include <linux/vmstat.h>
#include <linux/slab.h>

#define DEFAULT_DEBUG_LEVEL 2

#define PHCOMP_S_TIME(x)	x = jiffies
#define PHCOMP_E_TIME(x)	x = jiffies - x

#define phcomp_print(level, x...)			\
	do {						\
		if (phcomp_debug_level >= (level))	\
			pr_info(x);			\
	} while (0)

struct _phcomp_t {
	int idle_time;
	int exec_time;
	int st_order;
	int end_order;
	int tbl[11];
};

struct _phcomp_t *phcomp_data = NULL;

static unsigned int phcomp_compacted = 0;
static unsigned int phcomp_triggered = 0;
static unsigned int phcomp_executed = 0;
static unsigned int phcomp_debug_level __read_mostly = DEFAULT_DEBUG_LEVEL;

static struct task_struct *kphcompd = NULL;
static atomic_t kphcompd_running;
static unsigned short num_of_call_compact_node;

extern void call_compact_node(int nid, struct zone* zone, int order);

bool current_is_kphcompd(void)
{
	return current==kphcompd?1:0;
}

unsigned int get_phcomp_order_threshold( unsigned int order )
{
	return phcomp_data == NULL? 1000: phcomp_data->tbl[order];
}

unsigned int get_phcomp_idle_time_threshold( void )
{
	unsigned int ret = phcomp_data == NULL? 20000:(phcomp_data->idle_time + phcomp_data->exec_time);
	return ret * USEC_PER_MSEC;
}

void trigger_phcompd(int cpu)
{
	const struct cpumask *mask = cpumask_of(cpu);
	unsigned int nr_running = nr_running_cpu(cpu);

	if( unlikely(kphcompd == NULL) ) {
		phcomp_print(2, "%s : phcompd not yet init!!\n", __func__);
		return;
	}
	if( unlikely(nr_running) ) {
		phcomp_print(3, "%s: nr_running_cpu : %d, so we do not trigger kphcompd\n", __func__, (int)nr_running);
		return;
	}

	if( unlikely(atomic_cmpxchg(&kphcompd_running, 0, 1)) ) {
		phcomp_print(3, "%s: kphcompd is running, so we do not trigger kphcompd\n", __func__);
		return;
	}	

	phcomp_print(3, "%s: set affinity of %s to cpu_mask:0x%X\n", __func__, kphcompd->comm, (int)*mask->bits);
	wait_task_inactive(kphcompd, 0);
	phcomp_triggered++;
	set_cpus_allowed_ptr(kphcompd, mask);
	wake_up_process(kphcompd);
}

static int phcomp_thread(void * nothing)
{
	struct zone *zone;
	unsigned int nid, order, ret;
	unsigned int _phcomp_time;
	
	set_freezable();
	
	for ( ; ; ) {
		try_to_freeze();
		if (kthread_should_stop())
			break;
					
		if ( likely(atomic_read(&kphcompd_running)==1) ) {
			
			phcomp_print(5,"%s started\n", __func__);
			num_of_call_compact_node = 0;

			for_each_online_node(nid) {
				for_each_zone(zone) 
				{
					if ( unlikely(!populated_zone(zone)) )
						continue;
					
					for ( order = phcomp_data->end_order ; order >= phcomp_data->st_order ; order-- ) {
														
						if( compaction_deferred(zone, order) ) 
						{
							count_vm_event(PHCOMPDEFERED);
							continue;
						}
																				
						ret = compaction_suitable(zone, order);
						switch (ret) {
							case COMPACT_PARTIAL:
								phcomp_print(3,"%s : [%s/order=%d] COMPACT_PARTIAL (the allocation would succeed) \n",
									__func__, zone->name, order);
								continue;
							case COMPACT_SKIPPED:
								phcomp_print(3,"%s : [%s/order=%d] COMPACT_SKIPPED (there are too few free pages for compaction)\n",
									__func__, zone->name, order);
								continue;
						}							
					
						PHCOMP_S_TIME(_phcomp_time);
						call_compact_node(nid, zone, order);
						PHCOMP_E_TIME(_phcomp_time);
						
						num_of_call_compact_node++;
						phcomp_print(1,"%s : [%s/order=%d] compaction tooks %dus\n", 
							__func__, zone->name, order, jiffies_to_usecs(_phcomp_time));
											
					}
				}
			}
			phcomp_print(5,"%s ended\n", __func__);
		}
		
		if( num_of_call_compact_node )
		{
			phcomp_compacted += num_of_call_compact_node;
			phcomp_executed++;	
		}

		set_current_state(TASK_INTERRUPTIBLE);
		atomic_set(&kphcompd_running, 0);
		schedule();
	}
	return 0;
}


static int __init phcomp_init(void)
{
	struct device_node *np;
	int ret = 0;
	unsigned int order = 0;

	np = of_find_compatible_node(NULL, NULL, "samsung,phcomp");

	if (!np) {
		pr_err("failed to get phcomp node\n");
		goto err;
	}

	phcomp_data = kzalloc(sizeof(struct _phcomp_t), GFP_KERNEL);
	if ( !phcomp_data ) {
		pr_err("failed to alloc for phcomp_data\n");
		goto err;
	}

	ret = of_property_read_u32(np, "idle,time", &phcomp_data->idle_time);
	if ( ret ) {
		pr_err("failed to read idle,time\n");
		goto err_free_phcomp_data;
	}

	ret = of_property_read_u32(np, "exec,time", &phcomp_data->exec_time);
	if ( ret ) {
		pr_err("failed to read idle,time\n");
		goto err_free_phcomp_data;
	}

	ret = of_property_read_u32_array(np, "trigger-tbl", phcomp_data->tbl, 11);
	if ( ret ) {
		pr_err("failed to read trigger-tbl\n");
		goto err_free_phcomp_data;
	}

	for( order = 0 ; ; order++ ) {
		if ( phcomp_data->tbl[order] != 1000 ) {
			phcomp_data->st_order = order;
			break;
		}
	}
	
	for( order = 10; ; order-- ) {
		if ( phcomp_data->tbl[order] != 1000 ) {
		phcomp_data->end_order = order;
		break;
		}
	}

	kphcompd = kthread_run(phcomp_thread, NULL, "kphcompd");
	if (IS_ERR(kphcompd)) {
		/* Failure at boot is fatal */
		BUG_ON(system_state == SYSTEM_BOOTING);
	}

	atomic_set(&kphcompd_running, 0);
	return 0;

err_free_phcomp_data:
	if ( phcomp_data ) {
		kfree(phcomp_data);
		phcomp_data = NULL;
	}
err:
	panic("PHCOMP init failed!! \n");
	return 0;
}

static void __exit phcomp_exit(void)
{
	return;
}

module_param_named(phcomp_compacted, phcomp_compacted,  int, 0444);
module_param_named(phcomp_triggered, phcomp_triggered,  int, 0444);
module_param_named(phcomp_executed, phcomp_executed,  int, 0444);
module_param_named(phcomp_debug_level, phcomp_debug_level,  int, 0664);

module_init(phcomp_init);
module_exit(phcomp_exit);

MODULE_LICENSE("GPL");
