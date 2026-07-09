// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025, Altera
 */

#include <linux/of_platform.h>
#include <linux/sysfs.h>
#include <misc/socfpga_fcs_hal.h>
#include <linux/platform_device.h>
#include <linux/of.h>

// Define the store function for the session_id attribute
static ssize_t open_session_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t buf_size)
{
	struct fcs_cmd_context *const u_ctx = *(struct fcs_cmd_context **)buf;
	struct fcs_cmd_context *k_ctx;
	int ret = buf_size;

	k_ctx = hal_get_fcs_cmd_ctx();
	if (!k_ctx) {
		pr_err("Failed get context. Context is in use\n");
		ret = -EFAULT;
		goto out;
	}

	ret = copy_from_user(k_ctx, u_ctx, sizeof(struct fcs_cmd_context));
	if (ret) {
		pr_err("Failed to copy context from user space ret: %d\n", ret);
		ret = -EFAULT;
		goto out;
	}

	ret = hal_session_open(k_ctx);
	if (ret)
		pr_err("Failed to open session\n");

out:
	hal_release_fcs_cmd_ctx(k_ctx);

	return ret;
}

// Define the store function for the session_id attribute
static ssize_t close_session_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t buf_size)
{
	struct fcs_cmd_context *const u_ctx = *(struct fcs_cmd_context **)buf;
	struct fcs_cmd_context *k_ctx;
	int ret = buf_size;

	k_ctx = hal_get_fcs_cmd_ctx();
	if (!k_ctx) {
		pr_err("Failed get context. Context is in use\n");
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(k_ctx, u_ctx, sizeof(struct fcs_cmd_context))) {
		pr_err("Failed to copy context from user space\n");
		ret = -EFAULT;
		goto out;
	}

	ret = hal_session_close(k_ctx);
	if (ret)
		pr_err("Failed to close session\n");

out:
	hal_release_fcs_cmd_ctx(k_ctx);

	return ret;
}

static ssize_t context_info_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t buf_size)
{
	struct fcs_cmd_context *const u_ctx = *(struct fcs_cmd_context **)buf;
	struct fcs_cmd_context *k_ctx;
	int ret = buf_size;

	k_ctx = hal_get_fcs_cmd_ctx();
	if (!k_ctx) {
		pr_err("Failed get context. Context is in use\n");
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(k_ctx, u_ctx, sizeof(struct fcs_cmd_context))) {
		pr_err("Failed to copy context from user space\n");
		ret = -EFAULT;
	}

out:
	hal_release_fcs_cmd_ctx(k_ctx);

	return ret;
}

static ssize_t atf_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int version[3];

	hal_get_atf_version(version);
	return sprintf(buf, "%u.%u.%u\n", version[0], version[1], version[2]);
}

static ssize_t platform_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", hal_get_platform());
}

static DEVICE_ATTR_WO(open_session);
static DEVICE_ATTR_WO(close_session);
static DEVICE_ATTR_WO(context_info);
static DEVICE_ATTR_RO(atf_version);
static DEVICE_ATTR_RO(platform);

static struct attribute *fcs_config_attrs[] = {
	&dev_attr_open_session.attr,
	&dev_attr_close_session.attr,
	&dev_attr_context_info.attr,
	&dev_attr_atf_version.attr,
	&dev_attr_platform.attr,
	NULL
};

static struct attribute_group fcs_group = {
	.attrs = fcs_config_attrs,
};

static const struct attribute_group *fcs_groups[] = {
	&fcs_group,
	NULL,
};

struct kobject *sysfs_kobj;

static int fcs_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	sysfs_kobj = kobject_create_and_add("fcs_sysfs", kernel_kobj);
	if (!sysfs_kobj) {
		pr_err("Failed to create and add kobject\n");
		return -ENOMEM;
	}

	ret = sysfs_create_groups(sysfs_kobj, fcs_groups);
	if (ret) {
		dev_err(dev, "Failed to create sysfs groups\n");
		kobject_put(sysfs_kobj);
		return ret;
	}

	ret = hal_fcs_init(dev);
	if (ret) {
		dev_err(dev, "Failed to initialize FCS HAL\n");
		sysfs_remove_groups(sysfs_kobj, fcs_groups);
		kobject_put(sysfs_kobj);
		return ret;
	}

	pr_info("FCS config probing successfully completed");

	return ret;
}

static const struct of_device_id fcs_of_match[] = {
	{ .compatible = "intel,agilex5-soc-fcs-config" },
	{ .compatible = "intel,agilex-soc-fcs-config" },
	{ .compatible = "intel,n5x-soc-fcs-config" },
	{},
};

static struct platform_driver fcs_driver = {
	.probe = fcs_driver_probe,
	.driver = {
		.name = "socfpga-config",
		.of_match_table = of_match_ptr(fcs_of_match),
	},
};

MODULE_DEVICE_TABLE(of, fcs_of_match);

static int __init fcs_config_init(void)
{
	struct device_node *fw_np;
	struct device_node *np;
	int ret;

	fw_np = of_find_node_by_name(NULL, "firmware");
	if (!fw_np)
		return -ENODEV;

	of_node_get(fw_np);
	np = of_find_matching_node(fw_np, fcs_of_match);
	if (!np) {
		of_node_put(fw_np);
		return -ENODEV;
	}

	of_node_put(np);
	ret = of_platform_populate(fw_np, fcs_of_match, NULL, NULL);
	of_node_put(fw_np);
	if (ret)
		return ret;

	ret = platform_driver_register(&fcs_driver);
	if (ret)
		pr_err("Failed to register platform driver\n");

	return ret;
}

static void __exit fcs_config_exit(void)
{
	hal_fcs_deinit();
	/* Remove sysfs groups */
	sysfs_remove_groups(sysfs_kobj, fcs_groups);

	/* Remove the kobject */
	if (sysfs_kobj)
		kobject_put(sysfs_kobj);

	return platform_driver_unregister(&fcs_driver);
}

module_init(fcs_config_init);
module_exit(fcs_config_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Altera socfpga config driver");
MODULE_AUTHOR("Balsundar Ponnusamy, Santosh Male, Sagar Khadgi");
