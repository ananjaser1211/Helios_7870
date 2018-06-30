/* linux/drivers/video/exynos/decon_display/decon_board.c
 *
 * Copyright (c) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include "../../../../pinctrl/core.h"

#include "decon_board.h"

/*
*
0. There is a pre-defined property of the name of "decon_board".
decon_board has a phandle value that uniquely identifies the other node
containing subnodes to control gpio, regulator, delay and pinctrl.
If you want make new list, write control sequence list in dts, and call run_list function with subnode name.

1. type
There are 5 pre-defined types
- GPIO has 2 kinds of subtype: HIGH, LOW
- REGULATOR has 2 kinds of subtype: ENABLE, DISABLE
- DELAY has 3 kinds of subtype: MDELAY, MSLEEP, USLEEP
- PINCTRL has no pre-defined subtype, it needs pinctrl name as string in subtype position.
- TIMER has 3 kinds of subtype: START, DELAY, CLEAR
- TIMER needs it's name as string after subtype position. (ex: timer,start,name)

2. subtype and property
- GPIO(HIGH, LOW) search gpios-property for gpio position. 1 at a time.
- REGULATOR(ENABLE, DISABLE) search regulator-property for regulator name. 1 at a time.
- DELAY(MDELAY, MSLEEP) search delay-property for delay duration. 1 at a time.
- DELAY(USLEEP) search delay-property for delay duration. 2 at a time.
- TIMER(START) search delay-property for delay duration. 1 at a time.

3. property type
- type, subtype and desc property type is string
- gpio-property type is phandle
- regulator-property type is string
- delay-property type is u32

4. check rule
- number of type = number of subtype
- number of each type = number of each property.
But If subtype is USLEEP, it needs 2 parameter. So we check 1 USLEEP = 2 * u32 delay-property
- desc-property is for debugging message description. It's not essential.

5. example:
decon_board = <&node>;
node: node {
	subnode_1 {
		type = "regulator,enable", "gpio,high", "delay,usleep", "pinctrl,turnon_tes", "delay,msleep";
		desc = "ldo1 enable", "gpio high", "Wait 10ms", "te pin configuration", "30ms";
		gpios = <&gpf1 5 0x1>;
		delay = <10000 11000>, <30>;
		regulator = "ldo1";
	};
	subnode_2 {
		type = "timer,start,loading";
		delay = <300>;
		desc = "save timestamp when subnode_2 is called"
	};
	subnode_3 {
		type = "timer,delay,loading";
		desc = "if duration (timer,start ~ timer,delay) is less than 300, wait. and clear start timestamp"
	};
};

run_list(dev, "subnode_1");
run_list(dev, "subnode_2");
run_list(dev, "subnode_3");

*/

/* #define CONFIG_BOARD_DEBUG */

#define DECON_BOARD_DTS_NAME	"decon_board"

#if defined(CONFIG_BOARD_DEBUG)
#define bd_dbg(fmt, ...)		pr_debug(pr_fmt("%s: %3d: %s: " fmt), DECON_BOARD_DTS_NAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
#define bd_dbg(fmt, ...)
#endif
#define bd_info(fmt, ...)		pr_info(pr_fmt("%s: %3d: %s: " fmt), DECON_BOARD_DTS_NAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define bd_warn(fmt, ...)		pr_warn(pr_fmt("%s: %3d: %s: " fmt), DECON_BOARD_DTS_NAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define STREQ(a, b) 			(*(a) == *(b) && strcmp((a), (b)) == 0)
#define STRNEQ(a, b) 			(strncmp((a), (b), (strlen(a))) == 0)

#define MSEC_TO_USEC(ms)		(ms * USEC_PER_MSEC)
#define USEC_TO_MSEC(us)		(us / USEC_PER_MSEC)
#define SMALL_MSECS				(20)

struct bd_info {
	char					*name;
	struct list_head		node;
};

struct timer_info {
	const char				*name;
	ktime_t					start;
	ktime_t					target;
	ktime_t					now;
	unsigned int			delay;
};

struct action_info {
	char						*type;
	char						*subtype;
	const char					*desc;

	unsigned int				idx;
	int							gpio;
	unsigned int				delay[2];
	struct regulator_bulk_data	*supply;
	struct pinctrl				*pins;
	struct pinctrl_state		*state;
	struct timer_info			*timer;
	struct list_head			node;
};

