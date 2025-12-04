#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <soc/samsung/cal-if.h>

#include "fvmap.h"
#include "cmucal.h"
#include "vclk.h"
#include "ra.h"
#include "acpm_dvfs.h"

#define FVMAP_SIZE (SZ_16K)

#define STEP_UV			(6250)

void __iomem *fvmap_base;
void __iomem *sram_fvmap_base;

int init_margin_table[10];

int set_mif_volt;
int set_int_volt;
int set_cpucl0_volt;
int set_cpucl1_volt;
int set_g3d_volt;
int set_intcam_volt;
int set_cam_volt;
int set_disp_volt;
int set_g3dm_volt;
int set_cp_volt;

static int __init get_mif_volt(char *str)
{
	get_option(&str, &set_mif_volt);
	init_margin_table[0] = set_mif_volt;
	return 0;
}
early_param("mif", get_mif_volt);

static int __init get_int_volt(char *str)
{
	get_option(&str, &set_int_volt);
	init_margin_table[1] = set_int_volt;
	return 0;
}
early_param("int", get_int_volt);

static int __init get_cpucl0_volt(char *str)
{
	get_option(&str, &set_cpucl0_volt);
	init_margin_table[2] = set_cpucl0_volt;
	return 0;
}
early_param("big", get_cpucl0_volt);

static int __init get_cpucl1_volt(char *str)
{
	get_option(&str, &set_cpucl1_volt);
	init_margin_table[3] = set_cpucl1_volt;
	return 0;
}
early_param("lit", get_cpucl1_volt);

static int __init get_g3d_volt(char *str)
{
	get_option(&str, &set_g3d_volt);
	init_margin_table[4] = set_g3d_volt;
	return 0;
}
early_param("g3d", get_g3d_volt);

static int __init get_intcam_volt(char *str)
{
	get_option(&str, &set_intcam_volt);
	init_margin_table[5] = set_intcam_volt;
	return 0;
}
early_param("intcam", get_intcam_volt);

static int __init get_cam_volt(char *str)
{
	get_option(&str, &set_cam_volt);
	init_margin_table[6] = set_cam_volt;
	return 0;
}
early_param("cam", get_cam_volt);

static int __init get_disp_volt(char *str)
{
	get_option(&str, &set_disp_volt);
	init_margin_table[7] = set_disp_volt;
	return 0;
}
early_param("disp", get_disp_volt);

static int __init get_g3dm_volt(char *str)
{
	get_option(&str, &set_g3dm_volt);
	init_margin_table[8] = set_g3dm_volt;
	return 0;
}
early_param("g3dm", get_g3dm_volt);

static int __init get_cp_volt(char *str)
{
	get_option(&str, &set_cp_volt);
	init_margin_table[9] = set_cp_volt;
	return 0;
}
early_param("cp", get_cp_volt);

int fvmap_set_raw_voltage_table(unsigned int id, int uV)
{
	struct fvmap_header *fvmap_header;
	struct rate_volt_header *fv_table;
	int num_of_lv;
	int idx, i;

	idx = GET_IDX(id);

	fvmap_header = sram_fvmap_base;
	fv_table = sram_fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		fv_table->table[i].volt += uV;

	return 0;
}

int fvmap_get_voltage_table(unsigned int id, unsigned int *table)
{
	struct fvmap_header *fvmap_header = fvmap_base;
	struct rate_volt_header *fv_table;
	int idx, i;
	int num_of_lv;

	if (!IS_ACPM_VCLK(id))
		return 0;

	idx = GET_IDX(id);

	fvmap_header = fvmap_base;
	fv_table = fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		table[i] = fv_table->table[i].volt;

	return num_of_lv;

}

int fvmap_get_raw_voltage_table(unsigned int id)
{
	struct fvmap_header *fvmap_header;
	struct rate_volt_header *fv_table;
	int idx, i;
	int num_of_lv;
	unsigned int table[20];

	idx = GET_IDX(id);

	fvmap_header = sram_fvmap_base;
	fv_table = sram_fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		table[i] = fv_table->table[i].volt;

	for (i = 0; i < num_of_lv; i++)
		printk("dvfs id : %d  %d Khz : %d uv\n", ACPM_VCLK_TYPE | id, fv_table->table[i].rate, table[i]);

	return 0;
}

