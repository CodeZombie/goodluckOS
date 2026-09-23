// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *          Henson Li <lidongsheng111@gmail.com>
 *
 * Modified by Jeremy Clark <https://github.com/CodeZombie> for use on the GA36-MB v1.2
 * The original version of this file can be found here: https://github.com/cutiepi-io/cutiepi-drivers/blob/master/Display/drivers/gpu/drm/panel/panel-jd9366.c
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#ifndef mipi_dsi_dcs_write_seq
#define mipi_dsi_dcs_write_seq(dsi, cmd, seq...)                                     \
	do {                                                                             \
		static const u8 d[] = { cmd, seq };                                          \
		int ret;                                                                     \
		ret = mipi_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d));                      \
		if (ret < 0) {                                                               \
			dev_err(&dsi->dev, "dcs write failed (cmd 0x%02x): %d\n", cmd, ret);     \
			return ret;                                                              \
		}                                                                            \
		if (ret < ARRAY_SIZE(d)) {                                                   \
		    dev_err(&dsi->dev, "dcs partial write: %d of %d\n", ret, ARRAY_SIZE(d)); \
			return -EIO;                                                             \
		}                                                                            \
	} while (0)
#endif

struct jd9366 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *supply;
	struct backlight_device *backlight;
	bool prepared;
	bool enabled;
};

static const struct drm_display_mode default_mode = {
    .clock = 30000,                 /* lcd_dclk_freq * 1000 */

    .hdisplay = 640,                /* lcd_x */
    .hsync_start = 640 + 280,       /* hdisplay + calculated front porch */
    .hsync_end = 640 + 280 + 40,    /* hsync_start + lcd_hspw */
    .htotal = 1040,                 /* lcd_ht */

    .vdisplay = 480,                /* lcd_y */
    .vsync_start = 480 + 26,        /* vdisplay + calculated front porch */
    .vsync_end = 480 + 26 + 6,      /* vsync_start + lcd_vspw */
    .vtotal = 518,                  /* lcd_vt */

    .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC, /* Standard MIPI DSI sync polarities */

    .width_mm = 71,
    .height_mm = 53,
};

static inline struct jd9366 *panel_to_jd9366(struct drm_panel *panel)
{
	return container_of(panel, struct jd9366, panel);
}


