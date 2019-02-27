/* to Support headset detect function for factory 15 mode. KSND  */
static ssize_t earjack_state_show(struct device *dev,
                                      struct device_attribute *attr, char *buf)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);
    struct cod3026x_jack_det *jackdet = &cod3026x->jack_det;
    int status = jackdet->jack_det;
    int report = 0;

    if (status) {
        report = 1;
    }
    return sprintf(buf, "%d\n", report);
}

static ssize_t earjack_state_store(struct device *dev,
                    struct device_attribute *attr, const char *buf, size_t size)
{
    return size;
}

static ssize_t earjack_key_state_show(struct device *dev,
                                      struct device_attribute *attr, char *buf)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);
    struct cod3026x_jack_det *jackdet = &cod3026x->jack_det;
    int report = 0;

    report = jackdet->button_det ? true : false;

    return sprintf(buf, "%d\n", report);
}

static ssize_t earjack_key_state_store(struct device *dev,
                  struct device_attribute *attr, const char *buf, size_t size)
{
    return size;
}

static ssize_t earjack_select_jack_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    return 0;
}

static ssize_t earjack_select_jack_store(struct device *dev,
                struct device_attribute *attr, const char *buf, size_t size)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

    if ((!size) || (buf[0] != '1')) {
        switch_set_state(&cod3026x->sdev, 0);
    } else {
        switch_set_state(&cod3026x->sdev, 1);
    }

    return size;
}

static ssize_t earjack_mic_adc_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);
    struct cod3026x_jack_det *jackdet = &cod3026x->jack_det;

    return sprintf(buf, "%d\n", jackdet->adc_val);
}

static ssize_t earjack_mic_adc_store(struct device *dev,
                struct device_attribute *attr, const char *buf, size_t size)
{
    return size;
}

#if defined (SEC_SYSFS_ADC_EARJACK)
static ssize_t jack_adc_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

    int val[4] = {0,};

    val[1] = cod3026x->mic_adc_range -1;
    val[2] = cod3026x->mic_adc_range;
    val[3] = 9999;

    return sprintf(buf, "%d %d %d %d\n",val[0],val[1],val[2],val[3]);
}

static ssize_t hook_adc_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

    int val[2]  = {0,};

    val[0] =  cod3026x->jack_buttons_zones[0].adc_low;
    val[1] =  cod3026x->jack_buttons_zones[0].adc_high;

    return sprintf(buf, "%d %d\n",val[0],val[1]);
}

static ssize_t voc_ast_adc_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

    int val[2]  = {0,};

    val[0] =  cod3026x->jack_buttons_zones[1].adc_low;
    val[1] =  cod3026x->jack_buttons_zones[1].adc_high;

    return sprintf(buf, "%d %d\n",val[0],val[1]);
}

static ssize_t volup_adc_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

    int val[2]  = {0,};

    val[0] =  cod3026x->jack_buttons_zones[2].adc_low;
    val[1] =  cod3026x->jack_buttons_zones[2].adc_high;

    return sprintf(buf, "%d %d\n",val[0],val[1]);
}

static ssize_t voldown_adc_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

    int val[2]  = {0,};

    val[0] =  cod3026x->jack_buttons_zones[3].adc_low;
    val[1] =  cod3026x->jack_buttons_zones[3].adc_high;

    return sprintf(buf, "%d %d\n",val[0],val[1]);
}
#endif

static DEVICE_ATTR(select_jack, S_IRUGO | S_IWUSR | S_IWGRP,
                        earjack_select_jack_show, earjack_select_jack_store);

static DEVICE_ATTR(key_state, S_IRUGO | S_IWUSR | S_IWGRP,
                        earjack_key_state_show, earjack_key_state_store);

static DEVICE_ATTR(state, S_IRUGO | S_IWUSR | S_IWGRP,
                        earjack_state_show, earjack_state_store);

static DEVICE_ATTR(mic_adc, S_IRUGO | S_IWUSR | S_IWGRP,
                        earjack_mic_adc_show, earjack_mic_adc_store);

#if defined (SEC_SYSFS_ADC_EARJACK)
static DEVICE_ATTR(jacks_adc, S_IRUGO, jack_adc_show, NULL);
static DEVICE_ATTR(send_end_btn_adc, S_IRUGO, hook_adc_show, NULL);
static DEVICE_ATTR(voc_assist_btn_adc, S_IRUGO, voc_ast_adc_show, NULL);
static DEVICE_ATTR(vol_up_btn_adc, S_IRUGO, volup_adc_show, NULL);
static DEVICE_ATTR(vol_down_btn_adc, S_IRUGO, voldown_adc_show, NULL);
#endif

static void create_jack_devices(struct cod3026x_priv *info)
{
    static struct class *jack_class;
    static struct device *jack_dev;

    jack_class = class_create(THIS_MODULE, "audio");

    if (IS_ERR(jack_class)) {
        pr_err("Failed to create class\n");
    }
    jack_dev = device_create(jack_class, NULL, 0, info, "earjack");

    if (device_create_file(jack_dev, &dev_attr_select_jack) < 0) {
        pr_err("Failed to create (%s)\n", dev_attr_select_jack.attr.name);
    }

    if (device_create_file(jack_dev, &dev_attr_key_state) < 0){
        pr_err("Failed to create (%s)\n", dev_attr_key_state.attr.name);
    }

    if (device_create_file(jack_dev, &dev_attr_state) < 0){
        pr_err("Failed to create (%s)\n", dev_attr_state.attr.name);
    }

    if (device_create_file(jack_dev, &dev_attr_mic_adc) < 0){
        pr_err("Failed to create (%s)\n", dev_attr_mic_adc.attr.name);
    }

#if defined (SEC_SYSFS_ADC_EARJACK)
    if (device_create_file(jack_dev, &dev_attr_jacks_adc) < 0){
        pr_err("Failed to create (%s)\n", dev_attr_jacks_adc.attr.name);
    }

    if (device_create_file(jack_dev, &dev_attr_send_end_btn_adc) < 0){
        pr_err("Failed to create (%s)\n", dev_attr_send_end_btn_adc.attr.name);
    }

    if (device_create_file(jack_dev, &dev_attr_voc_assist_btn_adc) < 0){
        pr_err("Failed to create (%s)\n", dev_attr_voc_assist_btn_adc.attr.name);
    }

    if (device_create_file(jack_dev, &dev_attr_vol_up_btn_adc) < 0){
        pr_err("Failed to create (%s)\n", dev_attr_vol_up_btn_adc.attr.name);
    }

    if (device_create_file(jack_dev, &dev_attr_vol_down_btn_adc) < 0){
        pr_err("Failed to create (%s)\n", dev_attr_vol_down_btn_adc.attr.name);
    }
#endif
}