static void optimize_rate_volt_table(struct rate_volt_header *head, unsigned int num_of_lv) {
	bool changed;
	int i;

	/* optimize voltages */
	while (true) {
		changed = false;

		for (i = 1; i < num_of_lv; i++) {
			/* switch voltages if higher frequency uses less */
			if (head->table[i].volt > head->table[i-1].volt) {
				int temp_volt = head->table[i-1].volt;

				head->table[i-1].volt = head->table[i].volt;
				head->table[i].volt = temp_volt;

				changed = true;
			}
		}

		if (!changed)
			break;
	}
}

static inline ssize_t print_fvmap(char *buf, int start, int end)
{
	volatile struct fvmap_header *fvmap_header;
	struct rate_volt_header *cur;
	struct vclk *vclk;
	int size;
	int i, j;
	ssize_t len = 0;

	fvmap_header = fvmap_base;
	size = cmucal_get_list_size(ACPM_VCLK_TYPE);

	for (i = 0; i < size; i++) {
		vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);
		if (vclk == NULL)
			continue;
		cur = fvmap_base + fvmap_header[i].o_ratevolt;

		pr_info("dvfs_type : %s - id : %x\n",
			vclk->name, fvmap_header[i].dvfs_type);
		pr_info("  num_of_lv      : %d\n", fvmap_header[i].num_of_lv);
		pr_info("  num_of_members : %d\n", fvmap_header[i].num_of_members);
		for (j = 0; j < fvmap_header[i].num_of_lv; j++)
			pr_info("  lv : [%7d], volt = %d uV\n",
				cur->table[j].rate, cur->table[j].volt);

		if (buf != NULL && i >= start && i < end) {
			len += sprintf(buf + len, "dvfs_type : %s - id : %u\n",
				vclk->name, fvmap_header[i].dvfs_type);
			len += sprintf(buf + len, "  num_of_lv      : %d\n", fvmap_header[i].num_of_lv);

			for (j = 0; j < fvmap_header[i].num_of_lv; j++)
				len += sprintf(buf + len, "  rate : %7d Hz, volt : %d uV\n",
					cur->table[j].rate, cur->table[j].volt);
		}
	}

	return len;
}

ssize_t fvmap_print(char *buf, unsigned int dvfs_type)
{
	volatile struct fvmap_header *fvmap_header;
	struct rate_volt_header *cur;
	int size;
	int i, j;
	ssize_t len = 0;

	fvmap_header = fvmap_base;
	size = cmucal_get_list_size(ACPM_VCLK_TYPE);

	for (i = 0; i < size; i++) {
		if (fvmap_header[i].dvfs_type == dvfs_type) {
			cur = fvmap_base + fvmap_header[i].o_ratevolt;

			for (j = 0; j < fvmap_header[i].num_of_lv; j++)
				len += sprintf(buf + len, "%d %d\n",
					cur->table[j].rate, cur->table[j].volt);
		}
	}

	return len;
}

