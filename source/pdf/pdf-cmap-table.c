#include "mupdf/pdf.h"

#ifdef NOCJK
#define CJK_CMAPS 0
#endif

#ifndef CJK_CMAPS
#define CJK_CMAPS 1
#endif

#ifndef EXTRA_CMAPS
#define EXTRA_CMAPS 0
#endif
#ifndef UTF8_CMAPS
#define UTF8_CMAPS 0
#endif
#ifndef UTF32_CMAPS
#define UTF32_CMAPS 0
#endif

#if CJK_CMAPS

#include "gen_cmap_cjk.h"

struct table { const char *name; pdf_cmap *cmap; };

static const struct table table_cjk[] =
{
	{"83pv-RKSJ-H",&cmap_83pv_RKSJ_H},
	{"90ms-RKSJ-H",&cmap_90ms_RKSJ_H},
	{"90ms-RKSJ-V",&cmap_90ms_RKSJ_V},
	{"90msp-RKSJ-H",&cmap_90msp_RKSJ_H},
	{"90msp-RKSJ-V",&cmap_90msp_RKSJ_V},
	{"90pv-RKSJ-H",&cmap_90pv_RKSJ_H},
	{"Add-RKSJ-H",&cmap_Add_RKSJ_H},
	{"Add-RKSJ-V",&cmap_Add_RKSJ_V},
	{"Adobe-CNS1-UCS2",&cmap_Adobe_CNS1_UCS2},
	{"Adobe-GB1-UCS2",&cmap_Adobe_GB1_UCS2},
	{"Adobe-Japan1-UCS2",&cmap_Adobe_Japan1_UCS2},
	{"Adobe-Korea1-UCS2",&cmap_Adobe_Korea1_UCS2},
	{"B5pc-H",&cmap_B5pc_H},
	{"B5pc-V",&cmap_B5pc_V},
	{"CNS-EUC-H",&cmap_CNS_EUC_H},
	{"CNS-EUC-V",&cmap_CNS_EUC_V},
	{"ETen-B5-H",&cmap_ETen_B5_H},
	{"ETen-B5-V",&cmap_ETen_B5_V},
	{"ETenms-B5-H",&cmap_ETenms_B5_H},
	{"ETenms-B5-V",&cmap_ETenms_B5_V},
	{"EUC-H",&cmap_EUC_H},
	{"EUC-V",&cmap_EUC_V},
	{"Ext-RKSJ-H",&cmap_Ext_RKSJ_H},
	{"Ext-RKSJ-V",&cmap_Ext_RKSJ_V},
	{"GB-EUC-H",&cmap_GB_EUC_H},
	{"GB-EUC-V",&cmap_GB_EUC_V},
	{"GBK-EUC-H",&cmap_GBK_EUC_H},
	{"GBK-EUC-V",&cmap_GBK_EUC_V},
	{"GBK2K-H",&cmap_GBK2K_H},
	{"GBK2K-V",&cmap_GBK2K_V},
	{"GBKp-EUC-H",&cmap_GBKp_EUC_H},
	{"GBKp-EUC-V",&cmap_GBKp_EUC_V},
	{"GBpc-EUC-H",&cmap_GBpc_EUC_H},
	{"GBpc-EUC-V",&cmap_GBpc_EUC_V},
	{"H",&cmap_H},
	{"HKscs-B5-H",&cmap_HKscs_B5_H},
	{"HKscs-B5-V",&cmap_HKscs_B5_V},
	{"KSC-EUC-H",&cmap_KSC_EUC_H},
	{"KSC-EUC-V",&cmap_KSC_EUC_V},
	{"KSCms-UHC-H",&cmap_KSCms_UHC_H},
	{"KSCms-UHC-HW-H",&cmap_KSCms_UHC_HW_H},
	{"KSCms-UHC-HW-V",&cmap_KSCms_UHC_HW_V},
	{"KSCms-UHC-V",&cmap_KSCms_UHC_V},
	{"KSCpc-EUC-H",&cmap_KSCpc_EUC_H},
	{"UniCNS-UCS2-H",&cmap_UniCNS_UCS2_H},
	{"UniCNS-UCS2-V",&cmap_UniCNS_UCS2_V},
	{"UniCNS-UTF16-H",&cmap_UniCNS_UTF16_H},
	{"UniCNS-UTF16-V",&cmap_UniCNS_UTF16_V},
	{"UniCNS-X",&cmap_UniCNS_X},
	{"UniGB-UCS2-H",&cmap_UniGB_UCS2_H},
	{"UniGB-UCS2-V",&cmap_UniGB_UCS2_V},
	{"UniGB-UTF16-H",&cmap_UniGB_UTF16_H},
	{"UniGB-UTF16-V",&cmap_UniGB_UTF16_V},
	{"UniGB-X",&cmap_UniGB_X},
	{"UniJIS-UCS2-H",&cmap_UniJIS_UCS2_H},
	{"UniJIS-UCS2-HW-H",&cmap_UniJIS_UCS2_HW_H},
	{"UniJIS-UCS2-HW-V",&cmap_UniJIS_UCS2_HW_V},
	{"UniJIS-UCS2-V",&cmap_UniJIS_UCS2_V},
	{"UniJIS-UTF16-H",&cmap_UniJIS_UTF16_H},
	{"UniJIS-UTF16-V",&cmap_UniJIS_UTF16_V},
	{"UniJIS-X",&cmap_UniJIS_X},
	{"UniJIS-X16",&cmap_UniJIS_X16},
	{"UniKS-UCS2-H",&cmap_UniKS_UCS2_H},
	{"UniKS-UCS2-V",&cmap_UniKS_UCS2_V},
	{"UniKS-UTF16-H",&cmap_UniKS_UTF16_H},
	{"UniKS-UTF16-V",&cmap_UniKS_UTF16_V},
	{"UniKS-X",&cmap_UniKS_X},
	{"V",&cmap_V},
};

