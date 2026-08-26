#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/workqueue.h>

struct gamepad {
    struct input_dev *idev;
    struct serdev_device *serdev;

    struct gpio_desc *gpio_up;
    struct gpio_desc *gpio_down;
    struct gpio_desc *gpio_left;
    struct gpio_desc *gpio_right;

    struct gpio_desc *gpio_a;
    struct gpio_desc *gpio_b;
    struct gpio_desc *gpio_x;
    struct gpio_desc *gpio_y;

    struct gpio_desc *gpio_rb;
    struct gpio_desc *gpio_rt;
    struct gpio_desc *gpio_lb;
    struct gpio_desc *gpio_lt;

    struct gpio_desc *gpio_thumbr;
    struct gpio_desc *gpio_thumbl;

    struct gpio_desc *gpio_start;
    struct gpio_desc *gpio_select;
    struct gpio_desc *gpio_fn;

    struct delayed_work poll_work;

    u8 packet[8];
    int idx;
};

static void gamepad_poll(struct work_struct *work) {
    struct gamepad *gp = container_of(work, struct gamepad, poll_work.work);

    /* D-Pad */
    if (gp->gpio_up) input_report_key(gp->idev, BTN_DPAD_UP, gpiod_get_value_cansleep(gp->gpio_up));
    if (gp->gpio_down) input_report_key(gp->idev, BTN_DPAD_DOWN, gpiod_get_value_cansleep(gp->gpio_down));
    if (gp->gpio_left) input_report_key(gp->idev, BTN_DPAD_LEFT, gpiod_get_value_cansleep(gp->gpio_left));
    if (gp->gpio_right) input_report_key(gp->idev, BTN_DPAD_RIGHT, gpiod_get_value_cansleep(gp->gpio_right));

    /* Game Buttons */
    if (gp->gpio_a) input_report_key(gp->idev, BTN_A, gpiod_get_value_cansleep(gp->gpio_a));
    if (gp->gpio_b) input_report_key(gp->idev, BTN_B, gpiod_get_value_cansleep(gp->gpio_b));
    if (gp->gpio_x) input_report_key(gp->idev, BTN_X, gpiod_get_value_cansleep(gp->gpio_x));
    if (gp->gpio_y) input_report_key(gp->idev, BTN_Y, gpiod_get_value_cansleep(gp->gpio_y));

    /* Bumpers and Triggers */
    if (gp->gpio_rb) input_report_key(gp->idev, BTN_TR, gpiod_get_value_cansleep(gp->gpio_rb));
    if (gp->gpio_rt) input_report_key(gp->idev, BTN_TR2, gpiod_get_value_cansleep(gp->gpio_rt));
    if (gp->gpio_lb) input_report_key(gp->idev, BTN_TL, gpiod_get_value_cansleep(gp->gpio_lb));
    if (gp->gpio_lt) input_report_key(gp->idev, BTN_TL2, gpiod_get_value_cansleep(gp->gpio_lt));

    /* Thumbstick Clicks */
    if (gp->gpio_thumbr) input_report_key(gp->idev, BTN_THUMBR, gpiod_get_value_cansleep(gp->gpio_thumbr));
    if (gp->gpio_thumbl) input_report_key(gp->idev, BTN_THUMBL, gpiod_get_value_cansleep(gp->gpio_thumbl));

    /* System Buttons */
    if (gp->gpio_start) input_report_key(gp->idev, BTN_START, gpiod_get_value_cansleep(gp->gpio_start));
    if (gp->gpio_select) input_report_key(gp->idev, BTN_SELECT, gpiod_get_value_cansleep(gp->gpio_select));
    if (gp->gpio_fn) input_report_key(gp->idev, BTN_MODE, gpiod_get_value_cansleep(gp->gpio_fn));

    /* Sync events */
    input_sync(gp->idev);

    /* re-runn in 16ms */
    schedule_delayed_work(&gp->poll_work, msecs_to_jiffies(16));
}

