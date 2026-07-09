// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2024 Intel Corporation
 */

#ifndef __SOCFPA_HAL_LL_H
#define __SOCFPA_HAL_LL_H

#include <misc/socfpga_fcs_hal.h>
#include "socfpga_fcs_plat.h"

#include <linux/firmware/intel/stratix10-svc-client.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#define INVALID_STATUS		0xFFFFFFFF
#define INVALID_ID		0xFFFFFFFF

#define DIGEST_PARAM_SZ		4
#define DIGEST_SZ_OFFSET	4
#define CRYPTO_ECC_PARAM_SZ	4

#define MSG_RETRY		3
#define RETRY_SLEEP_MS		1
#define TIMEOUT			1000

#define FCS_SVC_CLIENT_NAME "socfpga-fcs"

FCS_HAL_VOID fcs_plat_reinit_completion(FCS_HAL_COMPLETION  *completion)
{
	reinit_completion(completion);
}

static void fcs_atf_version_callback(struct stratix10_svc_client *client,
				     struct stratix10_svc_cb_data *data)
{
	struct socfpga_fcs_priv *priv = client->priv;

	priv->status = data->status;
	if (data->status == BIT(SVC_STATUS_OK)) {
		priv->status = 0;
		priv->atf_version[0] = *((FCS_HAL_UINT *)data->kaddr1);
		priv->atf_version[1] = *((FCS_HAL_UINT *)data->kaddr2);
		priv->atf_version[2] = *((FCS_HAL_UINT *)data->kaddr3);
	} else if (data->status == BIT(SVC_STATUS_ERROR)) {
		priv->status = *((FCS_HAL_UINT *)data->kaddr1);
		dev_err(client->dev, "mbox_error=0x%x\n", priv->status);
	}

	complete(&priv->completion);
}

FCS_HAL_INT fcs_plat_copy_to_user(FCS_HAL_VOID *dst, FCS_HAL_VOID *src, FCS_HAL_SIZE size)
{
	if (access_ok(dst, size)) {
		if (copy_to_user(dst, src, size)) {
			pr_err("Failed to copy data to user-space\n");
			return -EFAULT;
		}
	} else {
		fcs_plat_memcpy(dst, src, size);
	}

	return 0;
}

FCS_HAL_INT fcs_plat_copy_from_user(FCS_HAL_VOID *dst, FCS_HAL_VOID *src, FCS_HAL_SIZE size)
{
	if (access_ok(src, size)) {
		if (copy_from_user(dst, src, size)) {
			pr_err("Failed to copy data from user-space\n");
			return -EFAULT;
		}
	} else {
		fcs_plat_memcpy(dst, src, size);
	}

	return 0;
}

FCS_HAL_VOID fcs_plat_memset(FCS_HAL_VOID *dst, FCS_HAL_U8 val, FCS_HAL_SIZE size)
{
	memset(dst, val, size);
}

FCS_HAL_VOID fcs_plat_memcpy(FCS_HAL_VOID *dst, FCS_HAL_VOID *src, FCS_HAL_SIZE size)
{
	memcpy(dst, src, size);
}

FCS_HAL_INT fcs_plat_wait_for_completion(FCS_HAL_COMPLETION *completion, FCS_HAL_ULONG timeout)
{
	return wait_for_completion_timeout(completion, timeout);
}

FCS_HAL_VOID fcs_plat_mutex_lock(struct socfpga_fcs_priv *priv)
{
	mutex_lock(&priv->lock);
}

FCS_HAL_VOID fcs_plat_mutex_unlock(struct socfpga_fcs_priv *priv)
{
	mutex_unlock(&priv->lock);
}

FCS_HAL_VOID *fcs_plat_alloc_mem(FCS_HAL_SIZE size)
{
	return kmalloc(size, GFP_KERNEL);
}

FCS_HAL_VOID fcs_plat_free_mem(FCS_HAL_VOID *ptr)
{
	kfree(ptr);
}

FCS_HAL_BOOL fcs_plat_uuid_compare(FCS_HAL_UUID *uuid1, FCS_HAL_UUID *uuid2)
{
	return uuid_equal(uuid1, uuid2);
}