static void fvmap_copy_from_sram(void __iomem *map_base, void __iomem *sram_base)
{
	volatile struct fvmap_header *fvmap_header, *header;
	struct rate_volt_header *old, *new;
	struct clocks *clks;
	struct pll_header *plls;
	struct vclk *vclk;
	struct cmucal_clk *clk_node;
	unsigned int paddr_offset, fvaddr_offset;
	int size;
	int i, j;

	fvmap_header = map_base;
	header = sram_base;

	size = cmucal_get_list_size(ACPM_VCLK_TYPE);

	for (i = 0; i < size; i++) {
		fvmap_header[i].dvfs_type = header[i].dvfs_type;
		fvmap_header[i].num_of_lv = header[i].num_of_lv;
		fvmap_header[i].num_of_members = header[i].num_of_members;
		fvmap_header[i].num_of_pll = header[i].num_of_pll;
		fvmap_header[i].num_of_mux = header[i].num_of_mux;
		fvmap_header[i].num_of_div = header[i].num_of_div;
		fvmap_header[i].gearratio = header[i].gearratio;
		fvmap_header[i].init_lv = header[i].init_lv;
		fvmap_header[i].num_of_gate = header[i].num_of_gate;
		fvmap_header[i].reserved[0] = header[i].reserved[0];
		fvmap_header[i].reserved[1] = header[i].reserved[1];
		fvmap_header[i].block_addr[0] = header[i].block_addr[0];
		fvmap_header[i].block_addr[1] = header[i].block_addr[1];
		fvmap_header[i].block_addr[2] = header[i].block_addr[2];
		fvmap_header[i].o_members = header[i].o_members;
		fvmap_header[i].o_ratevolt = header[i].o_ratevolt;
		fvmap_header[i].o_tables = header[i].o_tables;

		vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);
		if (vclk == NULL)
			continue;
		pr_info("dvfs_type : %s - id : %x\n",
			vclk->name, fvmap_header[i].dvfs_type);
		pr_info("  num_of_lv      : %d\n", fvmap_header[i].num_of_lv);
		pr_info("  num_of_members : %d\n", fvmap_header[i].num_of_members);

		old = sram_base + fvmap_header[i].o_ratevolt;
		new = map_base + fvmap_header[i].o_ratevolt;
		
		optimize_rate_volt_table(old, fvmap_header[i].num_of_lv);
		
		if (init_margin_table[i])
			cal_dfs_set_volt_margin(i | ACPM_VCLK_TYPE,
						init_margin_table[i]);

		if (i == 0) {
			unsigned int mif_freqs[12] = {2093000, 2002000, 1794000, 1540000, 1352000, 1014000, 845000, 676000, 546000, 421000, 286000, 208000};
			unsigned int mif_volts[12] = {925000, 862500, 750000, 700000, 668750, 625000, 593750, 581250, 575000, 575000, 575000, 575000};
			for (j = 0; j < 12; j++) {
				new->table[j].rate = mif_freqs[j];
				new->table[j].volt = mif_volts[j];
				old->table[j].volt = mif_volts[j];
				pr_info("  lv : [%7d], volt = %d uV\n",
					new->table[j].rate, new->table[j].volt);
			}
		}
		else if (i == 4) {
			unsigned int g3d_freqs[9] = {839000, 764000, 683000, 572000, 546000, 455000, 385000, 338000, 260000};
			unsigned int g3d_volts[9] = {925000, 750000, 700000, 668750, 656250, 631250, 631250, 625000, 625000};
			for (j = 0; j < 9; j++) {
				new->table[j].rate = g3d_freqs[j];
				new->table[j].volt = g3d_volts[j];
				old->table[j].volt = g3d_volts[j];
				pr_info("  lv : [%7d], volt = %d uV\n",
					new->table[j].rate, new->table[j].volt);
			}
		}
		else if (i == 8) {
			unsigned int g3dm_freqs[9] = {839000, 764000, 683000, 572000, 546000, 455000, 385000, 338000, 260000};
			unsigned int g3dm_volts[9] = {925000, 750000, 700000, 668750, 656250, 631250, 631250, 625000, 625000};
			for (j = 0; j < 9; j++) {
				new->table[j].rate = g3dm_freqs[j];
				new->table[j].volt = g3dm_volts[j];
				old->table[j].volt = g3dm_volts[j];
			}
		}
		else {
			for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
				new->table[j].rate = old->table[j].rate;
				new->table[j].volt = old->table[j].volt;
			}
		}

		for (j = 0; j < fvmap_header[i].num_of_pll; j++) {
			clks = sram_base + fvmap_header[i].o_members;
			plls = sram_base + clks->addr[j];
			clk_node = cmucal_get_node(vclk->list[j]);
			if (clk_node == NULL)
				continue;
			paddr_offset = clk_node->paddr & 0xFFFF;
			fvaddr_offset = plls->addr & 0xFFFF;
			if (paddr_offset == fvaddr_offset)
				continue;

			clk_node->paddr += fvaddr_offset - paddr_offset;
			clk_node->pll_con0 += fvaddr_offset - paddr_offset;
			if (clk_node->pll_con1)
				clk_node->pll_con1 += fvaddr_offset - paddr_offset;
		}
	}
}