static int jd9366_init_sequence(struct mipi_dsi_device *dsi)
{
    // Reverse-engineered from `lcd.ko` in the original GA36-MB v1.2's firmware via decompilation/static analysis.

    //Page 0
    mipi_dsi_dcs_write_seq(dsi, 0xE0, 0x00);

    // Password?
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x01);
    // Page 0
    mipi_dsi_dcs_write_seq(dsi, 0xE3, 0x00);
    // ???
    mipi_dsi_dcs_write_seq(dsi, 0x20, 0x90);
    mipi_dsi_dcs_write_seq(dsi, 0x25, 0x10);
    mipi_dsi_dcs_write_seq(dsi, 0x28, 0x6F);
    mipi_dsi_dcs_write_seq(dsi, 0x29, 0x01);
    mipi_dsi_dcs_write_seq(dsi, 0x2A, 0xDF);
    mipi_dsi_dcs_write_seq(dsi, 0x30, 0x58);
    mipi_dsi_dcs_write_seq(dsi, 0x37, 0x9C);
    mipi_dsi_dcs_write_seq(dsi, 0x38, 0xA7);
    mipi_dsi_dcs_write_seq(dsi, 0x39, 0x53);
    mipi_dsi_dcs_write_seq(dsi, 0x44, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x49, 0x3C);
    mipi_dsi_dcs_write_seq(dsi, 0x59, 0xFE);
    mipi_dsi_dcs_write_seq(dsi, 0x5C, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x60, 0x8F);
    mipi_dsi_dcs_write_seq(dsi, 0x80, 0x20);
    mipi_dsi_dcs_write_seq(dsi, 0x91, 0x77);
    mipi_dsi_dcs_write_seq(dsi, 0x92, 0x77);
    mipi_dsi_dcs_write_seq(dsi, 0xA0, 0x55);
    mipi_dsi_dcs_write_seq(dsi, 0xA1, 0x50);
    mipi_dsi_dcs_write_seq(dsi, 0xA3, 0x58);
    mipi_dsi_dcs_write_seq(dsi, 0xA4, 0x9C);
    mipi_dsi_dcs_write_seq(dsi, 0xA7, 0x02);
    mipi_dsi_dcs_write_seq(dsi, 0xA8, 0x01);
    mipi_dsi_dcs_write_seq(dsi, 0xA9, 0x21);
    mipi_dsi_dcs_write_seq(dsi, 0xAA, 0xFC);
    mipi_dsi_dcs_write_seq(dsi, 0xAB, 0x28);
    mipi_dsi_dcs_write_seq(dsi, 0xAC, 0x06);
    mipi_dsi_dcs_write_seq(dsi, 0xAD, 0x06);
    mipi_dsi_dcs_write_seq(dsi, 0xAE, 0x06);
    mipi_dsi_dcs_write_seq(dsi, 0xAF, 0x03);
    mipi_dsi_dcs_write_seq(dsi, 0xB0, 0x08);
    mipi_dsi_dcs_write_seq(dsi, 0xB1, 0x26);
    mipi_dsi_dcs_write_seq(dsi, 0xB2, 0x28);
    mipi_dsi_dcs_write_seq(dsi, 0xB3, 0x28);
    mipi_dsi_dcs_write_seq(dsi, 0xB4, 0x03);
    mipi_dsi_dcs_write_seq(dsi, 0xB5, 0x08);
    mipi_dsi_dcs_write_seq(dsi, 0xB6, 0x26);
    mipi_dsi_dcs_write_seq(dsi, 0xB7, 0x08);
    mipi_dsi_dcs_write_seq(dsi, 0xB8, 0x26);

    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x02);

    mipi_dsi_dcs_write_seq(dsi, 0xB0, 0x0B);
    mipi_dsi_dcs_write_seq(dsi, 0xB1, 0x16);
    mipi_dsi_dcs_write_seq(dsi, 0xB2, 0x17);
    mipi_dsi_dcs_write_seq(dsi, 0xB3, 0x2C);
    mipi_dsi_dcs_write_seq(dsi, 0xB4, 0x32);
    mipi_dsi_dcs_write_seq(dsi, 0xB5, 0x3B);
    mipi_dsi_dcs_write_seq(dsi, 0xB6, 0x29);
    mipi_dsi_dcs_write_seq(dsi, 0xB7, 0x40);
    mipi_dsi_dcs_write_seq(dsi, 0xB8, 0x0D);
    mipi_dsi_dcs_write_seq(dsi, 0xB9, 0x05);
    mipi_dsi_dcs_write_seq(dsi, 0xBA, 0x12);
    mipi_dsi_dcs_write_seq(dsi, 0xBB, 0x10);
    mipi_dsi_dcs_write_seq(dsi, 0xBC, 0x12);
    mipi_dsi_dcs_write_seq(dsi, 0xBD, 0x15);
    mipi_dsi_dcs_write_seq(dsi, 0xBE, 0x19);
    mipi_dsi_dcs_write_seq(dsi, 0xBF, 0x0E);
    mipi_dsi_dcs_write_seq(dsi, 0xC0, 0x16);
    mipi_dsi_dcs_write_seq(dsi, 0xC1, 0x0A);
    mipi_dsi_dcs_write_seq(dsi, 0xD0, 0x0C);
    mipi_dsi_dcs_write_seq(dsi, 0xD1, 0x17);
    mipi_dsi_dcs_write_seq(dsi, 0xD2, 0x14);
    mipi_dsi_dcs_write_seq(dsi, 0xD3, 0x2E);
    mipi_dsi_dcs_write_seq(dsi, 0xD4, 0x32);
    mipi_dsi_dcs_write_seq(dsi, 0xD5, 0x3C);
    mipi_dsi_dcs_write_seq(dsi, 0xD6, 0x22);
    mipi_dsi_dcs_write_seq(dsi, 0xD7, 0x3D);
    mipi_dsi_dcs_write_seq(dsi, 0xD8, 0x0D);
    mipi_dsi_dcs_write_seq(dsi, 0xD9, 0x07);
    mipi_dsi_dcs_write_seq(dsi, 0xDA, 0x13);
    mipi_dsi_dcs_write_seq(dsi, 0xDB, 0x13);
    mipi_dsi_dcs_write_seq(dsi, 0xDC, 0x11);
    mipi_dsi_dcs_write_seq(dsi, 0xDD, 0x15);
    mipi_dsi_dcs_write_seq(dsi, 0xDE, 0x19);
    mipi_dsi_dcs_write_seq(dsi, 0xDF, 0x10);
    mipi_dsi_dcs_write_seq(dsi, 0xE0, 0x17);
    mipi_dsi_dcs_write_seq(dsi, 0xE1, 0x0A);

    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x03);

    mipi_dsi_dcs_write_seq(dsi, 0x00, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x01, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x02, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x03, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x04, 0x61);
    mipi_dsi_dcs_write_seq(dsi, 0x05, 0x80);
    mipi_dsi_dcs_write_seq(dsi, 0x06, 0xC7);
    mipi_dsi_dcs_write_seq(dsi, 0x07, 0x01);
    mipi_dsi_dcs_write_seq(dsi, 0x08, 0x82);
    mipi_dsi_dcs_write_seq(dsi, 0x09, 0x83);
    mipi_dsi_dcs_write_seq(dsi, 0x30, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x31, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x32, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x33, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x34, 0x61);
    mipi_dsi_dcs_write_seq(dsi, 0x35, 0xC5);
    mipi_dsi_dcs_write_seq(dsi, 0x36, 0x80);
    mipi_dsi_dcs_write_seq(dsi, 0x37, 0x23);
    mipi_dsi_dcs_write_seq(dsi, 0x40, 0x82);
    mipi_dsi_dcs_write_seq(dsi, 0x41, 0x83);
    mipi_dsi_dcs_write_seq(dsi, 0x42, 0x80);
    mipi_dsi_dcs_write_seq(dsi, 0x43, 0x81);
    mipi_dsi_dcs_write_seq(dsi, 0x44, 0x11);
    mipi_dsi_dcs_write_seq(dsi, 0x45, 0xE2);
    mipi_dsi_dcs_write_seq(dsi, 0x46, 0xE1);
    mipi_dsi_dcs_write_seq(dsi, 0x47, 0x11);
    mipi_dsi_dcs_write_seq(dsi, 0x48, 0xE4);
    mipi_dsi_dcs_write_seq(dsi, 0x49, 0xE3);
    mipi_dsi_dcs_write_seq(dsi, 0x50, 0x02);
    mipi_dsi_dcs_write_seq(dsi, 0x51, 0x01);
    mipi_dsi_dcs_write_seq(dsi, 0x52, 0x04);
    mipi_dsi_dcs_write_seq(dsi, 0x53, 0x03);
    mipi_dsi_dcs_write_seq(dsi, 0x54, 0x11);
    mipi_dsi_dcs_write_seq(dsi, 0x55, 0xE6);
    mipi_dsi_dcs_write_seq(dsi, 0x56, 0xE5);
    mipi_dsi_dcs_write_seq(dsi, 0x57, 0x11);
    mipi_dsi_dcs_write_seq(dsi, 0x58, 0xE8);
    mipi_dsi_dcs_write_seq(dsi, 0x59, 0xE7);
    mipi_dsi_dcs_write_seq(dsi, 0x7E, 0x08);
    mipi_dsi_dcs_write_seq(dsi, 0x81, 0x0F);
    mipi_dsi_dcs_write_seq(dsi, 0x84, 0x0C);
    mipi_dsi_dcs_write_seq(dsi, 0x85, 0x0D);
    mipi_dsi_dcs_write_seq(dsi, 0x86, 0x07);
    mipi_dsi_dcs_write_seq(dsi, 0x87, 0x04);
    mipi_dsi_dcs_write_seq(dsi, 0x88, 0x05);
    mipi_dsi_dcs_write_seq(dsi, 0x89, 0x06);
    mipi_dsi_dcs_write_seq(dsi, 0x8A, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x97, 0x0F);
    mipi_dsi_dcs_write_seq(dsi, 0x9A, 0x0C);
    mipi_dsi_dcs_write_seq(dsi, 0x9B, 0x0D);
    mipi_dsi_dcs_write_seq(dsi, 0x9C, 0x07);
    mipi_dsi_dcs_write_seq(dsi, 0x9D, 0x04);
    mipi_dsi_dcs_write_seq(dsi, 0x9E, 0x05);
    mipi_dsi_dcs_write_seq(dsi, 0x9F, 0x06);
    mipi_dsi_dcs_write_seq(dsi, 0xA0, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0xE0, 0x02);
    mipi_dsi_dcs_write_seq(dsi, 0xE1, 0x52);

    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x30);
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x52);
    mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x00);
    mipi_dsi_dcs_write_seq(dsi, 0x36, 0x02);

    // Not sure if these are necessary, or just duplicates of `gpiod_set_value_cansleep` in `prepare`
    mipi_dsi_dcs_write_seq(dsi, 0x11);
    msleep(120);
    mipi_dsi_dcs_write_seq(dsi, 0x29);
    msleep(20);

    // TE -- do we need this?????
    //mipi_dsi_dcs_write_seq(dsi, 0x35, 0x00);
    return 0;
}

