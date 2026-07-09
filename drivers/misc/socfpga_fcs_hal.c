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

FCS_HAL_INT hal_import_key(struct fcs_cmd_context *const k_ctx)
{
	FCS_HAL_INT ret = 0;
	FCS_HAL_UINT sbuf_size;
	FCS_HAL_VOID *s_buf = NULL;
	struct fcs_cmd_context ctx;

	fcs_plat_memcpy(&ctx, k_ctx, sizeof(struct fcs_cmd_context));

	ret = fcs_plat_uuid_compare(&priv->uuid_id, &k_ctx->import_key.suuid);
	if (!ret) {
		ret = -EINVAL;
		LOG_ERR("Session UUID Mismatch ret: %d\n", ret);
		return ret;
	}

	sbuf_size = k_ctx->import_key.key_len + FCS_CRYPTO_KEY_HEADER_SIZE;

	s_buf = priv->plat_data->svc_alloc_memory(priv, sbuf_size);
	if (IS_ERR(s_buf)) {
		ret = -ENOMEM;
		LOG_ERR("Failed to allocate memory for source buffer ret: %d\n",
			ret);
		return ret;
	}

	/* Copy the session ID into the source buffer */
	fcs_plat_memcpy(s_buf, &priv->session_id, sizeof(FCS_HAL_U32));

	/* Copy the key data from user space to the source buffer */
	ret = fcs_plat_copy_from_user(s_buf + FCS_CRYPTO_KEY_HEADER_SIZE,
				      k_ctx->import_key.key,
				      k_ctx->import_key.key_len);
	if (ret) {
		LOG_ERR("Failed to copy data from user to kernel source buffer ret: %d\n",
			ret);
		goto free_mem;
	}

	k_ctx->import_key.key = s_buf;
	k_ctx->import_key.key_len = sbuf_size;

	ret = priv->plat_data->svc_send_request(priv, FCS_DEV_CRYPTO_IMPORT_KEY,
						FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_CRYPTO_IMPORT_KEY, ret);
		ret = -EFAULT;
		goto free_mem;
	}

	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to import key ret: %d\n", ret);
		goto copy_mbox_status;
	}

	ret = fcs_plat_copy_to_user(ctx.import_key.status, &priv->resp, 1);
	if (ret) {
		LOG_ERR("Failed to copy import key response data to user ret: %d\n",
			ret);
		goto copy_mbox_status;
	}

copy_mbox_status:
	ret = fcs_plat_copy_to_user(ctx.error_code_addr, &priv->status,
				    sizeof(priv->status));
	if (ret) {
		LOG_ERR("Failed to copy mbox error code to user ret: %d\n",
			ret);
		ret = -EFAULT;
	}
free_mem:
	priv->plat_data->svc_free_memory(priv, s_buf);

	return ret;
}
EXPORT_SYMBOL(hal_import_key);

FCS_HAL_INT hal_export_key(struct fcs_cmd_context *const k_ctx)
{
	FCS_HAL_INT ret = 0;
	FCS_HAL_VOID *d_buf = NULL;
	FCS_HAL_UINT key_len = CRYPTO_EXPORTED_KEY_OBJECT_MAX_SZ;
	struct fcs_cmd_context ctx;

	fcs_plat_memcpy(&ctx, k_ctx, sizeof(struct fcs_cmd_context));

	ret = fcs_plat_uuid_compare(&priv->uuid_id, &k_ctx->export_key.suuid);
	if (!ret) {
		ret = -EINVAL;
		LOG_ERR("Session UUID Mismatch ret: %d\n", ret);
		return ret;
	}

	d_buf = priv->plat_data->svc_alloc_memory(priv,
						  key_len + FCS_STATUS_LEN);
	if (IS_ERR(d_buf)) {
		ret = -ENOMEM;
		LOG_ERR("Failed to allocate memory for key object ret: %d\n",
			ret);
		return ret;
	}

	k_ctx->export_key.key = d_buf;
	k_ctx->export_key.key_len = &key_len;

	ret = priv->plat_data->svc_send_request(priv, FCS_DEV_CRYPTO_EXPORT_KEY,
						FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_CRYPTO_EXPORT_KEY, ret);
		goto free_dest;
	}

	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to export key ret: %d\n", ret);
		goto copy_mbox_status;
	}

	ret = fcs_plat_copy_to_user(ctx.export_key.key, d_buf + FCS_STATUS_LEN,
				    priv->resp - FCS_STATUS_LEN);
	if (ret) {
		LOG_ERR("Failed to copy key to user ret: %d\n", ret);
		goto copy_mbox_status;
	}

	ret = fcs_plat_copy_to_user(ctx.export_key.key_len, &priv->resp,
				    sizeof(priv->resp));
	if (ret) {
		LOG_ERR("Failed to copy key length to user ret: %d\n", ret);
		goto copy_mbox_status;
	}

