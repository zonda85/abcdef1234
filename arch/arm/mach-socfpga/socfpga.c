/*
 *  Copyright (C) 2012-2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/dw_apb_timer.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/stmmac.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>
#include <linux/sys_soc.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/pmu.h>

#include "core.h"
#include "socfpga_cti.h"
#include "dma.h"
#include "l2_cache.h"
#include "ocram.h"
#include "dwmac-socfpga.h"

void __iomem *socfpga_scu_base_addr = ((void __iomem *)(SOCFPGA_SCU_VIRT_BASE));
void __iomem *sys_manager_base_addr;
void __iomem *rst_manager_base_addr;
void __iomem *sdr_ctl_base_addr;
void __iomem *l3regs_base_addr;

void __iomem *clk_mgr_base_addr;
unsigned long cpu1start_addr;

static int stmmac_plat_init(struct platform_device *pdev);
static void stmmac_fix_mac_speed(void *priv, unsigned int speed);

static int socfpga_is_a5(void);
static int socfpga_is_c5(void);
static int socfpga_is_a10(void);

static struct plat_stmmacenet_data stmmacenet0_data = {
	.init = &stmmac_plat_init,
	.bus_id = 0,
	.fix_mac_speed = stmmac_fix_mac_speed,
};

static struct plat_stmmacenet_data stmmacenet1_data = {
	.init = &stmmac_plat_init,
	.bus_id = 1,
	.fix_mac_speed = stmmac_fix_mac_speed,
};

static struct plat_stmmacenet_data stmmacenet2_data = {
	.init = &stmmac_plat_init,
	.bus_id = 2,
	.fix_mac_speed = stmmac_fix_mac_speed,
};

#ifdef CONFIG_HW_PERF_EVENTS
static struct arm_pmu_platdata socfpga_pmu_platdata = {
	.handle_irq = socfpga_pmu_handler,
	.init = socfpga_init_cti,
	.start = socfpga_start_cti,
	.stop = socfpga_stop_cti,
};
#endif

static const struct of_dev_auxdata socfpga_auxdata_lookup[] __initconst = {
	/* Platform data entries for stmmac on Cyclone and Arria 5 */
	OF_DEV_AUXDATA("snps,dwmac-3.70a", 0xff700000, NULL, &stmmacenet0_data),
	OF_DEV_AUXDATA("snps,dwmac-3.70a", 0xff702000, NULL, &stmmacenet1_data),
	/* Platform data entries for stmmac on Arria 10 */
	OF_DEV_AUXDATA("snps,dwmac-3.72a", 0xff800000, NULL, &stmmacenet0_data),
	OF_DEV_AUXDATA("snps,dwmac-3.72a", 0xff802000, NULL, &stmmacenet1_data),
	OF_DEV_AUXDATA("snps,dwmac-3.72a", 0xff804000, NULL, &stmmacenet2_data),
	OF_DEV_AUXDATA("arm,pl330", 0xffe00000, "dma-pl330",
		&dma_platform_data),
	OF_DEV_AUXDATA("arm,pl330", 0xffe01000, "dma-pl330",
		&dma_platform_data),
#ifdef CONFIG_HW_PERF_EVENTS
	OF_DEV_AUXDATA("arm,cortex-a9-pmu", 0, "arm-pmu", &socfpga_pmu_platdata),
#endif
	{ /* sentinel */ }
};

static struct map_desc scu_io_desc __initdata = {
	.virtual	= SOCFPGA_SCU_VIRT_BASE,
	.pfn		= 0, /* run-time */
	.length		= SZ_8K,
	.type		= MT_DEVICE,
};

static void __init socfpga_soc_device_init(void)
{
	struct device_node *root;
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;
	const char *machine;
	u32 id = SOCFPGA_ID_DEFAULT;
	u32 rev = SOCFPGA_REVISION_DEFAULT;
	int err;

	root = of_find_node_by_path("/");
	if (!root)
		return;

	err = of_property_read_string(root, "model", &machine);
	if (err)
		return;

	of_node_put(root);

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return;

	/* Read Silicon ID from System manager */
	if (sys_manager_base_addr) {
		id =  __raw_readl(sys_manager_base_addr +
			SYSMGR_SILICON_ID1_OFFSET);
		rev = (id & SYSMGR_SILICON_ID1_REV_MASK)
				>> SYSMGR_SILICON_ID1_REV_SHIFT;
		id = (id & SYSMGR_SILICON_ID1_ID_MASK)
				>> SYSMGR_SILICON_ID1_ID_SHIFT;
	}

	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "%u", id);
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d", rev);
	soc_dev_attr->machine = kasprintf(GFP_KERNEL, "%s", machine);
	soc_dev_attr->family = "SOCFPGA";

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR_OR_NULL(soc_dev)) {
		kfree(soc_dev_attr->soc_id);
		kfree(soc_dev_attr->machine);
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr);
		return;
	}

	return;
}