#if EXTRA_CMAPS
#include "gen_cmap_extra.h"
static const struct table table_extra[] =
{
	{"78-EUC-H",&cmap_78_EUC_H},
	{"78-EUC-V",&cmap_78_EUC_V},
	{"78-H",&cmap_78_H},
	{"78-RKSJ-H",&cmap_78_RKSJ_H},
	{"78-RKSJ-V",&cmap_78_RKSJ_V},
	{"78-V",&cmap_78_V},
	{"78ms-RKSJ-H",&cmap_78ms_RKSJ_H},
	{"78ms-RKSJ-V",&cmap_78ms_RKSJ_V},
	{"90pv-RKSJ-V",&cmap_90pv_RKSJ_V},
	{"Add-H",&cmap_Add_H},
	{"Add-V",&cmap_Add_V},
	{"Adobe-CNS1-0",&cmap_Adobe_CNS1_0},
	{"Adobe-CNS1-1",&cmap_Adobe_CNS1_1},
	{"Adobe-CNS1-2",&cmap_Adobe_CNS1_2},
	{"Adobe-CNS1-3",&cmap_Adobe_CNS1_3},
	{"Adobe-CNS1-4",&cmap_Adobe_CNS1_4},
	{"Adobe-CNS1-5",&cmap_Adobe_CNS1_5},
	{"Adobe-CNS1-6",&cmap_Adobe_CNS1_6},
	{"Adobe-GB1-0",&cmap_Adobe_GB1_0},
	{"Adobe-GB1-1",&cmap_Adobe_GB1_1},
	{"Adobe-GB1-2",&cmap_Adobe_GB1_2},
	{"Adobe-GB1-3",&cmap_Adobe_GB1_3},
	{"Adobe-GB1-4",&cmap_Adobe_GB1_4},
	{"Adobe-GB1-5",&cmap_Adobe_GB1_5},
	{"Adobe-Japan1-0",&cmap_Adobe_Japan1_0},
	{"Adobe-Japan1-1",&cmap_Adobe_Japan1_1},
	{"Adobe-Japan1-2",&cmap_Adobe_Japan1_2},
	{"Adobe-Japan1-3",&cmap_Adobe_Japan1_3},
	{"Adobe-Japan1-4",&cmap_Adobe_Japan1_4},
	{"Adobe-Japan1-5",&cmap_Adobe_Japan1_5},
	{"Adobe-Japan1-6",&cmap_Adobe_Japan1_6},
	{"Adobe-Korea1-0",&cmap_Adobe_Korea1_0},
	{"Adobe-Korea1-1",&cmap_Adobe_Korea1_1},
	{"Adobe-Korea1-2",&cmap_Adobe_Korea1_2},
	{"B5-H",&cmap_B5_H},
	{"B5-V",&cmap_B5_V},
	{"CNS1-H",&cmap_CNS1_H},
	{"CNS1-V",&cmap_CNS1_V},
	{"CNS2-H",&cmap_CNS2_H},
	{"CNS2-V",&cmap_CNS2_V},
	{"ETHK-B5-H",&cmap_ETHK_B5_H},
	{"ETHK-B5-V",&cmap_ETHK_B5_V},
	{"Ext-H",&cmap_Ext_H},
	{"Ext-V",&cmap_Ext_V},
	{"GB-H",&cmap_GB_H},
	{"GB-V",&cmap_GB_V},
	{"GBT-EUC-H",&cmap_GBT_EUC_H},
	{"GBT-EUC-V",&cmap_GBT_EUC_V},
	{"GBT-H",&cmap_GBT_H},
	{"GBT-V",&cmap_GBT_V},
	{"GBTpc-EUC-H",&cmap_GBTpc_EUC_H},
	{"GBTpc-EUC-V",&cmap_GBTpc_EUC_V},
	{"HKdla-B5-H",&cmap_HKdla_B5_H},
	{"HKdla-B5-V",&cmap_HKdla_B5_V},
	{"HKdlb-B5-H",&cmap_HKdlb_B5_H},
	{"HKdlb-B5-V",&cmap_HKdlb_B5_V},
	{"HKgccs-B5-H",&cmap_HKgccs_B5_H},
	{"HKgccs-B5-V",&cmap_HKgccs_B5_V},
	{"HKm314-B5-H",&cmap_HKm314_B5_H},
	{"HKm314-B5-V",&cmap_HKm314_B5_V},
	{"HKm471-B5-H",&cmap_HKm471_B5_H},
	{"HKm471-B5-V",&cmap_HKm471_B5_V},
	{"Hankaku",&cmap_Hankaku},
	{"Hiragana",&cmap_Hiragana},
	{"KSC-H",&cmap_KSC_H},
	{"KSC-Johab-H",&cmap_KSC_Johab_H},
	{"KSC-Johab-V",&cmap_KSC_Johab_V},
	{"KSC-V",&cmap_KSC_V},
	{"KSCpc-EUC-V",&cmap_KSCpc_EUC_V},
	{"Katakana",&cmap_Katakana},
	{"NWP-H",&cmap_NWP_H},
	{"NWP-V",&cmap_NWP_V},
	{"RKSJ-H",&cmap_RKSJ_H},
	{"RKSJ-V",&cmap_RKSJ_V},
	{"Roman",&cmap_Roman},
	{"UniJIS2004-UTF16-H",&cmap_UniJIS2004_UTF16_H},
	{"UniJIS2004-UTF16-V",&cmap_UniJIS2004_UTF16_V},
	{"UniJISPro-UCS2-HW-V",&cmap_UniJISPro_UCS2_HW_V},
	{"UniJISPro-UCS2-V",&cmap_UniJISPro_UCS2_V},
	{"WP-Symbol",&cmap_WP_Symbol},
};
#endif