enum {
	ACTION_DUMMY,
	ACTION_GPIO_HIGH,
	ACTION_GPIO_LOW,
	ACTION_REGULATOR_ENABLE,
	ACTION_REGULATOR_DISABLE,
	ACTION_DELAY_MDELAY,
	ACTION_DELAY_MSLEEP,
	ACTION_DELAY_USLEEP,	/* usleep_range */
	ACTION_PINCTRL,
	ACTION_TIMER_START,
	ACTION_TIMER_DELAY,
	ACTION_TIMER_CLEAR,
	ACTION_MAX
};

const char *action_list[ACTION_MAX][2] = {
	[ACTION_GPIO_HIGH] = {"gpio", "high"},
	{"gpio", "low"},
	{"regulator", "enable"},
	{"regulator", "disable"},
	{"delay", "mdelay"},
	{"delay", "msleep"},
	{"delay", "usleep"},
	{"pinctrl", ""},
	{"timer", "start"},
	{"timer", "delay"},
	{"timer", "clear"}
};

static struct bd_info	*lists[10];

static int print_action(struct action_info *info)
{
	if (!IS_ERR_OR_NULL(info->desc))
		bd_dbg("[%2d] %s\n", info->idx, info->desc);

	switch (info->idx) {
	case ACTION_GPIO_HIGH:
		bd_dbg("[%2d] gpio(%d) high\n", info->idx, info->gpio);
		break;
	case ACTION_GPIO_LOW:
		bd_dbg("[%2d] gpio(%d) low\n", info->idx, info->gpio);
		break;
	case ACTION_REGULATOR_ENABLE:
		bd_dbg("[%2d] regulator(%s) enable\n", info->idx, info->supply->supply);
		break;
	case ACTION_REGULATOR_DISABLE:
		bd_dbg("[%2d] regulator(%s) disable\n", info->idx, info->supply->supply);
		break;
	case ACTION_DELAY_MDELAY:
		bd_dbg("[%2d] mdelay(%d)\n", info->idx, info->delay[0]);
		break;
	case ACTION_DELAY_MSLEEP:
		bd_dbg("[%2d] msleep(%d)\n", info->idx, info->delay[0]);
		break;
	case ACTION_DELAY_USLEEP:
		bd_dbg("[%2d] usleep(%d %d)\n", info->idx, info->delay[0], info->delay[1]);
		break;
	case ACTION_PINCTRL:
		bd_dbg("[%2d] pinctrl(%s)\n", info->idx, info->state->name);
		break;
	case ACTION_TIMER_START:
		bd_dbg("[%2d] timer,start(%s %d)\n", info->idx, info->timer->name, info->timer->delay);
		break;
	case ACTION_TIMER_DELAY:
		bd_dbg("[%2d] timer,delay(%s %d)\n", info->idx, info->timer->name, info->timer->delay);
		break;
	case ACTION_TIMER_CLEAR:
		bd_dbg("[%2d] timer,clear(%s %d)\n", info->idx, info->timer->name, info->timer->delay);
		break;
	default:
		bd_info("[%2d] unknown idx\n", info->idx);
		break;
	}

	return 0;
}

static int secprintf(char *buf, size_t size, s64 nsec)
{
	struct timeval tv = ns_to_timeval(nsec);

	return scnprintf(buf, size, "%lu.%06lu", (unsigned long)tv.tv_sec, tv.tv_usec);
}

static void print_timer(struct timer_info *timer)
{
	s64 elapse, remain;
	char buf[70] = {0, };
	int len = 0;

	elapse = timer->now.tv64 - timer->start.tv64;
	remain = abs(timer->target.tv64 - timer->now.tv64);

	len += secprintf(buf + len, sizeof(buf) - len, timer->start.tv64);
	len += scnprintf(buf + len, sizeof(buf) - len, " - ");
	len += secprintf(buf + len, sizeof(buf) - len, timer->now.tv64);
	len += scnprintf(buf + len, sizeof(buf) - len, " = ");
	len += secprintf(buf + len, sizeof(buf) - len, elapse);
	len += scnprintf(buf + len, sizeof(buf) - len, ", %s", timer->target.tv64 < timer->now.tv64 ? "-" : "");
	len += secprintf(buf + len, sizeof(buf) - len, remain);

	bd_info("%s: delay: %d, %s\n", timer->name, timer->delay, buf);
}