static size_t serial_analog_sticks_receive_buf(struct serdev_device *serdev, const unsigned char *buf, size_t count) {
    struct gamepad *gp = serdev_device_get_drvdata(serdev);
    size_t i;

    for (i = 0; i < count; i++) {
        gp->packet[gp->idx] = buf[i];

        /* Sync Byte 0 */
        if (gp->idx == 0 && gp->packet[0] != 0xA7)
            continue;

        /* Sync Byte 1 */
        if (gp->idx == 1 && gp->packet[1] != 0x10) {
            gp->idx = 0;
            continue;
        }

        gp->idx++;

        /* if full packet received */
        if (gp->idx == 8) {
            if (gp->packet[7] == 0x00) {

                // The left stick is physically inverted compared to the right stick.
                input_report_abs(gp->idev, ABS_X,  255 - gp->packet[3]);
                input_report_abs(gp->idev, ABS_Y,  255 - gp->packet[4]);
                input_report_abs(gp->idev, ABS_RX, gp->packet[5]);
                input_report_abs(gp->idev, ABS_RY, gp->packet[6]);

                input_sync(gp->idev);
            }
            gp->idx = 0;
        }
    }
    return count;
}

static const struct serdev_device_ops serial_analog_sticks_ops = {
    .receive_buf = serial_analog_sticks_receive_buf,
    .write_wakeup = serdev_device_write_wakeup,
};