FCS_HAL_VOID fcs_plat_uuid_copy(FCS_HAL_UUID *dst, FCS_HAL_UUID *src)
{
	uuid_copy(dst, src);
}

FCS_HAL_VOID fcs_plat_uuid_generate(struct socfpga_fcs_priv *priv)
{
	uuid_gen(&priv->uuid_id);
}

FCS_HAL_VOID fcs_plat_uuid_clear(struct socfpga_fcs_priv *priv)
{
	memset(&priv->uuid_id, 0, sizeof(FCS_HAL_UUID));
	memset(&priv->session_id, 0, sizeof(FCS_HAL_U32));
}

FCS_HAL_VOID fcs_plat_free_svc_memory(struct socfpga_fcs_priv *priv,
				      void *buf1, void *buf2, void *buf3)
{
	if (buf1)
		stratix10_svc_free_memory(priv->chan, buf1);

	if (buf2)
		stratix10_svc_free_memory(priv->chan, buf2);

	if (buf3)
		stratix10_svc_free_memory(priv->chan, buf3);
}

static FCS_HAL_VOID *plat_sip_svc_allocate_memory(struct socfpga_fcs_priv *priv,
						  size_t size)
{
	return stratix10_svc_allocate_memory(priv->chan, size);
}

static FCS_HAL_VOID plat_sip_svc_free_memory(struct socfpga_fcs_priv *priv,
					     void *buf)
{
	stratix10_svc_free_memory(priv->chan, buf);
}

static FCS_HAL_VOID plat_sip_svc_task_done(struct socfpga_fcs_priv *priv)
{
	stratix10_svc_done(priv->chan);
}

static void soc64_async_callback(void *ptr)
{
	if (ptr)
		complete(ptr);
}

static FCS_HAL_INT plat_sip_svc_send_request(struct socfpga_fcs_priv *priv,
					     enum fcs_command_code command,
					     FCS_HAL_ULONG timeout)
{
	FCS_HAL_BOOL no_async_poll = false;
	FCS_HAL_INT ret = 0;
	int status, index;
	void *handle = NULL;
	struct stratix10_svc_cb_data data;
	struct completion completion;
	struct fcs_cmd_context *k_ctx = &priv->k_ctx;
	FCS_SVC_CLIENT_MSG *msg =
		kzalloc(sizeof(FCS_SVC_CLIENT_MSG), GFP_KERNEL);

	if (!msg) {
		pr_err("failed to allocate memory for svc client message ret: %d\n",
		       ret);
		return -ENOMEM;
	}

	priv->status = 0;
	priv->resp = 0;

	switch (command) {
	case FCS_DEV_CRYPTO_OPEN_SESSION:
		pr_debug("Sending command: COMMAND_FCS_CRYPTO_OPEN_SESSION\n");
		msg->command = COMMAND_FCS_CRYPTO_OPEN_SESSION;
		break;

	case FCS_DEV_CRYPTO_CLOSE_SESSION:
		pr_debug("Sending command: COMMAND_FCS_CRYPTO_CLOSE_SESSION with session_id: 0x%x\n",
			 priv->session_id);
		msg->arg[0] = priv->session_id;
		msg->command = COMMAND_FCS_CRYPTO_CLOSE_SESSION;
		break;

	case FCS_DEV_ATF_VERSION:
		pr_debug("Sending command: COMMAND_SMC_ATF_BUILD_VER\n");
		msg->command = COMMAND_SMC_ATF_BUILD_VER;
		priv->client.receive_cb = fcs_atf_version_callback;
		break;

	default:
		pr_err("Unknown command: 0x%x\n", command);
		ret = -EINVAL;
		break;
	}

	if (command == FCS_DEV_ATF_VERSION) {
		reinit_completion(&priv->completion);

		ret = stratix10_svc_send(priv->chan, msg);
		if (ret) {
			pr_err("failed to send message to service channel\n");
			goto fun_ret;
		}

		if (!wait_for_completion_timeout(&priv->completion, timeout)) {
			pr_err("svc timeout to get completed status\n");
			ret = -ETIMEDOUT;
		}
fun_ret:
		kfree(msg);
		return ret;
	}

	init_completion(&completion);

	for (index = 0; index < MSG_RETRY; index++) {
		status = stratix10_svc_async_send(priv->chan, msg, &handle,
						  soc64_async_callback,
						  &completion);
		if (status == 0)
			break;
		msleep(RETRY_SLEEP_MS);
	}

	if (!handle || status != 0) {
		pr_err("Failed to send async message\n");
		return -ETIMEDOUT;
	}

	if (!no_async_poll) {
		ret = wait_for_completion_io_timeout(&completion, (TIMEOUT));
		if (ret > 0)
			pr_debug("Received async interrupt\n");
		else
			pr_err("timeout occurred while waiting for async message\n");

		ret = stratix10_svc_async_poll(priv->chan, handle, &data);
		if (ret) {
			pr_err("Failed to poll async message\n");
			goto out;
		}

		priv->status = data.status;

		if (data.kaddr1) {
			if (command == FCS_DEV_CHIP_ID) {
				priv->chip_id_lo =
					*((FCS_HAL_UINT *)data.kaddr1);
				priv->chip_id_hi =
					*((FCS_HAL_UINT *)data.kaddr2);
			} else {
				priv->resp = *((FCS_HAL_U32 *)data.kaddr1);
			}
		} else {
			priv->resp = 0;
		}
	}

out:
	stratix10_svc_async_done(priv->chan, handle);
	kfree(msg);

	return ret;
}