static int patch_fvmap(void __iomem *map_base, unsigned int dvfs_type, unsigned int rate, unsigned int volt)
{
	volatile struct fvmap_header *fvmap_header;
	struct rate_volt_header *rvh;
	bool exists = false;
	int size, rest;
	int i, j, k;
	int ret = 0;

	if (rate < 1)
		return -1;

	fvmap_header = map_base;
	size = cmucal_get_list_size(ACPM_VCLK_TYPE);

	if (volt > 10000)
		if ((rest = volt % STEP_UV) != 0) 
				volt += STEP_UV - rest;

	for (i = 0; i < size; i++) {
		if (fvmap_header[i].dvfs_type == dvfs_type) {
			rvh = map_base + fvmap_header[i].o_ratevolt;

			for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
				if (rvh->table[j].rate == rate) {
					exists = true;
					break;
				}
			}

			if (exists) {
				if (volt > 0) {
					for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
						if (rvh->table[j].rate == rate) {
							if (volt > 10000)
								rvh->table[j].volt = volt;
							else
								rvh->table[j].volt += volt * STEP_UV;
							break;
						}
					}
				} else {
					for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
						if (rvh->table[j].rate == rate) {
							for (k = j; k < fvmap_header[i].num_of_lv - 1; k++) {
								rvh->table[k].rate = rvh->table[k + 1].rate;
								rvh->table[k].volt = rvh->table[k + 1].volt;
							}
							fvmap_header[i].num_of_lv--;
							break;
						}
					}
				}
				ret = 1;
			} else {
				int insertPosition = -1;

				for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
					if (rvh->table[j].rate < rate) {
						insertPosition = j;
						break;
					}
				}

				if (insertPosition < 0)
					insertPosition = fvmap_header[i].num_of_lv;

				for (j = fvmap_header[i].num_of_lv; j > insertPosition; j--) {
					rvh->table[j].rate = rvh->table[j - 1].rate;
					rvh->table[j].volt = rvh->table[j - 1].volt;
				}

				rvh->table[insertPosition].rate = rate;
				if (volt > 10000)
					rvh->table[insertPosition].volt = volt;
				else if (volt > 0)
					rvh->table[insertPosition].volt += volt * STEP_UV;

				fvmap_header[i].num_of_lv++;
			}
			break;
		}
	}

	return ret;
}

int fvmap_patch(unsigned int dvfs_type, unsigned int rate, unsigned int volt)
{
	int ret;

	ret = patch_fvmap(fvmap_base, dvfs_type, rate, volt);
	if (ret == 1) {
		ret = 0;
		patch_fvmap(sram_fvmap_base, dvfs_type, rate, volt);
	}

	return ret;
}

static unsigned int read_fvmap(void __iomem *map_base, unsigned int dvfs_type, int mode, unsigned int value)
{
	volatile struct fvmap_header *fvmap_header;
	struct rate_volt_header *rvh;
	int size, rest;
	int i, j;
	unsigned int ret = 0;

	fvmap_header = map_base;
	size = cmucal_get_list_size(ACPM_VCLK_TYPE);

	if (mode == READ_RATE)
		if ((rest = value % STEP_UV) != 0) 
				value += STEP_UV - rest;

	for (i = 0; i < size; i++) {
		if (fvmap_header[i].dvfs_type == dvfs_type) {
			rvh = map_base + fvmap_header[i].o_ratevolt;

			for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
				if (mode == READ_VOLT && rvh->table[j].rate == value) {
					ret = rvh->table[j].volt;
					break;
				} else if (mode == READ_RATE && rvh->table[j].volt == value) {
					ret = rvh->table[j].rate;
					break;
				}
			}
		}
	}

	return ret;
}

unsigned int fvmap_read(unsigned int dvfs_type, int mode, unsigned int value)
{
	unsigned int ret;
	
	ret = read_fvmap(fvmap_base, dvfs_type, mode, value);
	if (!ret)
		ret = read_fvmap(sram_fvmap_base, dvfs_type, mode, value);

	return ret;
}

static ssize_t show_patch(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return print_fvmap(buf, 2, 5); /* Only Print CL0/CL1/G3D to Buffer */
}

static ssize_t store_patch(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	volatile struct fvmap_header *fvmap_header;
	unsigned int dvfs, rate, volt = 0;
	struct vclk *vclk;
	char dvfs_name[63];
	size_t ret;
	int size;
	int i;

	if (sscanf(buf, "%s %u %u", dvfs_name, &rate, &volt) != 3 &&
		sscanf(buf, "%u %u %u", &dvfs, &rate, &volt) != 3 &&
		sscanf(buf, "%s:%u:%u", dvfs_name, &rate, &volt) != 3 &&
		sscanf(buf, "%u:%u:%u", &dvfs, &rate, &volt) != 3)
		return -EINVAL;

	if (!dvfs) {
		fvmap_header = fvmap_base;
		size = cmucal_get_list_size(ACPM_VCLK_TYPE);

		for (i = 0; i < size; i++) {
			vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);
			if (vclk == NULL)
				continue;
			if (strstr(vclk->name, dvfs_name) != NULL) {
				dvfs = fvmap_header[i].dvfs_type;
				break;
			}
		}
	}

	if (!dvfs)
		return -EINVAL;

	ret = fvmap_patch(dvfs, rate, volt);
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute patch =
__ATTR(patch, 0644, show_patch, store_patch);

static struct attribute *fvmap_attrs[] = {
	&patch.attr,
	NULL,
};

