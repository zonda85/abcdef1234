#
# Makefile for the linux kernel.
#

obj-y					:= socfpga.o
obj-$(CONFIG_SMP)	+= headsmp.o platsmp.o
obj-$(CONFIG_SOCFPGA_SUSPEND)	+= pm.o self-refresh.o
obj-$(CONFIG_FPGADMA)	+= fpga-dma.o
obj-$(CONFIG_EDAC_ALTERA_OCRAM_ECC) += ocram.o ecc.o
obj-$(CONFIG_EDAC_ALTERA_L2_ECC) += l2_cache.o
obj-$(CONFIG_EDAC_ALTERA_NAND_ECC) += nand.o
