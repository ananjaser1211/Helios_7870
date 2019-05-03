


#ifndef __SMS3230_H__
#define __SMS3230_H__

#ifdef __cplusplus
extern "C" {
#endif



//TODO:

#define IOCTL_MAGIC 't'


#define IOCTL_ISDBT_RESET	\
	_IO(IOCTL_MAGIC, 0)




#define IOCTL_ISDBT_POWER_ON	\
	_IO(IOCTL_MAGIC, 23)
#define IOCTL_ISDBT_POWER_OFF	\
	_IO(IOCTL_MAGIC, 24)




#if 0
extern void isdbt_set_drv_mode(unsigned int mode);
#ifdef USE_THREADED_IRQ
extern irqreturn_t isdbt_threaded_irq(int irq, void *dev_id);
#else
extern irqreturn_t isdbt_irq(int irq, void *dev_id);
#endif
extern int isdbt_drv_open(struct inode *inode, struct file *filp);
extern ssize_t isdbt_drv_read(struct file *filp
	, char *buf, size_t count, loff_t *f_pos);
extern int isdbt_drv_release(struct inode *inode, struct file *filp);
extern long isdbt_drv_ioctl(struct file *filp
	, unsigned int cmd, unsigned long arg);
extern void isdbt_drv_probe(void);
extern void isdbt_drv_remove(void);
extern void isdbt_drv_mmap(struct file *filp, struct vm_area_struct *vma);
#endif/*0*/




#ifdef __cplusplus
}
#endif

#endif /* __SMS3230_H__ */
