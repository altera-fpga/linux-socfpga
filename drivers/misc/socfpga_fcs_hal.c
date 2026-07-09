// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright (C) 2025 Altera Corporation
 */

#include <misc/socfpga_fcs_hal.h>
#include "socfpga_fcs_plat.h"

#define RANDOM_NUMBER_EXT_HDR_SIZE		12
#define FCS_CRYPTO_KEY_HEADER_SIZE		12
#define CERTIFICATE_RSP_MAX_SZ			4096
#define MBOX_SEND_RSP_MAX_SZ			4096
#define QSPI_READ_LEN_MAX			4096
#define MCTP_MAX_LEN				4096
#define DEVICE_IDENTITY_MAX_LEN			4096
#define QSPI_GET_INFO_LEN			36

#define HKDF_REQ_SZ_MAX				4096

#define DIGEST_CMD_MAX_SZ			SZ_4M
#define CRYPTO_DIGEST_MAX_SZ			SZ_4M
#define MAC_CMD_MAX_SZ				SZ_4M
#define FCS_ECC_PUBKEY_LEN			SZ_4M
#define FCS_ECDSA_HASH_SIGN_MAX_LEN		SZ_4M
#define FCS_ECDSA_HSHA2_DATA_SIGN_BLOCK_SZ	SZ_4M
#define FCS_ECDSA_SHA2_DATA_VERIFY_BLOCK_SZ	SZ_4M
#define FCS_ECDSA_DATA_SIGN_VERIFY_MAX_LEN	SZ_4M

#define FCS_ECDSA_SHA2_DATA_VERIFY_RSP_SZ	SZ_4M

#define WORDS_TO_BYTES_SIZE			4 /* 4 bytes in a word */

#define CRYPTO_SERVICE_MIN_DATA_SIZE		8

#define OWNER_ID_OFFSET				12
#define OWNER_ID_SIZE				8

#define RESPONSE_HEADER_SIZE			12

/*SDM required minimum 8 bytes of data for crypto service*/
#define DIGEST_SERVICE_MIN_DATA_SIZE	8
#define FCS_AES_IV_SZ			16
#define FCS_POLL_STATUS_LEN		4
#define FCS_AES_REQUEST_TIMEOUT		(10 * FCS_REQUEST_TIMEOUT)
#define FCS_CRYPTO_BLOCK_SZ		(4 * 1024 * 1024)
#define FCS_AES_CRYPT_BLOCK_SZ		FCS_CRYPTO_BLOCK_SZ
#define AES_PARAMS_CRYPT_OFFSET		1
#define AES_PARAMS_TAG_LEN_OFFSET	2
#define AES_PARAMS_IV_TYPE_OFFSET	4
#define AES_PARAMS_AAD_LEN_OFFSET	8
#define FCS_AES_PARAMS_ECB_SZ		12
#define GCM_TAG_LEN			16
#define GCM_AAD_ALIGN			16
#define GCM_DATA_ALIGN			16
#define NON_GCM_DATA_ALIGN		32
#define FCS_STATUS_LEN			4
#define FCS_ECDSA_CRYPTO_BLOCK_SZ	FCS_CRYPTO_BLOCK_SZ

/* AES GCM tag length */
#define GCS_TAG_LEN_32			4
#define GCS_TAG_LEN_64			8
#define GCS_TAG_LEN_96			12
#define GCS_TAG_LEN_128			16
#define MAP_GCM_TAG_32_TO_SDM_TAG	0
#define MAP_GCM_TAG_64_TO_SDM_TAG	1
#define MAP_GCM_TAG_96_TO_SDM_TAG	2
#define MAP_GCM_TAG_128_TO_SDM_TAG	3

/* HKDF input payload size with 1st and 2nd input */
#define HKDF_INPUT_DATA_SIZE		80

#define FCS_MAX_RESP_MS			50
#define SDOS_DECRYPTION_REPROVISION_KEY_WARN		0x102
#define SDOS_DECRYPTION_NOT_LATEST_KEY_WARN		0x103

static struct socfpga_fcs_priv *priv;

