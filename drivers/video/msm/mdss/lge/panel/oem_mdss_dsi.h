/* Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef OEM_MDSS_DSI_H
#define OEM_MDSS_DSI_H

#include <linux/list.h>
#include <linux/mdss_io_util.h>
#include <linux/irqreturn.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio.h>

int tianma_hvga_cmd_pre_mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int enable);
int tianma_hvga_cmd_post_mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int enable);
int tianma_hvga_cmd_mdss_panel_parse_dts(struct device_node *np,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int tianma_hvga_cmd_panel_device_create(struct device_node *node,
	struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int tianma_hvga_cmd_dsi_panel_device_register(struct device_node *pan_node,
				struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int tianma_hvga_cmd_mdss_dsi_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int tianma_hvga_cmd_mdss_dsi_panel_reset(struct mdss_panel_data *pdata, int enable);
int tianma_hvga_cmd_panel_device_create(struct device_node *node,
	struct mdss_dsi_ctrl_pdata *ctrl_pdata);

#if defined(CONFIG_LGD_SSD2068_FWVGA_VIDEO_PANEL)
int lgd_fwvga_video_pre_mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int enable);
int lgd_fwvga_video_post_mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int enable);
int lgd_fwvga_video_dsi_panel_device_register(struct device_node *pan_node,
				struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int lgd_fwvga_video_mdss_panel_parse_dts(struct device_node *np,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int lgd_fwvga_video_panel_device_create(struct device_node *node,
	struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int lgd_fwvga_video_mdss_dsi_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int lgd_fwvga_video_mdss_dsi_panel_reset(struct mdss_panel_data *pdata, int enable);
int lgd_fwvga_video_msm_mdss_enable_vreg(struct mdss_dsi_ctrl_pdata *ctrl_pdata, int enable);
#elif defined(CONFIG_LGD_DB7400_HD_VIDEO_PANEL)
int lgd_db7400_hd_video_pre_mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int enable);
int lgd_db7400_hd_video_post_mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int enable);
int lgd_db7400_hd_video_dsi_panel_device_register(struct device_node *pan_node,
				struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int lgd_db7400_hd_video_mdss_panel_parse_dts(struct device_node *np,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int lgd_db7400_hd_video_panel_device_create(struct device_node *node,
	struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int lgd_db7400_hd_video_mdss_dsi_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int lgd_db7400_hd_video_mdss_dsi_panel_reset(struct mdss_panel_data *pdata, int enable);
int lgd_db7400_hd_video_msm_mdss_enable_vreg(struct mdss_dsi_ctrl_pdata *ctrl_pdata, int enable);
#elif defined(CONFIG_TCL_ILI9806E_FWVGA_VIDEO_PANEL)
int tcl_ili9806e_fwvga_video_pre_mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int enable);
int tcl_ili9806e_fwvga_video_post_mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int enable);
int tcl_ili9806e_fwvga_video_dsi_panel_device_register(struct device_node *pan_node, struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int tcl_ili9806e_fwvga_video_mdss_panel_parse_dts(struct device_node *np, struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int tcl_ili9806e_fwvga_video_panel_device_create(struct device_node *node, struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int tcl_ili9806e_fwvga_video_mdss_dsi_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int tcl_ili9806e_fwvga_video_mdss_dsi_panel_reset(struct mdss_panel_data *pdata, int enable);
#endif

extern void smbchg_fb_notify_update_cb(bool is_on);

enum {
TIANMA_ILI9488_HVGA_CMD_PANEL,
TIANMA_ILI9488_HVGA_VIDEO_PANEL,
#if defined(CONFIG_LGD_SSD2068_FWVGA_VIDEO_PANEL)
LGD_SSD2068_FWVGA_VIDEO_PANEL,
#elif defined(CONFIG_LGD_DB7400_HD_VIDEO_PANEL)
LGD_DB7400_HD_VIDEO_PANEL,
#elif defined(CONFIG_TCL_ILI9806E_FWVGA_VIDEO_PANEL)
TCL_ILI9806E_FWVGA_VIDEO_PANEL,
#endif
UNKNOWN_PANEL
};

#endif /* OEM_MDSS_DSI_H */
