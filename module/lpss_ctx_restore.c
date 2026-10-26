// SPDX-License-Identifier: GPL-2.0
/*
 * Restore Intel LPSS (Wildcat Point-LP, PCI mode) private SPI registers
 * after system sleep. On MacBook8,1 firmware does not restore LPSS clock
 * and reset registers on S3 resume and spi-pxa2xx does not either, leaving
 * the SSP block in reset with its clock gated -> applespi transfers time out.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/delay.h>

#define DRV_NAME        "lpss_ctx_restore"
#define LPSS_PRIV_OFF   0x800
#define LPSS_PRIV_SIZE  0x100
#define PRV_CLOCK       0x00
#define PRV_RESETS      0x04
#define PRV_CLOCK_EN    BIT(0)
#define PRV_RESETS_MASK 0x3

static const u32 ctx_regs[] = { 0x00, 0x04, 0x08, 0x0c, 0x18 };
#define NREGS ARRAY_SIZE(ctx_regs)
#define IDX_CLOCK  0
#define IDX_RESETS 1

static const unsigned short lpss_spi_ids[] = {
	0x9ce5,	/* Wildcat Point-LP GSPI0 */
	0x9ce6,	/* Wildcat Point-LP GSPI1 (applespi on MacBook8,1) */
};

struct lpss_ctx {
	struct pci_dev *pdev;
	void __iomem *priv;
	u32 regs[NREGS];
	bool valid;
};

static struct lpss_ctx ctxs[ARRAY_SIZE(lpss_spi_ids)];
static int nctx;
static struct platform_device *lcr_pdev;

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Skip DMI check (untested hardware, use at own risk)");

static void ctx_read(struct lpss_ctx *c, u32 *out)
{
	int i;

	for (i = 0; i < NREGS; i++)
		out[i] = readl(c->priv + ctx_regs[i]);
}

static bool ctx_accessible(const u32 *r)
{
	return r[IDX_CLOCK] != ~0U;	/* all-ones: device in D3 / not decoding */
}

static bool ctx_healthy(const u32 *r)
{
	return ctx_accessible(r) &&
	       (r[IDX_RESETS] & PRV_RESETS_MASK) == PRV_RESETS_MASK &&
	       (r[IDX_CLOCK] & PRV_CLOCK_EN);
}

static void ctx_save(struct lpss_ctx *c)
{
	u32 cur[NREGS];

	ctx_read(c, cur);
	if (ctx_healthy(cur)) {
		memcpy(c->regs, cur, sizeof(cur));
		c->valid = true;
	}
	/* otherwise keep the last known-good context */
}

static void ctx_restore(struct lpss_ctx *c)
{
	u32 cur[NREGS];
	int i;

	if (!c->valid)
		return;
	ctx_read(c, cur);
	if (!ctx_accessible(cur)) {
		dev_warn(&c->pdev->dev, "MMIO not accessible, restore skipped\n");
		return;
	}
	if (ctx_healthy(cur))
		return;		/* firmware restored it, or s2idle */

	writel(c->regs[IDX_RESETS], c->priv + PRV_RESETS);
	udelay(10);
	for (i = 0; i < NREGS; i++)
		if (i != IDX_RESETS)
			writel(c->regs[i], c->priv + ctx_regs[i]);

	dev_info(&c->pdev->dev, "LPSS private context restored\n");
}

static int lcr_save_all(struct device *dev)
{
	int i;

	for (i = 0; i < nctx; i++)
		ctx_save(&ctxs[i]);
	return 0;
}

static int lcr_restore_all(struct device *dev)
{
	int i;

	for (i = 0; i < nctx; i++)
		ctx_restore(&ctxs[i]);
	return 0;
}

static const struct dev_pm_ops lcr_pm_ops = {
	.suspend_late  = lcr_save_all,
	.resume_early  = lcr_restore_all,
	.freeze_late   = lcr_save_all,
	.thaw_early    = lcr_restore_all,
	.poweroff_late = lcr_save_all,
	.restore_early = lcr_restore_all,
};

static int lcr_probe(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver lcr_driver = {
	.probe  = lcr_probe,
	.driver = {
		.name = DRV_NAME,
		.pm   = &lcr_pm_ops,
	},
};

static void lcr_release_ctxs(void)
{
	while (nctx > 0) {
		nctx--;
		iounmap(ctxs[nctx].priv);
		pci_dev_put(ctxs[nctx].pdev);
	}
}

static int __init lcr_init(void)
{
	struct pci_dev *p;
	int i, ret;

	if (!force && !(dmi_match(DMI_SYS_VENDOR, "Apple Inc.") &&
			dmi_match(DMI_PRODUCT_NAME, "MacBook8,1")))
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(lpss_spi_ids); i++) {
		p = pci_get_device(PCI_VENDOR_ID_INTEL, lpss_spi_ids[i], NULL);
		if (!p)
			continue;
		if (pci_resource_len(p, 0) < LPSS_PRIV_OFF + LPSS_PRIV_SIZE) {
			pci_dev_put(p);
			continue;
		}
		ctxs[nctx].pdev = p;
		ctxs[nctx].priv = ioremap(pci_resource_start(p, 0) + LPSS_PRIV_OFF,
					  LPSS_PRIV_SIZE);
		if (!ctxs[nctx].priv) {
			pci_dev_put(p);
			continue;
		}
		ctx_save(&ctxs[nctx]);	/* baseline from firmware-configured state */
		nctx++;
	}
	if (!nctx)
		return -ENODEV;

	ret = platform_driver_register(&lcr_driver);
	if (ret)
		goto err_ctx;

	lcr_pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(lcr_pdev)) {
		ret = PTR_ERR(lcr_pdev);
		goto err_drv;
	}
	return 0;

err_drv:
	platform_driver_unregister(&lcr_driver);
err_ctx:
	lcr_release_ctxs();
	return ret;
}

static void __exit lcr_exit(void)
{
	platform_device_unregister(lcr_pdev);
	platform_driver_unregister(&lcr_driver);
	lcr_release_ctxs();
}

module_init(lcr_init);
module_exit(lcr_exit);
MODULE_DESCRIPTION("Restore LPSS SPI private context after sleep (MacBook8,1)");
MODULE_LICENSE("GPL");