#if UTF8_CMAPS
#include "gen_cmap_utf8.h"
static const struct table table_utf8[] =
{
	{"UniCNS-UTF8-H",&cmap_UniCNS_UTF8_H},
	{"UniCNS-UTF8-V",&cmap_UniCNS_UTF8_V},
	{"UniGB-UTF8-H",&cmap_UniGB_UTF8_H},
	{"UniGB-UTF8-V",&cmap_UniGB_UTF8_V},
	{"UniJIS-UTF8-H",&cmap_UniJIS_UTF8_H},
	{"UniJIS-UTF8-V",&cmap_UniJIS_UTF8_V},
	{"UniJIS-X8",&cmap_UniJIS_X8},
	{"UniJIS2004-UTF8-H",&cmap_UniJIS2004_UTF8_H},
	{"UniJIS2004-UTF8-V",&cmap_UniJIS2004_UTF8_V},
	{"UniJISPro-UTF8-V",&cmap_UniJISPro_UTF8_V},
	{"UniKS-UTF8-H",&cmap_UniKS_UTF8_H},
	{"UniKS-UTF8-V",&cmap_UniKS_UTF8_V},
};
#endif

#if UTF32_CMAPS
#include "gen_cmap_utf32.h"
static const struct table table_utf32[] =
{
	{"UniCNS-UTF32-H",&cmap_UniCNS_UTF32_H},
	{"UniCNS-UTF32-V",&cmap_UniCNS_UTF32_V},
	{"UniGB-UTF32-H",&cmap_UniGB_UTF32_H},
	{"UniGB-UTF32-V",&cmap_UniGB_UTF32_V},
	{"UniJIS-UTF32-H",&cmap_UniJIS_UTF32_H},
	{"UniJIS-UTF32-V",&cmap_UniJIS_UTF32_V},
	{"UniJIS-X32",&cmap_UniJIS_X32},
	{"UniJIS2004-UTF32-H",&cmap_UniJIS2004_UTF32_H},
	{"UniJIS2004-UTF32-V",&cmap_UniJIS2004_UTF32_V},
	{"UniJISX0213-UTF32-H",&cmap_UniJISX0213_UTF32_H},
	{"UniJISX0213-UTF32-V",&cmap_UniJISX0213_UTF32_V},
	{"UniJISX02132004-UTF32-H",&cmap_UniJISX02132004_UTF32_H},
	{"UniJISX02132004-UTF32-V",&cmap_UniJISX02132004_UTF32_V},
	{"UniKS-UTF32-H",&cmap_UniKS_UTF32_H},
	{"UniKS-UTF32-V",&cmap_UniKS_UTF32_V},
};
#endif