static int gamepad_probe(struct serdev_device *serdev)
{
    struct gamepad *gp;
    int err;
    u8 auth_init[] = {0xA6, 0x01, 0x00, 0x11, 0x22, 0x33};
    u8 auth_keepalive[] = {0xA6, 0x01, 0x01, 0x01, 0x01, 0x01};

    gp = devm_kzalloc(&serdev->dev, sizeof(*gp), GFP_KERNEL);
    if (!gp)
        return -ENOMEM;

    gp->serdev = serdev;
    serdev_device_set_drvdata(serdev, gp);
    serdev_device_set_client_ops(serdev, &serial_analog_sticks_ops);

    err = serdev_device_open(serdev);
    if (err) {
        dev_err(&serdev->dev, "Unable to open serial device\n");
        return err;
    }

    serdev_device_set_baudrate(serdev, 9600);
    serdev_device_set_flow_control(serdev, false);

    gp->idev = devm_input_allocate_device(&serdev->dev);
    if (!gp->idev) {
        err = -ENOMEM;
        goto err_close;
    }

    gp->idev->name = "GA36-MB Gamepad";
    gp->idev->id.bustype = BUS_HOST;

    /* Setup Analog Sticks */
    input_set_abs_params(gp->idev, ABS_X,  0, 255, 15, 2);
    input_set_abs_params(gp->idev, ABS_Y,  0, 255, 15, 2);
    input_set_abs_params(gp->idev, ABS_RX, 0, 255, 15, 2);
    input_set_abs_params(gp->idev, ABS_RY, 0, 255, 15, 2);

    /* Setup Buttons */
    input_set_capability(gp->idev, EV_KEY, BTN_DPAD_UP);
    input_set_capability(gp->idev, EV_KEY, BTN_DPAD_DOWN);
    input_set_capability(gp->idev, EV_KEY, BTN_DPAD_LEFT);
    input_set_capability(gp->idev, EV_KEY, BTN_DPAD_RIGHT);

    input_set_capability(gp->idev, EV_KEY, BTN_A);
    input_set_capability(gp->idev, EV_KEY, BTN_B);
    input_set_capability(gp->idev, EV_KEY, BTN_X);
    input_set_capability(gp->idev, EV_KEY, BTN_Y);

    input_set_capability(gp->idev, EV_KEY, BTN_TR);
    input_set_capability(gp->idev, EV_KEY, BTN_TR2);
    input_set_capability(gp->idev, EV_KEY, BTN_TL);
    input_set_capability(gp->idev, EV_KEY, BTN_TL2);

    input_set_capability(gp->idev, EV_KEY, BTN_THUMBR);
    input_set_capability(gp->idev, EV_KEY, BTN_THUMBL);

    input_set_capability(gp->idev, EV_KEY, BTN_START);
    input_set_capability(gp->idev, EV_KEY, BTN_SELECT);
    input_set_capability(gp->idev, EV_KEY, BTN_MODE);

    /* Request GPIOs from Device Tree */
    gp->gpio_up = devm_gpiod_get_optional(&serdev->dev, "dpad-up", GPIOD_IN);
    if (IS_ERR(gp->gpio_up)) {
        dev_err(&serdev->dev, "Failed to get \"up\" GPIO\n");
        err = PTR_ERR(gp->gpio_up);
        goto err_close;
    }

    gp->gpio_down = devm_gpiod_get_optional(&serdev->dev, "dpad-down", GPIOD_IN);
    if (IS_ERR(gp->gpio_down)) {
        dev_err(&serdev->dev, "Failed to get \"down\" GPIO\n");
        err = PTR_ERR(gp->gpio_down);
        goto err_close;
    }

    gp->gpio_left = devm_gpiod_get_optional(&serdev->dev, "dpad-left", GPIOD_IN);
    if (IS_ERR(gp->gpio_left)) {
        dev_err(&serdev->dev, "Failed to get \"left\" GPIO\n");
        err = PTR_ERR(gp->gpio_left);
        goto err_close;
    }

    gp->gpio_right = devm_gpiod_get_optional(&serdev->dev, "dpad-right", GPIOD_IN);
    if (IS_ERR(gp->gpio_right)) {
        dev_err(&serdev->dev, "Failed to get \"right\" GPIO\n");
        err = PTR_ERR(gp->gpio_right);
        goto err_close;
    }

    gp->gpio_a = devm_gpiod_get_optional(&serdev->dev, "btn-a", GPIOD_IN);
    if (IS_ERR(gp->gpio_a)) {
        dev_err(&serdev->dev, "Failed to get \"a\" GPIO\n");
        err = PTR_ERR(gp->gpio_a);
        goto err_close;
    }

    gp->gpio_b = devm_gpiod_get_optional(&serdev->dev, "btn-b", GPIOD_IN);
    if (IS_ERR(gp->gpio_b)) {
        dev_err(&serdev->dev, "Failed to get \"b\" GPIO\n");
        err = PTR_ERR(gp->gpio_b);
        goto err_close;
    }

    gp->gpio_x = devm_gpiod_get_optional(&serdev->dev, "btn-x", GPIOD_IN);
    if (IS_ERR(gp->gpio_x)) {
        dev_err(&serdev->dev, "Failed to get \"x\" GPIO\n");
        err = PTR_ERR(gp->gpio_x);
        goto err_close;
    }

    gp->gpio_y = devm_gpiod_get_optional(&serdev->dev, "btn-y", GPIOD_IN);
    if (IS_ERR(gp->gpio_y)) {
        dev_err(&serdev->dev, "Failed to get \"y\" GPIO\n");
        err = PTR_ERR(gp->gpio_y);
        goto err_close;
    }

    gp->gpio_rb = devm_gpiod_get_optional(&serdev->dev, "btn-rb", GPIOD_IN);
    if (IS_ERR(gp->gpio_rb)) {
        dev_err(&serdev->dev, "Failed to get \"rb\" GPIO\n");
        err = PTR_ERR(gp->gpio_rb);
        goto err_close;
    }

    gp->gpio_rt = devm_gpiod_get_optional(&serdev->dev, "btn-rt", GPIOD_IN);
    if (IS_ERR(gp->gpio_rt)) {
        dev_err(&serdev->dev, "Failed to get \"rt\" GPIO\n");
        err = PTR_ERR(gp->gpio_rt);
        goto err_close;
    }

    gp->gpio_lb = devm_gpiod_get_optional(&serdev->dev, "btn-lb", GPIOD_IN);
    if (IS_ERR(gp->gpio_lb)) {
        dev_err(&serdev->dev, "Failed to get \"lb\" GPIO\n");
        err = PTR_ERR(gp->gpio_lb);
        goto err_close;
    }

    gp->gpio_lt = devm_gpiod_get_optional(&serdev->dev, "btn-lt", GPIOD_IN);
    if (IS_ERR(gp->gpio_lt)) {
        dev_err(&serdev->dev, "Failed to get \"lt\" GPIO\n");
        err = PTR_ERR(gp->gpio_lt);
        goto err_close;
    }

    gp->gpio_thumbr = devm_gpiod_get_optional(&serdev->dev, "btn-thumbr", GPIOD_IN);
    if (IS_ERR(gp->gpio_thumbr)) {
        dev_err(&serdev->dev, "Failed to get \"thumbr\" GPIO\n");
        err = PTR_ERR(gp->gpio_thumbr);
        goto err_close;
    }

    gp->gpio_thumbl = devm_gpiod_get_optional(&serdev->dev, "btn-thumbl", GPIOD_IN);
    if (IS_ERR(gp->gpio_thumbl)) {
        dev_err(&serdev->dev, "Failed to get \"thumbl\" GPIO\n");
        err = PTR_ERR(gp->gpio_thumbl);
        goto err_close;
    }

    gp->gpio_start = devm_gpiod_get_optional(&serdev->dev, "btn-start", GPIOD_IN);
    if (IS_ERR(gp->gpio_start)) {
        dev_err(&serdev->dev, "Failed to get \"start\" GPIO\n");
        err = PTR_ERR(gp->gpio_start);
        goto err_close;
    }

    gp->gpio_select = devm_gpiod_get_optional(&serdev->dev, "btn-select", GPIOD_IN);
    if (IS_ERR(gp->gpio_select)) {
        dev_err(&serdev->dev, "Failed to get \"select\" GPIO\n");
        err = PTR_ERR(gp->gpio_select);
        goto err_close;
    }

    gp->gpio_fn = devm_gpiod_get_optional(&serdev->dev, "btn-fn", GPIOD_IN);
    if (IS_ERR(gp->gpio_fn)) {
        dev_err(&serdev->dev, "Failed to get \"fn\" GPIO\n");
        err = PTR_ERR(gp->gpio_fn);
        goto err_close;
    }


    err = input_register_device(gp->idev);
    if (err) {
        dev_err(&serdev->dev, "Failed to register input device\n");
        goto err_close;
    }

    /* Start the GPIO polling loop */
    INIT_DELAYED_WORK(&gp->poll_work, gamepad_poll);
    schedule_delayed_work(&gp->poll_work, msecs_to_jiffies(16));

    /* Send auth and then keepalive packets to the secondary MCU via serial */
    serdev_device_write_buf(serdev, auth_init, sizeof(auth_init));
    msleep(100);
    serdev_device_write_buf(serdev, auth_keepalive, sizeof(auth_keepalive));

    dev_info(&serdev->dev, "GA36-MB Gamepad Driver Initialized\n");
    return 0;

err_close:
    serdev_device_close(serdev);
    return err;
}

static void gamepad_remove(struct serdev_device *serdev)
{
    struct gamepad *gp = serdev_device_get_drvdata(serdev);

    cancel_delayed_work_sync(&gp->poll_work);

    serdev_device_close(serdev);
}

static const struct of_device_id gamepad_of_match[] = {
    { .compatible = "ga36mb,ga36-gamepad" },
    { }
};
MODULE_DEVICE_TABLE(of, gamepad_of_match);

static struct serdev_device_driver udt_gamepad_driver = {
    .driver = {
        .name = "ga36-gamepad",
        .of_match_table = gamepad_of_match,
    },
    .probe = gamepad_probe,
    .remove = gamepad_remove,
};
module_serdev_device_driver(udt_gamepad_driver);

MODULE_AUTHOR("Jeremy Clark");
MODULE_DESCRIPTION("GA36-MB Gamepad Driver");
MODULE_LICENSE("GPL");