static void __init socfpga_scu_map_io(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_io_desc.pfn = __phys_to_pfn(base);
	iotable_init(&scu_io_desc, 1);
}

static void __init enable_periphs(void)
{
	/* Release all peripherals, except for emacs, from reset.*/
	u32 rstval;
	rstval = RSTMGR_PERMODRST_EMAC0 | RSTMGR_PERMODRST_EMAC1;

	if (socfpga_is_a10()) {
		/* temp hack to enable all periphs from reset for A10 */
		writel(0x0, rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);
		writel(0x0, rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER1MODRST);
	} else {
		writel(rstval, rst_manager_base_addr + SOCFPGA_RSTMGR_MODPERRST);
	}
}

#define MICREL_KSZ9021_EXTREG_CTRL		11
#define MICREL_KSZ9021_EXTREG_DATA_WRITE	12
#define MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SKEW	260
#define MICREL_KSZ9021_RGMII_RX_DATA_PAD_SKEW	261

#define MICREL_KSZ9031_MMD_ACCESS_CONTROL	0xd
#define MICREL_KSZ9031_MMD_ACCESS_DATA		0xe

#define MICREL_KSZ9031_MMD_OP_DATANOINC		0x4000

/*
 * Note that these skew settings depend on an FPGA I/O Delay of
 * 600ps applied to the TX_CLK output to the phy in order
 * for these settings to work correctly.
 * See the Application notes on this topic at http://www.altera.com
 */
#define MICREL_KSZ9031_A10_CLOCK_SKEW		0x3fc
#define MICREL_KSZ9031_A10_CTRL_SKEW		0x070
#define MICREL_KSZ9031_A10_RXD_SKEW		0x7777
#define MICREL_KSZ9031_A10_TXD_SKEW		0x0

static int ksz9021rlrn_phy_fixup(struct phy_device *phydev)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		/* min rx data delay */
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			  MICREL_KSZ9021_RGMII_RX_DATA_PAD_SKEW | 0x8000);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_DATA_WRITE, 0x0000);

		/* max rx/tx clock delay, min rx/tx control delay */
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			  MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SKEW | 0x8000);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_DATA_WRITE, 0xa0d0);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL, 0x104);
	}

	return 0;
}

static void ksz9031_write_mmd_register(struct phy_device *phydev,
				       unsigned short mmd_address,
				       unsigned short mmdreg,
				       unsigned short mmdval)
{
	unsigned short mmd_address_op = mmd_address |
		MICREL_KSZ9031_MMD_OP_DATANOINC;

	phy_write(phydev, MICREL_KSZ9031_MMD_ACCESS_CONTROL, mmd_address);
	phy_write(phydev, MICREL_KSZ9031_MMD_ACCESS_DATA, mmdreg);
	phy_write(phydev, MICREL_KSZ9031_MMD_ACCESS_CONTROL, mmd_address_op);
	phy_write(phydev, MICREL_KSZ9031_MMD_ACCESS_DATA, mmdval);
}

static int ksz9031_phy_fixup(struct phy_device *phydev)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		/*
		 * See the Micrel ksz9031rnx specification for details on
		 * how to access the MMD registers to configure the skew
		 * values.
		 */

		/* set ksz9031, register 8 - clock pad skews */
		ksz9031_write_mmd_register(phydev, 2, 8,
					   MICREL_KSZ9031_A10_CLOCK_SKEW);

		/* set ksz9031, register 4 - control skews */
		ksz9031_write_mmd_register(phydev, 2, 4,
					   MICREL_KSZ9031_A10_CTRL_SKEW);

		/* set ksz9031, register 5 - RX Data skews */
		ksz9031_write_mmd_register(phydev, 2, 5,
					   MICREL_KSZ9031_A10_RXD_SKEW);

		/* set ksz9031, register 6 - TX Data skews */
		ksz9031_write_mmd_register(phydev, 2, 6,
					   MICREL_KSZ9031_A10_TXD_SKEW);

		ksz9031_write_mmd_register(phydev, 0, 4, 0x0006);
		ksz9031_write_mmd_register(phydev, 0, 3, 0x1A80);
	}
	return 0;
}

static void stmmac_fix_mac_speed(void *priv, unsigned int speed)
{
	adapter_config(priv, speed);
}