static void dump_list(struct list_head *head)
{
	struct action_info *info;

	list_for_each_entry(info, head, node) {
		print_action(info);
	}
}

static int decide_action(const char *type, const char *subtype)
{
	int i;
	int action = ACTION_DUMMY;

	if (type == NULL || *type == '\0')
		return 0;
	if (subtype == NULL || *subtype == '\0')
		return 0;

	if (STRNEQ("pinctrl", type)) {
		action = ACTION_PINCTRL;
		goto exit;
	}

	for (i = ACTION_GPIO_HIGH; i < ACTION_MAX; i++) {
		if (STRNEQ(action_list[i][0], type) && STRNEQ(action_list[i][1], subtype)) {
			action = i;
			break;
		}
	}

exit:
	if (action == ACTION_DUMMY)
		bd_warn("there is no valid action for %s %s\n", type, subtype);

	return action;
}

static int check_dt(struct device_node *np)
{
	struct property *prop;
	const char *s;
	int type, desc;
	int gpio = 0, delay = 0, regulator = 0, pinctrl = 0, delay_property = 0, timer = 0;

	of_property_for_each_string(np, "type", prop, s) {
		if (STRNEQ("gpio", s))
			gpio++;
		else if (STRNEQ("regulator", s))
			regulator++;
		else if (STRNEQ("delay", s))
			delay++;
		else if (STRNEQ("pinctrl", s))
			pinctrl++;
		else if (STRNEQ("timer", s))
			timer++;
		else {
			bd_warn("there is no valid type for %s\n", s);
#ifdef CONFIG_BOARD_DEBUG
			BUG();
#endif
		}
	}

	of_property_for_each_string(np, "type", prop, s) {
		if (STRNEQ("delay,usleep", s))
			delay++;
	}

	of_property_for_each_string(np, "type", prop, s) {
		if (STRNEQ("timer,start", s))
			delay++;
	}

	type = of_property_count_strings(np, "type");

	if (of_find_property(np, "desc", NULL)) {
		desc = of_property_count_strings(np, "desc");
		WARN(type != desc, "type(%d) and desc(%d) is not match\n", type, desc);
#ifdef CONFIG_BOARD_DEBUG
		BUG_ON(type != desc);
#endif
	}

	if (of_find_property(np, "gpios", NULL)) {
		WARN(gpio != of_gpio_count(np), "gpio(%d %d) is not match\n", gpio, of_gpio_count(np));
#ifdef CONFIG_BOARD_DEBUG
		BUG_ON(gpio != of_gpio_count(np));
#endif
	}

	if (of_find_property(np, "regulator", NULL)) {
		WARN(regulator != of_property_count_strings(np, "regulator"),
			"regulator(%d %d) is not match\n", regulator, of_property_count_strings(np, "regulator"));
#ifdef CONFIG_BOARD_DEBUG
		BUG_ON(regulator != of_property_count_strings(np, "regulator"));
#endif
	}

	if (of_find_property(np, "delay", &delay_property)) {
		delay_property /= sizeof(u32);
		WARN(delay != delay_property, "delay(%d %d) is not match\n", delay, delay_property);
#ifdef CONFIG_BOARD_DEBUG
		BUG_ON(delay != delay_property);
#endif
	}

	bd_info("gpio: %d, regulator: %d, delay: %d, pinctrl: %d, timer: %d\n", gpio, regulator, delay, pinctrl, timer);

	return 0;
}

static struct timer_info *find_timer(const char *name)
{
	struct bd_info *list = NULL;
	struct list_head *head = NULL;
	struct timer_info *timer = NULL;
	struct action_info *info;
	int idx = 0;

	bd_dbg("%s\n", name);
	while (!IS_ERR_OR_NULL(lists[idx])) {
		list = lists[idx];
		head = &list->node;
		bd_dbg("%dth list name is %s\n", idx, list->name);
		list_for_each_entry(info, head, node) {
			if (STRNEQ("timer", info->type)) {
				if (info->timer && info->timer->name && STREQ(info->timer->name, name)) {
					bd_dbg("%s is found in %s\n", info->timer->name, list->name);
					return info->timer;
				}
			}
		}
		idx++;
		BUG_ON(idx == ARRAY_SIZE(lists));
	};

	bd_info("%s is not exist, so create it\n", name);
	timer = kzalloc(sizeof(struct timer_info), GFP_KERNEL);
	timer->name = kstrdup(name, GFP_KERNEL);