copy_mbox_status:
	ret = fcs_plat_copy_to_user(ctx.error_code_addr, &priv->status,
				    sizeof(priv->status));
	if (ret) {
		LOG_ERR("Failed to copy mail box status code to user ret: %d\n",
			ret);
	}
free_dest:
	priv->plat_data->svc_free_memory(priv, d_buf);

	return ret;
}
EXPORT_SYMBOL(hal_export_key);

FCS_HAL_INT hal_remove_key(struct fcs_cmd_context *const k_ctx)
{
	FCS_HAL_INT ret = 0;
	struct fcs_cmd_context ctx;

	fcs_plat_memcpy(&ctx, k_ctx, sizeof(struct fcs_cmd_context));

	ret = fcs_plat_uuid_compare(&priv->uuid_id, &k_ctx->remove_key.suuid);
	if (!ret) {
		ret = -EINVAL;
		LOG_ERR("Session UUID Mismatch ret: %d\n", ret);
		return ret;
	}

	ret = priv->plat_data->svc_send_request(priv, FCS_DEV_CRYPTO_REMOVE_KEY,
						FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_CRYPTO_REMOVE_KEY, ret);
		return ret;
	}

	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to remove key ret: %d\n", ret);
	}

	ret = fcs_plat_copy_to_user(ctx.error_code_addr, &priv->status,
				    sizeof(priv->status));
	if (ret) {
		LOG_ERR("Failed to copy mail box status code to user ret: %d\n",
			ret);
	}

	return ret;
}
EXPORT_SYMBOL(hal_remove_key);

FCS_HAL_INT hal_get_key_info(struct fcs_cmd_context *const k_ctx)
{
	FCS_HAL_INT ret = 0;
	FCS_HAL_UINT info_len = CRYPTO_KEY_INFO_MAX_SZ;
	FCS_HAL_VOID *d_buf = NULL;
	struct fcs_cmd_context ctx;

	fcs_plat_memcpy(&ctx, k_ctx, sizeof(struct fcs_cmd_context));

	ret = fcs_plat_uuid_compare(&priv->uuid_id, &k_ctx->key_info.suuid);
	if (!ret) {
		ret = -EINVAL;
		LOG_ERR("session UUID Mismatch ret: %d\n", ret);
		return ret;
	}

	/* Allocate memory for the key info kernel buffer */
	d_buf = priv->plat_data->svc_alloc_memory(priv, CRYPTO_KEY_INFO_MAX_SZ);
	if (IS_ERR(d_buf)) {
		ret = -ENOMEM;
		LOG_ERR("Failed to allocate memory for key info kernel buffer ret: %d\n",
			ret);
		return ret;
	}

	k_ctx->key_info.info = d_buf;
	k_ctx->key_info.info_len = &info_len;

	/* Send the request to get key info */
	ret = priv->plat_data->svc_send_request(
		priv, FCS_DEV_CRYPTO_GET_KEY_INFO, FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_CRYPTO_GET_KEY_INFO, ret);
		goto free_dest;
	}

	/* Check if there was a mailbox error during key info retrieval */
	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to get key info ret: %d\n", ret);
		goto copy_mbox_status;
	}

	/* Copy the key info from kernel space to user space */
	ret = fcs_plat_copy_to_user(ctx.key_info.info, d_buf, priv->resp);
	if (ret) {
		LOG_ERR("Failed to copy key info to user ret: %d\n", ret);
		goto copy_mbox_status;
	}

	/* Copy the key info length from kernel space to user space */
	ret = fcs_plat_copy_to_user(ctx.key_info.info_len, &priv->resp,
				    sizeof(priv->resp));
	if (ret) {
		LOG_ERR("Failed to copy key info length to user ret: %d\n",
			ret);
		goto copy_mbox_status;
	}