static int stmmac_init_c5a5(struct platform_device *pdev)
{
	u32 ctrl, val, shift;
	u32 rstmask;
	int phymode;
	int ret;

	phymode = of_get_phy_mode(pdev->dev.of_node);

	switch (phymode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII;
		break;
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_SGMII:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII;
		break;
	/*
	 * RMII case is broken on Cyclone 5 and Arria 5.
	 * Have the case fall through to failure explicitly
	 * for documentation and debugging purposes.
	 */
	case PHY_INTERFACE_MODE_RMII:
	default:
		pr_err("%s bad phy mode %d", __func__, phymode);
		return -EINVAL;
	}

	ret = adapter_init(pdev, phymode, &val);
	if (ret != 0) {
		pr_err("adapter init fail\n");
		return ret;
	}

	if (&stmmacenet1_data == pdev->dev.platform_data) {
		shift = SYSMGR_EMACGRP_CTRL_PHYSEL_WIDTH;
		rstmask = RSTMGR_PERMODRST_EMAC1;
	} else if (&stmmacenet0_data == pdev->dev.platform_data) {
		shift = 0;
		rstmask = RSTMGR_PERMODRST_EMAC0;
	} else {
		pr_err("%s unexpected platform data pointer\n", __func__);
		return -EINVAL;
	}

	ctrl = readl(sys_manager_base_addr + SYSMGR_EMACGRP_CTRL_OFFSET);
	ctrl &= ~(SYSMGR_EMACGRP_CTRL_PHYSEL_MASK << shift);
	ctrl |= (val << shift);

	writel(ctrl, (sys_manager_base_addr + SYSMGR_EMACGRP_CTRL_OFFSET));

	ctrl = readl(rst_manager_base_addr + SOCFPGA_RSTMGR_MODPERRST);
	ctrl &= ~(rstmask);
	writel(ctrl, rst_manager_base_addr + SOCFPGA_RSTMGR_MODPERRST);

	return 0;
}

static int stmmac_init_a10(struct platform_device *pdev)
{
	u32 ctrl, val;
	u32 rstmask;
	u32 rstmask_ecc;
	int phymode;
	int ret;
	int sysmgr_emac_offs;

	phymode = of_get_phy_mode(pdev->dev.of_node);

	switch (phymode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII;
		break;
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_SGMII:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RMII;
		break;
	default:
		pr_err("%s bad phy mode %d", __func__, phymode);
		return -EINVAL;
	}

	ret = adapter_init(pdev, phymode, &val);
	if (ret != 0) {
		pr_err("adapter init fail\n");
		return ret;
	}

	if (&stmmacenet2_data == pdev->dev.platform_data) {
		sysmgr_emac_offs = SOCFPGA_A10_SYSMGR_EMAC2_CTRL;
		rstmask = RSTMGR_PER0MODRST_A10_EMAC2;
		rstmask_ecc = RSTMGR_PER0MODRST_A10_EMAC2_ECC;
	} else if (&stmmacenet1_data == pdev->dev.platform_data) {
		sysmgr_emac_offs = SOCFPGA_A10_SYSMGR_EMAC1_CTRL;
		rstmask = RSTMGR_PERMODRST_EMAC1;
		rstmask_ecc = RSTMGR_PER0MODRST_A10_EMAC1_ECC;
	} else if (&stmmacenet0_data == pdev->dev.platform_data) {
		sysmgr_emac_offs = SOCFPGA_A10_SYSMGR_EMAC0_CTRL;
		rstmask = RSTMGR_PERMODRST_EMAC0;
		rstmask_ecc = RSTMGR_PER0MODRST_A10_EMAC0_ECC;
	} else {
		pr_err("%s unexpected platform data pointer\n", __func__);
		return -EINVAL;
	}

	/* Place the EMAC into reset */
	ctrl = readl(rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);
	ctrl |= rstmask_ecc;
	writel(ctrl, rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);

	ctrl = readl(rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);
	ctrl |= rstmask;
	writel(ctrl, rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);

	/* Set the desired phy mode bits */
	ctrl = readl(sys_manager_base_addr + sysmgr_emac_offs);
	ctrl &= ~SYSMGR_EMACGRP_CTRL_PHYSEL_MASK;
	ctrl |= val;
	writel(ctrl, (sys_manager_base_addr + sysmgr_emac_offs));

	/* Make sure to hold EMAC in reset for a min amount of time */
	udelay(1);

	/* Take the EMAC out of reset */
	ctrl = readl(rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);
	ctrl &= ~(rstmask);
	writel(ctrl, rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);

	ctrl = readl(rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);
	ctrl &= ~(rstmask_ecc);
	writel(ctrl, rst_manager_base_addr + SOCFPGA_A10_RSTMGR_PER0MODRST);

	/* Wait for some min amount of time to make sure the EMAC is reset */
	udelay(1);

	return 0;
}

static int socfpga_is_a10(void)
{
	return of_machine_is_compatible("altr,socfpga-arria10");
}

static int socfpga_is_c5(void)
{
	return of_machine_is_compatible("altr,socfpga-cyclone5");
}