FCS_HAL_INT fcs_platform_get(FCS_HAL_DEV *dev, struct socfpga_fcs_priv *priv)
{
	FCS_HAL_INT ret = 0;

	if (of_device_is_compatible(dev->of_node, "intel,agilex5-soc-fcs-config"))
		priv->platform = AGILEX5_PLAT;
	else if (of_device_is_compatible(dev->of_node, "intel,agilex-soc-fcs-config"))
		priv->platform = AGILEX_PLAT;
	else if (of_device_is_compatible(dev->of_node, "intel,n5x-soc-fcs-config"))
		priv->platform = N5X_PLAT;
	else
		priv->platform = 0;

	if (!priv->platform) {
		pr_err("Unsupported platform\n");
		ret = -ENODEV;
	}
	return ret;
}

FCS_HAL_INT fcs_plat_init(FCS_HAL_DEV *dev, struct socfpga_fcs_priv *priv)
{
	mutex_init(&priv->lock);
	FCS_HAL_S32 ret = 0;

	priv->plat_data =
		kmalloc(sizeof(struct socfpga_fcs_service_ops), GFP_KERNEL);
	if (!priv->plat_data) {
		pr_err("Failed to allocate memory for priv->plat_data\n");
		return -ENOMEM;
	}

	priv->dev = dev;
	priv->client.dev = dev;
	priv->client.receive_cb = NULL;
	priv->client.priv = priv;

	priv->chan = stratix10_svc_request_channel_byname(&priv->client,
							  SVC_CLIENT_FCS);
	if (IS_ERR(priv->chan)) {
		pr_err("couldn't get service channel %s\n", SVC_CLIENT_FCS);
		return -EPROBE_DEFER;
	}

	ret = stratix10_svc_add_async_client(priv->chan, true);
	if (ret) {
		pr_err("Failed to add async client\n");
		return ret;
	}

	init_completion(&priv->completion);

	priv->plat_data->svc_send_request = plat_sip_svc_send_request;
	priv->plat_data->svc_alloc_memory = plat_sip_svc_allocate_memory;
	priv->plat_data->svc_free_memory = plat_sip_svc_free_memory;
	priv->plat_data->svc_task_done = plat_sip_svc_task_done;

	ret = fcs_platform_get(priv->dev, priv);
	if (ret) {
		pr_err("Failed to get platform data\n");
		return ret;
	}

	return 0;
}

FCS_HAL_VOID fcs_plat_deinit(struct socfpga_fcs_priv *priv)
{
	stratix10_svc_remove_async_client(priv->chan);
	stratix10_svc_free_channel(priv->chan);
}

FCS_HAL_VOID fcs_plat_cleanup(struct socfpga_fcs_priv *priv)
{
	stratix10_svc_free_channel(priv->chan);
}

#endif /* __SOCFPA_HAL_LL_H */