copy_mbox_status:
	ret = fcs_plat_copy_to_user(ctx.error_code_addr, &priv->status,
				    sizeof(priv->status));
	if (ret) {
		LOG_ERR("Failed to copy mailbox status code to user ret: %d\n",
			ret);
	}
free_dest:
	priv->plat_data->svc_free_memory(priv, d_buf);

	return ret;
}
EXPORT_SYMBOL(hal_get_key_info);

FCS_HAL_INT hal_create_key(struct fcs_cmd_context *const k_ctx)
{
	FCS_HAL_INT ret = 0;
	FCS_HAL_VOID *s_buf = NULL;
	struct fcs_cmd_context ctx;

	fcs_plat_memcpy(&ctx, k_ctx, sizeof(struct fcs_cmd_context));

	/* Compare the session UUIDs to check for a match */
	ret = fcs_plat_uuid_compare(&priv->uuid_id, &k_ctx->create_key.suuid);
	if (!ret) {
		ret = -EINVAL;
		LOG_ERR("session UUID Mismatch ret: %d\n", ret);
		return ret;
	}

	/* Calculate the total key length including the header */
	k_ctx->create_key.key_len =
		FCS_CRYPTO_KEY_HEADER_SIZE + k_ctx->create_key.key_len;

	/* Allocate memory for the key object */
	s_buf = priv->plat_data->svc_alloc_memory(priv,
						  k_ctx->create_key.key_len);
	if (IS_ERR(s_buf)) {
		ret = -ENOMEM;
		LOG_ERR("Failed to allocate memory for key object ret: %d\n",
			ret);
		return ret;
	}

	/* Copy the session ID into the source buffer */
	fcs_plat_memcpy(s_buf, &priv->session_id, sizeof(FCS_HAL_U32));

	/* Copy the key object data from user space to the source buffer */
	ret = fcs_plat_copy_from_user(s_buf + FCS_CRYPTO_KEY_HEADER_SIZE,
				      k_ctx->create_key.key,
				      k_ctx->create_key.key_len);
	if (ret) {
		LOG_ERR("Failed to copy data from user to kernel source buffer ret: %d\n",
			ret);
		goto free_mem;
	}

	k_ctx->create_key.key = s_buf;

	/* Send the request to create key */
	ret = priv->plat_data->svc_send_request(priv, FCS_DEV_CRYPTO_CREATE_KEY,
						FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_CRYPTO_CREATE_KEY, ret);
		goto free_mem;
	}

	/* Check if there was a mailbox error during key creation */
	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to create key ret: %d\n", ret);
		goto copy_mbox_status;
	}

	ret = fcs_plat_copy_to_user(ctx.create_key.status, &priv->resp, 1);
	if (ret) {
		LOG_ERR("Failed to copy create key status to user ret: %d\n",
			ret);
		goto copy_mbox_status;
	}

copy_mbox_status:
	ret = fcs_plat_copy_to_user(ctx.error_code_addr, &priv->status,
				    sizeof(priv->status));
	if (ret) {
		LOG_ERR("Failed to copy mailbox status code to user ret: %d\n",
			ret);
	}
free_mem:
	priv->plat_data->svc_free_memory(priv, s_buf);

	return ret;
}
EXPORT_SYMBOL(hal_create_key);