	return timer;
}

static int make_list(struct device *dev, struct list_head *head, const char *name)
{
	struct device_node *np = NULL;
	struct action_info *info;
	int i, count;
	int gpio = 0, delay = 0, regulator = 0, ret = 0;
	const char *type;
	char *timer_name;

	np = of_parse_phandle(dev->of_node, DECON_BOARD_DTS_NAME, 0);
	if (!np)
		bd_warn("%s node does not exist, so create dummy\n", DECON_BOARD_DTS_NAME);

	np = of_find_node_by_name(np, name);
	if (!np) {
		bd_warn("%s node does not exist, so create dummy\n", name);
		info = kzalloc(sizeof(struct action_info), GFP_KERNEL);
		list_add_tail(&info->node, head);
		return -EINVAL;
	}

	check_dt(np);

	count = of_property_count_strings(np, "type");

	for (i = 0; i < count; i++) {
		of_property_read_string_index(np, "type", i, &type);

		if (!lcdtype && !STRNEQ("delay", type) && !STRNEQ("timer", type)) {
			bd_info("lcdtype is zero, so skip to add %s: %2d: %s\n", name, count, type);
			continue;
		}

		info = kzalloc(sizeof(struct action_info), GFP_KERNEL);
		info->subtype = kstrdup(type, GFP_KERNEL);
		info->type = strsep(&info->subtype, ",");

		if (of_property_count_strings(np, "desc") == count)
			of_property_read_string_index(np, "desc", i, &info->desc);

		info->idx = decide_action(info->type, info->subtype);

		list_add_tail(&info->node, head);
	}

	list_for_each_entry(info, head, node) {
		switch (info->idx) {
		case ACTION_GPIO_HIGH:
		case ACTION_GPIO_LOW:
			info->gpio = of_get_gpio(np, gpio);
			if (!gpio_is_valid(info->gpio)) {
				bd_warn("of_get_gpio fail\n");
				ret = -EINVAL;
			}
			gpio++;
			break;
		case ACTION_REGULATOR_ENABLE:
		case ACTION_REGULATOR_DISABLE:
			info->supply = kzalloc(sizeof(struct regulator_bulk_data), GFP_KERNEL);
			of_property_read_string_index(np, "regulator", regulator, &info->supply->supply);
			ret = regulator_bulk_get(NULL, 1, info->supply);
			if (ret)
				bd_warn("regulator_bulk_get fail, %s %d\n", info->supply->supply, ret);
			regulator++;
			break;
		case ACTION_DELAY_MDELAY:
		case ACTION_DELAY_MSLEEP:
			ret = of_property_read_u32_index(np, "delay", delay, &info->delay[0]);
			if (ret)
				bd_warn("of_property_read_u32_index fail\n");
			delay++;
			break;
		case ACTION_DELAY_USLEEP:
			ret = of_property_read_u32_index(np, "delay", delay, &info->delay[0]);
			delay++;
			ret += of_property_read_u32_index(np, "delay", delay, &info->delay[1]);
			delay++;
			if (ret)
				bd_warn("of_property_read_u32_index fail\n");
			if (info->delay[0] >= MSEC_TO_USEC(20)) {
				bd_warn("use msleep instead of usleep for (%d)us\n", info->delay[0]);
				ret = -EINVAL;
			}
			break;
		case ACTION_PINCTRL:
			info->pins = devm_pinctrl_get(dev);
			if (IS_ERR(info->pins)) {
				bd_warn("devm_pinctrl_get fail\n");
				ret = -EINVAL;
			}
			info->state = pinctrl_lookup_state(info->pins, info->subtype);
			if (IS_ERR(info->pins)) {
				bd_warn("pinctrl_lookup_state fail, %s\n", info->subtype);
				ret = -EINVAL;
			}
			break;
		case ACTION_TIMER_START:
			timer_name = info->subtype;
			info->subtype = strsep(&timer_name, ",");
			info->timer = find_timer(timer_name);
			ret = of_property_read_u32_index(np, "delay", delay, &info->timer->delay);
			if (ret)
				bd_warn("of_property_read_u32_index fail\n");
			delay++;

			if (info->timer->delay < SMALL_MSECS) {
				bd_warn("use usleep instead of timer for (%d)ms\n", info->timer->delay);
				ret = -EINVAL;
			}
			break;
		case ACTION_TIMER_DELAY:
		case ACTION_TIMER_CLEAR:
			timer_name = info->subtype;
			info->subtype = strsep(&timer_name, ",");
			info->timer = find_timer(timer_name);
			break;
		default:
			bd_warn("idx: %d, type: %s, subtype: %s is invalid\n", info->idx, info->type, info->subtype);
			ret = -EINVAL;
			break;
		}
	}

#ifdef CONFIG_BOARD_DEBUG
	if (ret)
		BUG();
#endif

	return ret;
}