static int socfpga_is_a5(void)
{
	return of_machine_is_compatible("altr,socfpga-arria5");
}

static int stmmac_plat_init(struct platform_device *pdev)
{
	if (socfpga_is_a10())
		return stmmac_init_a10(pdev);

	if (socfpga_is_c5() || socfpga_is_a5())
		return stmmac_init_c5a5(pdev);

	return 0;
}

static void __init socfpga_map_io(void)
{
	socfpga_scu_map_io();
	early_printk("Early printk initialized\n");
}

static void __init socfpga_sysmgr_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "altr,sys-mgr");
	if (!np) {
		pr_err("SOCFPGA: Unable to find sys-magr in dtb\n");
		return;
	}

	if (of_property_read_u32(np, "cpu1-start-addr",
			(u32 *) &cpu1start_addr))
		pr_err("SMP: Need cpu1-start-addr in device tree.\n");

	sys_manager_base_addr = of_iomap(np, 0);
	WARN_ON(!sys_manager_base_addr);

	np = of_find_compatible_node(NULL, NULL, "altr,rst-mgr");
	if (!np) {
		pr_err("SOCFPGA: Unable to find rst-mgr in dtb\n");
		return;
	}

	rst_manager_base_addr = of_iomap(np, 0);
	WARN_ON(!rst_manager_base_addr);

	np = of_find_compatible_node(NULL, NULL, "altr,clk-mgr");
	if (!np) {
		pr_err("SOCFPGA: Unable to find clk-mgr\n");
		return;
	}

	clk_mgr_base_addr = of_iomap(np, 0);
	WARN_ON(!clk_mgr_base_addr);

	np = of_find_compatible_node(NULL, NULL, "altr,sdr-ctl");
	if (!np) {
		pr_err("SOCFPGA: Unable to find sdr-ctl\n");
		return;
	}

	sdr_ctl_base_addr = of_iomap(np, 0);
	WARN_ON(!sdr_ctl_base_addr);

	np = of_find_compatible_node(NULL, NULL, "altr,l3regs");
	if (!np) {
		pr_err("SOCFPGA: Unable to find l3regs\n");
		return;
	}

	l3regs_base_addr = of_iomap(np, 0);
	WARN_ON(!l3regs_base_addr);
}

static void __init socfpga_init_irq(void)
{
	irqchip_init();
	socfpga_sysmgr_init();

	of_clk_init(NULL);
	clocksource_of_init();
}

static void socfpga_cyclone5_restart(char mode, const char *cmd)
{
	u32 temp;

	/* Turn on all periph PLL clocks */
	if (!of_machine_is_compatible("altr,socfpga-arria10")) {
		writel(0xffff, clk_mgr_base_addr + SOCFPGA_ENABLE_PLL_REG);
		temp = readl(rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);
	} else {
		temp = readl(rst_manager_base_addr + SOCFPGA_A10_RSTMGR_CTRL);
	}

	if (mode == 'h')
		temp |= RSTMGR_CTRL_SWCOLDRSTREQ;
	else
		temp |= RSTMGR_CTRL_SWWARMRSTREQ;

	if (of_machine_is_compatible("altr,socfpga-arria10")) {
		writel(temp, rst_manager_base_addr + SOCFPGA_A10_RSTMGR_CTRL);
	} else {
		writel(temp, rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);
	}
}

static void __init socfpga_cyclone5_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	u32 aux_ctrl = 0;
	socfpga_init_l2_ecc();
	aux_ctrl |= (1 << L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT) |
			(1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT);
	l2x0_of_init(aux_ctrl, ~0UL);
#endif
	of_platform_populate(NULL, of_default_bus_match_table,
		socfpga_auxdata_lookup, NULL);

	socfpga_init_ocram_ecc();

	enable_periphs();

	socfpga_soc_device_init();
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		phy_register_fixup_for_uid(PHY_ID_KSZ9021RLRN,
					   MICREL_PHY_ID_MASK,
					   ksz9021rlrn_phy_fixup);
		phy_register_fixup_for_uid(PHY_ID_KSZ9031,
					   MICREL_PHY_ID_MASK,
					   ksz9031_phy_fixup);
	}
}

static const char *altera_dt_match[] = {
	"altr,socfpga",
	NULL
};

DT_MACHINE_START(SOCFPGA, "Altera SOCFPGA")
	.smp		= smp_ops(socfpga_smp_ops),
	.map_io		= socfpga_map_io,
	.init_irq	= socfpga_init_irq,
	.init_time	= dw_apb_timer_init,
	.init_machine	= socfpga_cyclone5_init,
	.restart	= socfpga_cyclone5_restart,
	.dt_compat	= altera_dt_match,
MACHINE_END