static pdf_cmap *
pdf_load_builtin_cmap_imp(const struct table *table, int r, const char *name)
{
	int l = 0;
	while (l <= r)
	{
		int m = (l + r) >> 1;
		int c = strcmp(name, table[m].name);
		if (c < 0)
			r = m - 1;
		else if (c > 0)
			l = m + 1;
		else
			return table[m].cmap;
	}
	return NULL;
}

pdf_cmap *
pdf_load_builtin_cmap(fz_context *ctx, const char *name)
{
	pdf_cmap *cmap = NULL;
	if (!cmap) cmap = pdf_load_builtin_cmap_imp(table_cjk, nelem(table_cjk)-1, name);
#if EXTRA_CMAPS
	if (!cmap) cmap = pdf_load_builtin_cmap_imp(table_extra, nelem(table_extra)-1, name);
#endif
#if UTF8_CMAPS
	if (!cmap) cmap = pdf_load_builtin_cmap_imp(table_utf8, nelem(table_utf8)-1, name);
#endif
#if UTF32_CMAPS
	if (!cmap) cmap = pdf_load_builtin_cmap_imp(table_utf32, nelem(table_utf32)-1, name);
#endif
	return cmap;
}

#else

pdf_cmap *
pdf_load_builtin_cmap(fz_context *ctx, const char *name)
{
	return NULL;
}

#endif