FCS_HAL_INT hal_session_close(struct fcs_cmd_context *const k_ctx)
{
	FCS_HAL_INT ret = 0;
	struct fcs_cmd_context ctx;

	fcs_plat_memcpy(&ctx, k_ctx, sizeof(struct fcs_cmd_context));

	ret = fcs_plat_uuid_compare(&priv->uuid_id,
				    &k_ctx->close_session.suuid);
	if (!ret) {
		ret = -EINVAL;
		LOG_ERR("Session UUID Mismatch ret: %d\n", ret);
		return ret;
	}

	ret = priv->plat_data->svc_send_request(
		priv, FCS_DEV_CRYPTO_CLOSE_SESSION, FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_CRYPTO_CLOSE_SESSION, ret);
		return ret;
	}

	fcs_plat_uuid_clear(priv);

	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to close session ret: %d\n",
			ret);
	}

	ret = fcs_plat_copy_to_user(ctx.error_code_addr, &priv->status,
				    sizeof(priv->status));
	if (ret) {
		LOG_ERR("Failed to copy mail box status code to user ret: %d\n",
			ret);
	}


	return ret;
}
EXPORT_SYMBOL(hal_session_close);

FCS_HAL_INT hal_session_open(struct fcs_cmd_context *const k_ctx)
{
	FCS_HAL_INT ret = 0;

	ret = priv->plat_data->svc_send_request(
		priv, FCS_DEV_CRYPTO_OPEN_SESSION, FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_CRYPTO_OPEN_SESSION, ret);
		return ret;
	}

	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to open session ret: %d\n", ret);
		goto copy_mbox_status;
	}

	fcs_plat_uuid_generate(priv);

	memcpy(&priv->session_id, &priv->resp, sizeof(priv->session_id));

	ret = fcs_plat_copy_to_user(k_ctx->open_session.suuid, &priv->uuid_id,
				    sizeof(FCS_HAL_UUID));
	if (ret) {
		LOG_ERR("Failed to copy session ID to user suuid addr: %p ret: %d\n",
			k_ctx->open_session.suuid, ret);
		goto copy_mbox_status;
	}

copy_mbox_status:
	ret = fcs_plat_copy_to_user(k_ctx->error_code_addr, &priv->status,
				    sizeof(priv->status));
	if (ret) {
		LOG_ERR("Failed to copy mail box status code to user ret: %d\n",
			ret);
	}

	return ret;
}
EXPORT_SYMBOL(hal_session_open);

FCS_HAL_VOID hal_get_atf_version(FCS_HAL_U32 *version)
{
	fcs_plat_memcpy(version, priv->atf_version, sizeof(priv->atf_version));
}
EXPORT_SYMBOL(hal_get_atf_version);

struct fcs_cmd_context *hal_get_fcs_cmd_ctx(void)
{
	fcs_plat_mutex_lock(priv);
	return &priv->k_ctx;
}
EXPORT_SYMBOL(hal_get_fcs_cmd_ctx);

FCS_HAL_VOID hal_destroy_fcs_cmd_ctx(struct fcs_cmd_context *const k_ctx)
{
	fcs_plat_memset(k_ctx, 0, sizeof(struct fcs_cmd_context));
}
EXPORT_SYMBOL(hal_destroy_fcs_cmd_ctx);

FCS_HAL_VOID hal_release_fcs_cmd_ctx(struct fcs_cmd_context *const k_ctx)
{
	fcs_plat_mutex_unlock(priv);
}
EXPORT_SYMBOL(hal_release_fcs_cmd_ctx);

static FCS_HAL_INT hal_read_version_from_atf(FCS_HAL_VOID)
{
	FCS_HAL_INT ret = 0;

	ret = priv->plat_data->svc_send_request(priv, FCS_DEV_ATF_VERSION,
						FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_ATF_VERSION, ret);
		return ret;
	}

	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to read ATF version ret: %d\n",
			ret);
	}

	priv->plat_data->svc_task_done(priv);

	return ret;
}

FCS_HAL_INT hal_fcs_init(FCS_HAL_DEV *dev)
{
	FCS_HAL_INT ret;

	priv = devm_kzalloc(dev, sizeof(struct socfpga_fcs_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = fcs_plat_init(dev, priv);
	if (ret) {
		LOG_ERR("Failed to initialize platform data ret: %d\n", ret);
		return ret;
	}

	hal_read_version_from_atf();

	return ret;
}

FCS_HAL_VOID hal_fcs_deinit(void)
{
	if (priv && priv->session_id) {
		FCS_HAL_INT ret = priv->plat_data->svc_send_request(
			priv, FCS_DEV_CRYPTO_CLOSE_SESSION,
			FCS_REQUEST_TIMEOUT);
		if (ret) {
			LOG_ERR("Failed to close FCS service session,ret=%d\n",
				ret);
		}
	}

	if (priv)
		fcs_plat_deinit(priv);
}

FCS_HAL_INT hal_get_platform(FCS_HAL_VOID)
{
	return priv->platform;
}

FCS_HAL_VOID hal_fcs_cleanup(void)
{
	fcs_plat_cleanup(priv);
	priv = NULL;
}