static int jd9366_disable(struct drm_panel *panel)
{
	struct jd9366 *ctx = panel_to_jd9366(panel);

	if (!ctx->enabled)
		return 0;

	backlight_disable(ctx->backlight);

	ctx->enabled = false;

	return 0;
}

static int jd9366_unprepare(struct drm_panel *panel)
{
	struct jd9366 *ctx = panel_to_jd9366(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret)
		return ret;

	msleep(120);

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
	}

	regulator_disable(ctx->supply);

	ctx->prepared = false;

	return 0;
}

static int jd9366_prepare(struct drm_panel *panel)
{
    struct jd9366 *ctx = panel_to_jd9366(panel);
    struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
    int ret;

    if (ctx->prepared) {
        dev_info(panel->dev, "Panel already prepared. Exiting early.\n");
        return 0;
    }

    ret = regulator_enable(ctx->supply);
    if (ret < 0) return ret;

    msleep(120);

    if (ctx->reset_gpio) {
        gpiod_set_value_cansleep(ctx->reset_gpio, 1);
        usleep_range(10000, 15000);

        gpiod_set_value_cansleep(ctx->reset_gpio, 0);
        msleep(120);
    }

    jd9366_init_sequence(dsi);


    ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
    dev_info(panel->dev, "mipi_dsi_dcs_exit_sleep_mode() finished: %d\n", ret);
    if (ret) return ret;


    ret = mipi_dsi_dcs_set_display_on(dsi);
    dev_info(panel->dev, "mipi_dsi_dcs_set_display_on() finished: %d\n", ret);
    if (ret) return ret;

    msleep(20);

    ctx->prepared = true;
    dev_info(panel->dev, "jd9366_prepare() SUCCESS.\n");

    return 0;
}

