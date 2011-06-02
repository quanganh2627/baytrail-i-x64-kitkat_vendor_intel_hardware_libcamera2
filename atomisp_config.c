#include <stdio.h>
#include <utils/Log.h>
#include <string.h>
#include <stdlib.h>
#include "atomisp_config.h"
#include "atomisp_features.h"
#include <linux/videodev2.h>
#include <linux/atomisp.h>

#define CFG_PATH "/system/etc/atomisp/atomisp.cfg"
#define LINE_BUF_SIZE	64

struct ParamList {
	unsigned int index;
	unsigned int value;
};

enum ParamIndex {
	SWITCH,
	MACC,
	SC,
	GDC,
	IE,
	GAMMA,
	BPC,
	FPN,
	BLC,
	EE,
	NR,
	XNR,
	BAYERDS,
	ZOOM,
	MF,
	ME,
	MWB,
	ISO,
	DIS,
	DVS,
	FCC,
	REDEYE,
	NUM_OF_CFG,
};


/* For General Param */
enum ParamValueIndex_General {
	FUNC_DEFAULT,
	FUNC_ON,
	FUNC_OFF,
	NUM_OF_GENERAL,
};


/* For Macc Param */
enum {
	MACC_NONE,
	MACC_GRASSGREEN,
	MACC_SKYBLUE,
	MACC_SKIN,
	NUM_OF_MACC,
}ParamValueIndex_Macc;


/* For IE Param */
enum {
	IE_NONE,
	IE_MONO,
	IE_SEPIA,
	IE_NEGATIVE,
	NUM_OF_IE,
}ParamValueIndex_Ie;


static char *FunctionKey[] = {
	"switch", /* Total switch, to decide whether enable the config file */
	"macc", /* macc config */
	"sc",	/* shading correction config */
	"gdc",	/* gdc config */
	"ie",	/* image effect */
	"gamma", /* gamma/tone-curve setting */
	"bpc",	/* bad pixel correction */
	"fpn",
	"blc",	/* black level compensation */
	"ee",	/* edge enhancement */
	"nr",	/* noise reduction */
	"xnr",	/* xnr */
	"bayer_ds",
	"zoom",
	"focus_pos",
	"expo_pos",
	"wb_mode",
	"iso",
	"dis",
	"dvs",
	"fcc",
	"redeye",
};

static char *FunctionOption_Macc[] = {
	"none",
	"grass-green",
	"sky-blue",
	"skin",
};

static char *FunctionOption_Ie[] = {
	"none",
	"mono",
	"sepia",
	"negative",
};

static char *FunctionOption_General[] = {
	"default",
	"on",
	"off",
};

unsigned int default_function_value_list[] = {
	FUNC_OFF,	/* switch */
	MACC_NONE,	/* macc */
	FUNC_OFF,	/* sc */
	FUNC_OFF,	/* GDC */
	IE_NONE,	/* IE */
	FUNC_OFF,	/* GAMMA */
	FUNC_OFF,	/* BPC */
	FUNC_OFF,	/* FPN */
	FUNC_OFF,	/* BLC */
	FUNC_OFF,	/* EE */
	FUNC_OFF,	/* NR */
	FUNC_OFF,	/* XNR */
	FUNC_OFF,	/* BAY_DS */
	0,	/* ZOOM */
	0,	/* FOCUS_POS */
	0,	/* EXPO_POS */
	0,	/* WB_MODE */
	0,	/* ISO */
	FUNC_OFF,	/* DIS */
	FUNC_OFF,	/* DVS */
	FUNC_OFF,	/* FCC */
	FUNC_OFF,	/* REDEYE */
};

static int find_cfg_index(char *in)
{
	int i;

	for(i = 0; i < NUM_OF_CFG; i++) {
		if(!memcmp(in, FunctionKey[i], strlen(FunctionKey[i])))
			return i;
	}

	return -1;
}

static int analyze_cfg_value(unsigned int index, char *value)
{
	int i;

	switch (index) {
		case MACC:
			for (i = 0; i < NUM_OF_MACC; i++) {
				if(!memcmp(value, FunctionOption_Macc[i],
					   strlen(FunctionOption_Macc[i]))) {
					default_function_value_list[index] = i;
					return 0;
				}
			}
			return -1;
		case IE:
			for (i = 0; i < NUM_OF_IE; i++) {
				if(!memcmp(value, FunctionOption_Ie[i],
					   strlen(FunctionOption_Ie[i]))) {
					default_function_value_list[index] = i;
					return 0;
				}
			}
			return -1;
		case ZOOM:
		case MF:
		case ME:
		case MWB:
			default_function_value_list[index] = atoi(value);
			return 0;
		default:
			for (i = 0; i < NUM_OF_GENERAL; i++) {
				if(!memcmp(value, FunctionOption_General[i],
					strlen(FunctionOption_General[i]))) {
					default_function_value_list[index] = i;
					return 0;
				}
			}
			return -1;
	}
}