static int do_list(struct list_head *head)
{
	struct action_info *info;
	int ret = 0;
	u64 delta;

	list_for_each_entry(info, head, node) {
		switch (info->idx) {
		case ACTION_GPIO_HIGH:
			ret = gpio_request_one(info->gpio, GPIOF_OUT_INIT_HIGH, NULL);
			if (ret)
				bd_warn("gpio_request_one fail\n");
			gpio_free(info->gpio);
			break;
		case ACTION_GPIO_LOW:
			ret = gpio_request_one(info->gpio, GPIOF_OUT_INIT_LOW, NULL);
			if (ret)
				bd_warn("gpio_request_one fail\n");
			gpio_free(info->gpio);
			break;
		case ACTION_REGULATOR_ENABLE:
			ret = regulator_enable(info->supply->consumer);
			if (ret)
				bd_warn("regulator_enable fail, %s\n", info->supply->supply);
			break;
		case ACTION_REGULATOR_DISABLE:
			ret = regulator_disable(info->supply->consumer);
			if (ret)
				bd_warn("regulator_disable fail, %s\n", info->supply->supply);
			break;
		case ACTION_DELAY_MDELAY:
			mdelay(info->delay[0]);
			break;
		case ACTION_DELAY_MSLEEP:
			msleep(info->delay[0]);
			break;
		case ACTION_DELAY_USLEEP:
			usleep_range(info->delay[0], info->delay[1]);
			break;
		case ACTION_PINCTRL:
			pinctrl_select_state(info->pins, info->state);
			break;
		case ACTION_TIMER_START:
			info->timer->start = ns_to_ktime(local_clock());
			info->timer->target = ktime_add_ms(info->timer->start, info->timer->delay);
			break;
		case ACTION_TIMER_DELAY:
			info->timer->now = ns_to_ktime(local_clock());
			print_timer(info->timer);

			if (!info->timer->target.tv64)
				msleep(info->timer->delay);
			else if (info->timer->target.tv64 > info->timer->now.tv64) {
				delta = ktime_us_delta(info->timer->target, info->timer->now);

				if (!delta || delta > UINT_MAX)
					break;

				if (delta < MSEC_TO_USEC(SMALL_MSECS))
					usleep_range(delta, delta + (delta >> 1));
				else
					msleep(USEC_TO_MSEC(delta));
			}
		case ACTION_TIMER_CLEAR:
			info->timer->target.tv64 = 0;
			break;
		case ACTION_DUMMY:
			break;
		default:
			bd_warn("unknown idx(%d)\n", info->idx);
			ret = -EINVAL;
			break;
		}
	}

#ifdef CONFIG_BOARD_DEBUG
	if (ret)
		BUG();
#endif

	return ret;
}

static inline struct list_head *find_list(const char *name)
{
	struct bd_info *list = NULL;
	int idx = 0;

	bd_dbg("%s\n", name);
	while (!IS_ERR_OR_NULL(lists[idx])) {
		list = lists[idx];
		bd_dbg("%dth list name is %s\n", idx, list->name);
		if (STREQ(list->name, name))
			return &list->node;
		idx++;
		BUG_ON(idx == ARRAY_SIZE(lists));
	};

	bd_info("%s is not exist, so create it\n", name);
	list = kzalloc(sizeof(struct bd_info), GFP_KERNEL);
	list->name = kstrdup(name, GFP_KERNEL);
	INIT_LIST_HEAD(&list->node);

	lists[idx] = list;

	return &list->node;
}

void run_list(struct device *dev, const char *name)
{
	struct list_head *head = find_list(name);

	if (unlikely(list_empty(head))) {
		bd_info("%s is empty, so make list\n", name);
		make_list(dev, head, name);
		dump_list(head);
	}

	do_list(head);
}