static const struct attribute_group fvmap_group = {
	.attrs = fvmap_attrs,
};

static struct dentry *fvmap_debugfs_dir;

static int sram_debugfs_open(struct inode *inode, struct file *file)
{
	struct fvmap_header *fvmap_header;
	struct rate_volt_header *fv_table;
	int num_of_vclks;
	int num_of_lv;
	int idx, i;
	char *buf;
	size_t buf_size = 1048576;
	ssize_t len = 0;

	if (!sram_fvmap_base)
		return -ENODEV;

	buf = vmalloc(buf_size);
	if (!buf)
		return -ENOMEM;

	num_of_vclks = cmucal_get_list_size(ACPM_VCLK_TYPE);
	fvmap_header = sram_fvmap_base;

	len += snprintf(buf + len, buf_size - len, "SRAM Frequency-Voltage Table Data:\n");
	len += snprintf(buf + len, buf_size - len, "==================================\n\n");

	for (idx = 0; idx < num_of_vclks; idx++) {
		struct vclk *vclk = cmucal_get_node(ACPM_VCLK_TYPE | idx);
		const char *vclk_name = vclk ? vclk->name : "UNKNOWN";

		if (len >= buf_size - 512)
			break;

		fv_table = sram_fvmap_base + fvmap_header[idx].o_ratevolt;
		num_of_lv = fvmap_header[idx].num_of_lv;

		len += snprintf(buf + len, buf_size - len,
				"Domain: %s (ID: 0x%x, Margin ID: %d)\n",
				vclk_name, ACPM_VCLK_TYPE | idx, idx);
		len += snprintf(buf + len, buf_size - len,
				"Levels: %d, Members: %d\n",
				num_of_lv, fvmap_header[idx].num_of_members);
		len += snprintf(buf + len, buf_size - len,
				"----------------------------------------\n");
		len += snprintf(buf + len, buf_size - len,
				"Level | Frequency(kHz) | Voltage(uV)   |\n");
		len += snprintf(buf + len, buf_size - len,
				"----------------------------------------\n");

		for (i = 0; i < num_of_lv; i++) {
			len += snprintf(buf + len, buf_size - len,
					"%-5d | %-14u | %-12u |\n",
					i,
					fv_table->table[i].rate,
					fv_table->table[i].volt);
		}

		len += snprintf(buf + len, buf_size - len,
				"----------------------------------------\n");
		len += snprintf(buf + len, buf_size - len,
				"Current Margin: %d%%\n\n", init_margin_table[idx]);
	}

	file->private_data = buf;
	return 0;
}

static ssize_t sram_debugfs_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char *buf = file->private_data;
	size_t len = strlen(buf);

	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static int sram_debugfs_release(struct inode *inode, struct file *file)
{
	vfree(file->private_data);
	return 0;
}

static const struct file_operations sram_debugfs_fops = {
	.open = sram_debugfs_open,
	.read = sram_debugfs_read,
	.release = sram_debugfs_release,
	.llseek = default_llseek,
};

static int __init fvmap_debugfs_init(void)
{
	fvmap_debugfs_dir = debugfs_create_dir("fvmap", NULL);
	if (!fvmap_debugfs_dir) {
		pr_err("%s: failed to create fvmap debugfs directory\n", __func__);
		return -ENOMEM;
	}

	if (!debugfs_create_file("sram", 0444, fvmap_debugfs_dir, NULL, &sram_debugfs_fops)) {
		pr_err("%s: failed to create sram debugfs file\n", __func__);
		debugfs_remove_recursive(fvmap_debugfs_dir);
		return -ENOMEM;
	}

	return 0;
}

int fvmap_init(void __iomem *sram_base)
{
	void __iomem *map_base;
	struct kobject *kobj;

	map_base = kzalloc(FVMAP_SIZE, GFP_KERNEL);

	fvmap_base = map_base;
	sram_fvmap_base = sram_base;
	pr_info("%s:fvmap initialize %pK\n", __func__, sram_base);
	fvmap_copy_from_sram(fvmap_base, sram_base);
	print_fvmap(NULL, 0, 0);

	kobj = kobject_create_and_add("fvmap", power_kobj);
	if (!kobj)
		pr_err("Fail to create fvmap kboject\n");

	if (sysfs_create_group(kobj, &fvmap_group))
		pr_err("Fail to create fvmap group\n");

	if (IS_ENABLED(CONFIG_VDD_AUTO_CAL))
		exynos_acpm_vdd_auto_calibration(1);

	fvmap_debugfs_init();

	return 0;
}