int atomisp_parse_cfg_file()
{
	char line[LINE_BUF_SIZE];
	char *line_name;
	char *line_value;
	int param_index;
	int res;
	int err = 0;

	FILE *fp;

	fp = fopen(CFG_PATH, "r");
	if (!fp) {
		LOGE("Error open file:%s\n", CFG_PATH);
		return -1;
	}
	/* anaylize file item */
	while(fgets(line, LINE_BUF_SIZE, fp)) {
		line_name = line;
		line_value = strchr(line, '=') + 1;
		param_index = find_cfg_index(line_name);
		if (param_index < 0) {
			LOGE("Error index in line: %s.\n", line);
			err = -1;
			continue;
		}

		res = analyze_cfg_value(param_index, line_value);
		if (res < 0) {
			LOGE("Error value in line: %s.\n", line);
			err = -1;
			continue;
		}
	}

	fclose(fp);

	return err;
}

int atomisp_set_cfg(int fd)
{
	int err = 0;
	int i;
	unsigned int value;

	if (default_function_value_list[SWITCH] == FUNC_OFF) {
		LOGD("Does not using the configuration file.\n");
		return 0;
	}

	for (i = 1; i < NUM_OF_CFG; i++) {
		value = default_function_value_list[i];
		switch (i) {
			case MACC:
				switch (value) {
				case MACC_GRASSGREEN:
					//Fix Me! Added set macc table
					err |= cam_driver_set_tone_mode(fd,
						V4L2_COLORFX_GRASS_GREEN);
				       break;
				case MACC_SKYBLUE:
					//Fix Me! Added set macc table
					err |= cam_driver_set_tone_mode(fd,
						V4L2_COLORFX_SKY_BLUE);
					break;
			 	case MACC_SKIN:
					//Fix Me! Added set macc table
					err |= cam_driver_set_tone_mode(fd,
					V4L2_COLORFX_SKIN_WHITEN);
					break;
				case MACC_NONE:
					err |= cam_driver_set_tone_mode(fd,
						V4L2_COLORFX_NONE);
					break;
				}
				LOGD("macc:%s.\n", FunctionOption_Macc[value]);
				break;
			case SC:
				LOGD("sc:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= cam_driver_set_sc(fd, value);
				break;
			case IE:
				LOGD("ie:%s.\n", FunctionOption_Ie[value]);
				switch (value) {
				case IE_MONO:
					err |= cam_driver_set_tone_mode(fd,
						V4L2_COLORFX_BW);
				       break;
				case IE_SEPIA:
					err |= cam_driver_set_tone_mode(fd,
						V4L2_COLORFX_SEPIA);
					break;
			 	case IE_NEGATIVE:
					err |= cam_driver_set_tone_mode(fd,
						V4L2_COLORFX_NEGATIVE);
					break;
				}

				break;
			case GAMMA:
				//Fix Me! Add setting gamma table here
				LOGD("gamma:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= cam_driver_set_gamma_from_value(fd, DEFAULT_GAMMA_VALUE,
                                                        DEFAULT_CONTRAST,
					                                    DEFAULT_BRIGHTNESS,
					                                    !!DEFAULT_INV_GAMMA);
				break;
			case BPC:
				LOGD("bpc:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= cam_driver_set_bpd(fd, value);
				break;
			case FPN:
				LOGD("fpn:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= cam_driver_set_fpn(fd, value);
				break;
			case BLC:
				LOGD("blc:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= cam_driver_set_blc(fd, value);
				break;
			case EE:
				LOGD("ee:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= cam_driver_set_ee(fd, value);
				break;
			case NR:
				LOGD("nr:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF) {
					err |= cam_driver_set_bnr(fd, value);
					err |= cam_driver_set_ynr(fd, value);
				}
				break;
			case XNR:
				LOGD("xnr:%s.\n", FunctionOption_General[value]);
				if(value != FUNC_OFF)
					err |= cam_driver_set_xnr(fd, value);
				break;
			case BAYERDS:
				LOGD("bayer-ds:%s.\n", FunctionOption_General[value]);
				//Needed added new interface
				break;
			case ZOOM:
				LOGD("zoom:%d.\n", value);
				if(value != 0)
					err |= cam_driver_set_zoom(fd, value);
				break;
			case MF:
				LOGD("mf:%d.\n", value);
				if(value != 0)
					err |= cam_driver_set_focus_posi(fd, value);
				break;
			case ME:
				LOGD("me:%d.\n", value);
				if(value != 0)
					err |= cam_driver_set_exposure(fd, value);

				break;
			case MWB:
				LOGD("mwb:%d.\n", value);
				//Fix Me! Add 3A Lib interface here
				break;
			case ISO:
				LOGD("iso:%d.\n", value);
				//Fix Me! Add implementatino here
				break;
			case DIS:
				LOGD("dis:%s.\n", FunctionOption_General[value]);
				//Fix Me! Add setting DIS Interface
				break;
			case DVS:
				LOGD("dvs:%s.\n", FunctionOption_General[value]);
				if(value != 0)
					err |= cam_driver_set_dvs(fd, value);
				break;
			case REDEYE:
				LOGD("red-eye:%s.\n", FunctionOption_General[value]);
				//Fix Me! Add red-eye interface here
				break;
			default:
				err |= -1;
		}
	}

	return err;

}