FCS_HAL_INT hal_hkdf_request(struct fcs_cmd_context *const k_ctx)
{
	FCS_HAL_INT ret = 0;
	FCS_HAL_VOID *s_buf = NULL, *src_ptr = NULL;
	struct fcs_cmd_context ctx;

	fcs_plat_memcpy(&ctx, k_ctx, sizeof(struct fcs_cmd_context));

	/* Compare the session UUIDs to check for a match. Here suuid is set
	 * through hal_store_context
	 */
	ret = fcs_plat_uuid_compare(&priv->uuid_id, &k_ctx->hkdf_req.suuid);
	if (!ret) {
		ret = -EINVAL;
		LOG_ERR("session UUID Mismatch ret: %d\n", ret);
		return ret;
	}

	/* Allocate memory for the shared secret kernel buffer */
	s_buf = priv->plat_data->svc_alloc_memory(priv, FCS_KDK_MAX_SZ);
	if (IS_ERR(s_buf)) {
		ret = -ENOMEM;
		LOG_ERR("Failed to allocate memory for HKDF kernel buffer ret: %d\n",
			ret);
		return ret;
	}

	src_ptr = s_buf;

	fcs_plat_memset(src_ptr, 0, HKDF_REQ_SZ_MAX);

	ret = fcs_plat_copy_from_user(src_ptr, &ctx.hkdf_req.ikm_len,
				      sizeof(ctx.hkdf_req.ikm_len));
	if (ret) {
		LOG_ERR("Failed to copy HKDF salt length from user to kernel buffer ret: %d\n",
			ret);
		goto free_mem;
	}
	src_ptr += sizeof(ctx.hkdf_req.ikm_len);

	ret = fcs_plat_copy_from_user(src_ptr, ctx.hkdf_req.ikm,
				      ctx.hkdf_req.ikm_len);
	if (ret) {
		LOG_ERR("Failed to copy HKDF info from user to kernel buffer ret: %d\n",
			ret);
		goto free_mem;
	}
	src_ptr += HKDF_INPUT_DATA_SIZE;

	ret = fcs_plat_copy_from_user(src_ptr, &ctx.hkdf_req.info_len,
				      sizeof(ctx.hkdf_req.info_len));
	if (ret) {
		LOG_ERR("Failed to copy HKDF salt from user to kernel buffer ret: %d\n",
			ret);
		goto free_mem;
	}
	src_ptr += sizeof(ctx.hkdf_req.info_len);

	ret = fcs_plat_copy_from_user(src_ptr, ctx.hkdf_req.info,
					ctx.hkdf_req.info_len);
	if (ret) {
		LOG_ERR("Failed to copy HKDF salt from user to kernel buffer ret: %d\n",
			ret);
		goto free_mem;
	}
	src_ptr += HKDF_INPUT_DATA_SIZE;

	ret = fcs_plat_copy_from_user(src_ptr,
				      ctx.hkdf_req.output_key_obj,
				      ctx.hkdf_req.output_key_obj_len);
	if (ret) {
		LOG_ERR("Failed to copy HKDF output key obj from user ret: %d\n",
			ret);
		goto free_mem;
	}

	src_ptr += ctx.hkdf_req.output_key_obj_len;

	k_ctx->hkdf_req.ikm_len = src_ptr - s_buf;

	k_ctx->hkdf_req.ikm = s_buf;

	ret = priv->plat_data->svc_send_request(priv,
						FCS_DEV_CRYPTO_HKDF_REQUEST,
						FCS_REQUEST_TIMEOUT);
	if (ret) {
		LOG_ERR("Failed to send the cmd=%d,ret=%d\n",
			FCS_DEV_CRYPTO_HKDF_REQUEST, ret);
		goto free_mem;
	}

	if (priv->status) {
		ret = -EIO;
		LOG_ERR("Mailbox error, Failed to perform HKDF ret: %d\n", ret);
	}

	ret = fcs_plat_copy_to_user(ctx.error_code_addr, &priv->status,
				    sizeof(priv->status));
	if (ret) {
		LOG_ERR("Failed to copy mailbox status code to user ret: %d\n",
			ret);
	}

	ret = fcs_plat_copy_to_user(ctx.hkdf_req.hkdf_resp, &priv->resp,
				    sizeof(priv->resp));
	if (ret)
		LOG_ERR("Failed to copy HKDF status to user ret: %d\n", ret);

free_mem:
	priv->plat_data->svc_free_memory(priv, s_buf);

	return ret;
}
EXPORT_SYMBOL(hal_hkdf_request);

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