static int jd9366_enable(struct drm_panel *panel)
{
	struct jd9366 *ctx = panel_to_jd9366(panel);

    if (ctx->enabled)
        return 0;

    backlight_enable(ctx->backlight);
    ctx->enabled = true;

	return 0;
}

static int jd9366_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay,
			default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs jd9366_drm_funcs = {
	.disable = jd9366_disable,
	.unprepare = jd9366_unprepare,
	.prepare = jd9366_prepare,
	.enable = jd9366_enable,
	.get_modes = jd9366_get_modes,
};

static int jd9366_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jd9366 *ctx;
	int ret;
	u8 mode;

	dev_info(dev, "Starting probe...");

    ctx = devm_drm_panel_alloc(dev, struct jd9366, panel, &jd9366_drm_funcs, DRM_MODE_CONNECTOR_DSI);
    if (IS_ERR(ctx)) {
        return PTR_ERR(ctx);
    }

    ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
    if (IS_ERR(ctx->reset_gpio)) {
        return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "cannot get reset GPIO\n");
    }

    ctx->supply = devm_regulator_get(dev, "power");
    if (IS_ERR(ctx->supply)) {
        return dev_err_probe(dev, PTR_ERR(ctx->supply), "cannot get regulator\n");
    }

    ctx->backlight = devm_of_find_backlight(dev);
    if (IS_ERR(ctx->backlight)) {
        return dev_err_probe(dev, PTR_ERR(ctx->backlight), "cannot get backlight\n");
    }

    ctx->dev = dev;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ret = regulator_enable(ctx->supply);
	if (ret < 0)
		return ret;

	msleep(20);

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(10);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(20);
	}

	// This is a hack which initializes the DCS bus so we can probe the display to see if
	// it'll turn on or not.
	// In my testing this has reliably predicted whether or not the display will actually initialize.
	//
	// We want to check this here so that we can defer probing so the device doesn't think the doomed DCS bus
	// is ready and register a panel it can't communicate with. If that happens, we get massively long DCS timeouts
	// which causes the device to take 2+ minutes to get into userspace, which isn't very cool.
	ret = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_POWER_MODE, &mode, 1);
	if (ret < 0) {
	    int probe_success = 0;
	    int i = 0;
		for (i=0; i<4; i++) {

		    regulator_disable(ctx->supply);
			msleep(20);
            regulator_enable(ctx->supply);
            msleep(20);
            if (ctx->reset_gpio) {
    			gpiod_set_value_cansleep(ctx->reset_gpio, 1);
    			msleep(20);
    			gpiod_set_value_cansleep(ctx->reset_gpio, 0);
                msleep(20);
    		}
            ret = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_POWER_MODE, &mode, 1);
            if (ret < 0) {
                continue;
            }
            probe_success = 1;
		}

		if (probe_success == 0) {
		    // stick a very-identifiable flag in the kernel logs so we can identify this failure state in userspace.
    		dev_err(dev, "XXGOODLUCKOSHACK::MIPI_DSI_PROBE_READ::%d\n", ret);
    		if (ctx->reset_gpio)
    			gpiod_set_value_cansleep(ctx->reset_gpio, 1);
    		regulator_disable(ctx->supply);
    		return -ENODEV;
		}
	}

	// If we make it to this point, the display will initialize.
	ctx->panel.prepare_prev_first = true;
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach() failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}


static void jd9366_remove(struct mipi_dsi_device *dsi)
{
	struct jd9366 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_jd9366_of_match[] = {
	{ .compatible = "boe,jd9366" },
	{ }
};
MODULE_DEVICE_TABLE(of, boe_jd9366_of_match);

static struct mipi_dsi_driver boe_jd9366_driver = {
	.probe = jd9366_probe,
	.remove = jd9366_remove,
	.driver = {
		.name = "panel-boe-jd9366",
		.of_match_table = boe_jd9366_of_match,
	},
};
module_mipi_dsi_driver(boe_jd9366_driver);

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_AUTHOR("Henson Li <lidongsheng111@gmail.com>");
MODULE_DESCRIPTION("DRM Driver for BOE JD9366 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
