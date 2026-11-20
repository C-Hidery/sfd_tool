/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 *
 * 结构体拷贝自 SPD Flash Tool 源码 Calibration_Struct.h（注释已去除）。
 */
#pragma once

#include <cstdint>

#ifndef _CALIBRATION_STRUCT_H__
#define _CALIBRATION_STRUCT_H__


#ifndef uint8
typedef unsigned char uint8;
#endif

#ifndef uint16
typedef unsigned short uint16;
#endif

#ifndef uint32
typedef unsigned int uint32;
#endif

#ifndef int8
typedef char int8;
#endif

#ifndef int16
typedef short int16;
#endif

namespace calibration_struct_va
{
	const int NUM_TEMP_BANDS = 5;
	const int NUM_RAMP_RANGES = 16;        
	const int NUM_TX_LEVEL = 16;          
	const int NUM_RAMP_POINTS = 20;
	const int NUM_GSM_ARFCN_BANDS = 6;
	const int NUM_DCS_ARFCN_BANDS = 8;
	const int NUM_PCS_ARFCN_BANDS = 7;
	const int NUM_GSM850_ARFCN_BANDS = 6;
	const int MAX_COMPENSATE_POINT = 75;

	typedef struct 
	{
		int16 stage0[NUM_RAMP_POINTS];
		int16 stage1[NUM_RAMP_POINTS];
		int16 stage2[NUM_RAMP_POINTS];
		int16 stage3[NUM_RAMP_POINTS];
		int16 stage4[NUM_RAMP_POINTS];
		int16 stage5[NUM_RAMP_POINTS];
		int16 stage6[NUM_RAMP_POINTS];
		int16 stage7[NUM_RAMP_POINTS];
		int16 stage8[NUM_RAMP_POINTS];
		int16 stage9[NUM_RAMP_POINTS];
		int16 stage10[NUM_RAMP_POINTS];
		int16 stage11[NUM_RAMP_POINTS];
		int16 stage12[NUM_RAMP_POINTS];
		int16 stage13[NUM_RAMP_POINTS];
		int16 stage14[NUM_RAMP_POINTS];
		int16 stage15[NUM_RAMP_POINTS];

	}RF_ramp_table_T;

	typedef struct
	{
		
		
		uint16 slope_length ;
		uint16 cdac ;
		uint16 cafc ;
		uint16 slope_struct[3][17] ;
	    
	}RF_aero2_AFC_T ;

	typedef struct
	{
		
		uint16 is_using_si4134;        
		
		
		uint16 si4134_coarse_13m;      
	    
		uint16 afc_center;
		uint16 afc_slope_per_6bits;
	    
	}RF_aero1_AFC_T ; 
	typedef struct
	{
		
		RF_aero1_AFC_T rf_aero1_afc_struct;
		
		int16  osci_temp_comp_in_dac[ NUM_TEMP_BANDS ];     
		
		
		
		
		
		RF_aero2_AFC_T rf_aero2_afc_struct;                                                        
	} RF_param_common_DSP_use_T;

	typedef struct
	{
		



		int16 rf_ramp_param_constant_up[NUM_RAMP_RANGES];
		int16 rf_ramp_param_constant_down[NUM_RAMP_RANGES];
		int16 rf_ramp_up_param_num;
		int16 rf_ramp_down_param_num;

		
		int16 reserved[64 - 2*NUM_RAMP_RANGES - 1 - 1];

		int16 TX_compensate_boundary_arfcn_index[4];
		int16 RF_ramppwr_step_factor[5][NUM_TX_LEVEL];
		int16 RF_ramp_delta_timing[NUM_TX_LEVEL];
		int16 RF_ramp_PA_power_on_duration[NUM_TX_LEVEL];
	
	
		int16 reserved1[9];
		int16 temperature_RX_compensate_boundary_ADC[4];
		int16 temperature_RX_compensate_value[5];
		int16 temperature_TX_compensate_boundary_ADC[4];
		int16 temperature_TX_rampwr_step_factor[5];
		int16  ramp_switch_level;
		uint16 afc_alg_bias;
		uint16 agc_ctrl_word[91];   
										
	    
		int16  max_rf_gain_index;
		int16  reserve_for_future[20];
	    
		int16  RX_compensate_value[MAX_COMPENSATE_POINT];
		RF_ramp_table_T RF_ramp_table;
	    
	} RF_param_band_DSP_use_T;

	typedef struct
	{
		RF_param_common_DSP_use_T   rf_common_param_dsp_use;
		RF_param_band_DSP_use_T     rf_gsm_param_dsp_use;
		RF_param_band_DSP_use_T     rf_dcs_param_dsp_use;
		RF_param_band_DSP_use_T     rf_pcs_param_dsp_use;
		RF_param_band_DSP_use_T     rf_gsm850_param_dsp_use;	
	}RF_param_DSP_use_T;

	typedef struct
	{
		int8    rx_whole_band_comp_gsm[NUM_GSM_ARFCN_BANDS];
		int8    rx_whole_band_comp_dcs[NUM_DCS_ARFCN_BANDS];
		int8    rx_whole_band_comp_pcs[NUM_PCS_ARFCN_BANDS];
		int8    rx_whole_band_comp_gsm850[NUM_GSM850_ARFCN_BANDS];        
		int8    pa_GSM_temp_comp[ NUM_TEMP_BANDS];
	    
		int8    pa_DCS_temp_comp[ NUM_TEMP_BANDS];
	    
		int8    pa_PCS_temp_comp[ NUM_TEMP_BANDS];
		int8    pa_GSM850_temp_comp[ NUM_TEMP_BANDS];
	    
		
		
		
		int8    rxlev_GSM_temp_comp[ NUM_TEMP_BANDS];
	    
		int8    rxlev_DCS_temp_comp[ NUM_TEMP_BANDS];
	    
		int8    rxlev_PCS_temp_comp[ NUM_TEMP_BANDS];
		int8    rxlev_GSM850_temp_comp[ NUM_TEMP_BANDS];
	    
		
		int8    pa_GSM_vcc_comp_threshold;      
		int8    pa_GSM_vcc_comp_step;           
		int8    pa_DCS_vcc_comp_threshold;      
		int8    pa_DCS_vcc_comp_step;           
		int8    pa_PCS_vcc_comp_threshold;      
		int8    pa_PCS_vcc_comp_step;           
		int8    pa_GSM850_vcc_comp_threshold;      
		int8    pa_GSM850_vcc_comp_step;           
	    
	} RF_param_MCU_use_T;

	typedef struct
	{
		
		
		
		
		uint32 batt_mV_per_4bits;   
	    
		
		
		
	    
		uint16  clock_32k_cal_interval; 
		uint16  clock_32k_cal_duration; 
	}Misc_cal_MCU_use_T;

	typedef struct
	{
		uint32    adc[2];           
		uint32    battery[2];       
		uint32    reserved[8];      
	}ADC_T;

	typedef struct
	{
		uint16              calibration_param_version;
		RF_param_DSP_use_T  rf_param_dsp_use;
		RF_param_MCU_use_T  rf_param_mcu_use;
		Misc_cal_MCU_use_T  misc_cal_mcu_use;
		int16                   mic_gain;        
		int16                   auxi_mic_gain;   
		uint16                  PM_version;      
	    
		
		uint16                  software_version;   
		uint16                  station_num;
		uint8                   operate_code[ 16 ];
		uint32                  date;
	    
		ADC_T                   adc;             
		
	    
	}calibration_param_T;
}

namespace calibration_struct_vb
{
	const int NUM_TEMP_BANDS = 5;
	const int NUM_RAMP_RANGES = 16;        
	const int NUM_TX_LEVEL = 16;          
	const int NUM_RAMP_POINTS = 20;
	const int NUM_GSM_ARFCN_BANDS = 6;
	const int NUM_DCS_ARFCN_BANDS = 8;
	const int NUM_PCS_ARFCN_BANDS = 7;
	const int NUM_GSM850_ARFCN_BANDS = 6;
	const int MAX_COMPENSATE_POINT = 75;

	typedef struct 
	{
		int16 stage0[NUM_RAMP_POINTS];
		int16 stage1[NUM_RAMP_POINTS];
		int16 stage2[NUM_RAMP_POINTS];
		int16 stage3[NUM_RAMP_POINTS];
		int16 stage4[NUM_RAMP_POINTS];
		int16 stage5[NUM_RAMP_POINTS];
		int16 stage6[NUM_RAMP_POINTS];
		int16 stage7[NUM_RAMP_POINTS];
		int16 stage8[NUM_RAMP_POINTS];
		int16 stage9[NUM_RAMP_POINTS];
		int16 stage10[NUM_RAMP_POINTS];
		int16 stage11[NUM_RAMP_POINTS];
		int16 stage12[NUM_RAMP_POINTS];
		int16 stage13[NUM_RAMP_POINTS];
		int16 stage14[NUM_RAMP_POINTS];
		int16 stage15[NUM_RAMP_POINTS];

	}RF_ramp_table_T;

	typedef struct
	{
		
		
		uint16 slope_length ;
		uint16 cdac ;
		uint16 cafc ;
		uint16 slope_struct[3][17] ;
	    
	}RF_aero2_AFC_T ;

	typedef struct
	{
		
		uint16 is_using_si4134;        
		
		
		uint16 si4134_coarse_13m;      
	    
		uint16 afc_center;
		uint16 afc_slope_per_6bits;
	    
	}RF_aero1_AFC_T ; 
	typedef struct
	{
		
		RF_aero1_AFC_T rf_aero1_afc_struct;
		
		int16  osci_temp_comp_in_dac[ NUM_TEMP_BANDS ];     
		
		
		
		
		
		RF_aero2_AFC_T rf_aero2_afc_struct;                                                        
	} RF_param_common_DSP_use_T;

	typedef struct
	{
		



		int16 rf_ramp_param_constant_up[NUM_RAMP_RANGES];
		int16 rf_ramp_param_constant_down[NUM_RAMP_RANGES];
		int16 rf_ramp_up_param_num;
		int16 rf_ramp_down_param_num;

		
		int16 reserved[64 - 2*NUM_RAMP_RANGES - 1 - 1];

		int16 TX_compensate_boundary_arfcn_index[4];
		int16 RF_ramppwr_step_factor[5][NUM_TX_LEVEL];
		int16 RF_ramp_delta_timing[NUM_TX_LEVEL];
		int16 RF_ramp_PA_power_on_duration[NUM_TX_LEVEL];
	
	
		int16 reserved1[9];
		int16 temperature_RX_compensate_boundary_ADC[4];
		int16 temperature_RX_compensate_value[5];
		int16 temperature_TX_compensate_boundary_ADC[4];
		int16 temperature_TX_rampwr_step_factor[5];
		int16  ramp_switch_level;
		uint16 afc_alg_bias;
		uint16 agc_ctrl_word[91];   
										
	    
		int16  max_rf_gain_index;

		uint16 agc_ctrl_word_div[91];	
		int16  max_rf_gain_index_div;   

		int16  reserve_for_future[20];
	    
		int16  RX_compensate_value[MAX_COMPENSATE_POINT];
		int16  rssi_campensate_div[MAX_COMPENSATE_POINT];
		int16  reserved2;
		RF_ramp_table_T RF_ramp_table;
	    
	} RF_param_band_DSP_use_T;

	typedef struct
	{
		RF_param_common_DSP_use_T   rf_common_param_dsp_use;
		RF_param_band_DSP_use_T     rf_gsm_param_dsp_use;
		RF_param_band_DSP_use_T     rf_dcs_param_dsp_use;
		RF_param_band_DSP_use_T     rf_pcs_param_dsp_use;
		RF_param_band_DSP_use_T     rf_gsm850_param_dsp_use;	
	}RF_param_DSP_use_T;

	typedef struct
	{
		int8    rx_whole_band_comp_gsm[NUM_GSM_ARFCN_BANDS];
		int8    rx_whole_band_comp_dcs[NUM_DCS_ARFCN_BANDS];
		int8    rx_whole_band_comp_pcs[NUM_PCS_ARFCN_BANDS];
		int8    rx_whole_band_comp_gsm850[NUM_GSM850_ARFCN_BANDS];        
		int8    pa_GSM_temp_comp[ NUM_TEMP_BANDS];
	    
		int8    pa_DCS_temp_comp[ NUM_TEMP_BANDS];
	    
		int8    pa_PCS_temp_comp[ NUM_TEMP_BANDS];
		int8    pa_GSM850_temp_comp[ NUM_TEMP_BANDS];
	    
		
		
		
		int8    rxlev_GSM_temp_comp[ NUM_TEMP_BANDS];
	    
		int8    rxlev_DCS_temp_comp[ NUM_TEMP_BANDS];
	    
		int8    rxlev_PCS_temp_comp[ NUM_TEMP_BANDS];
		int8    rxlev_GSM850_temp_comp[ NUM_TEMP_BANDS];
	    
		
		int8    pa_GSM_vcc_comp_threshold;      
		int8    pa_GSM_vcc_comp_step;           
		int8    pa_DCS_vcc_comp_threshold;      
		int8    pa_DCS_vcc_comp_step;           
		int8    pa_PCS_vcc_comp_threshold;      
		int8    pa_PCS_vcc_comp_step;           
		int8    pa_GSM850_vcc_comp_threshold;      
		int8    pa_GSM850_vcc_comp_step;           
	    
	} RF_param_MCU_use_T;

	typedef struct
	{
		
		
		
		
		uint32 batt_mV_per_4bits;   
	    
		
		
		
	    
		uint16  clock_32k_cal_interval; 
		uint16  clock_32k_cal_duration; 
	}Misc_cal_MCU_use_T;

	typedef struct
	{
		uint32    adc[2];           
		uint32    battery[2];       
		uint32    reserved[8];      
	}ADC_T;

	typedef struct
	{
		uint16              calibration_param_version;
		RF_param_DSP_use_T  rf_param_dsp_use;
		RF_param_MCU_use_T  rf_param_mcu_use;
		Misc_cal_MCU_use_T  misc_cal_mcu_use;
		int16                   mic_gain;        
		int16                   auxi_mic_gain;   
		uint16                  PM_version;      
	    
		
		uint16                  software_version;   
		uint16                  station_num;
		uint8                   operate_code[ 16 ];
		uint32                  date;
	    
		ADC_T                   adc;             
		
	    
	}calibration_param_T;
}



namespace td_calibration_struct_v3
{

	const int AGC_GAIN_LEN_QS3200	= 1495;
	const int AGC_CTL_LEN_QS3200	= 115;
	const int APC_CTL_LEN_QS3200	= 1300;
	const int AGC_COMP_LEN_QS3200	= 61;
	const int APC_COMP_LEN_QS3200	= 613;

	typedef struct  
	{   
		   uint16   cali_version;
		   uint16   antenna_switch_pin_control;
		   uint16   Antenna_switch_truth_table[8];
		   uint16   rf_afc_dac;
		   uint16   rf_afc_slope;
		   uint16   agc_ctl_word[AGC_CTL_LEN_QS3200];
		   uint16   agc_gain_word[AGC_GAIN_LEN_QS3200]; 
		   uint16   AGC_compensation[AGC_COMP_LEN_QS3200];
		   uint16   tdpa_pin_control;
		   uint16   TDPA_APC_control_word_index[APC_CTL_LEN_QS3200]; 
		   uint16   APC_compensation[APC_COMP_LEN_QS3200]; 
		   uint16   rf_register_num;
		   uint32   rf_Register_Initial[50];
		   uint32   rf_action_tbl[281];
		   uint8    reserved[500];

	}td_calibration_T; 

}

namespace td_calibration_struct_v4
{

	const int AGC_GAIN_LEN_QS3200	= 2875;
	const int APC_CTL_LEN_QS3200	= 2500;
	typedef struct  
	{   
		   uint16   cali_version;
		   uint16   rf_afc_dac;
		   uint16   rf_afc_slope;
		   uint16   slope_length;
		   uint16   CDAC;
		   uint16   CAFC;
		   uint16   agc_gain_word[AGC_GAIN_LEN_QS3200]; 
		   uint16   TDPA_APC_control_word_index[APC_CTL_LEN_QS3200]; 
	}td_calibration_T; 

}



namespace lte_calibration_struct_v1
{
	









	
	





	
	
	typedef struct _LTE_NV_CALI_FAST_PLLLOCK
	{
		uint16 plllock_array[64];
	}LTE_NV_CALI_FAST_PLLLOCK;
	
	




	
	
	typedef struct _LTE_NV_CALI_AGC_GAIN_MAIN
	{
		uint16 agc_gain_main_24G_center_array[124];
		uint16 agc_gain_main_24G_channel_array[21+20];
		uint16 agc_gain_main_26G_center_array[124];
		uint16 agc_gain_main_26G_channel_array[11+20];
	}LTE_NV_CALI_AGC_GAIN_MAIN;
	
	
	typedef struct _LTE_NV_CALI_AGC_GAIN_DIV
	{
		uint16 agc_gain_div_24G_center_array[124];
		uint16 agc_gain_div_24G_channel_array[21+20];
		uint16 agc_gain_div_26G_center_array[124];
		uint16 agc_gain_div_26G_channel_array[11+20];
	}LTE_NV_CALI_AGC_GAIN_DIV;
	
	
	typedef struct _LTE_NV_CALI_AFC_CONTROL
	{
		uint16 AFC0;
		uint16 AFC_K;
	}LTE_NV_CALI_AFC_CONTROL;
	
	




	
	
	typedef struct _LTE_NV_CALI_APC_CONTROL_WORD_MAIN
	{
		uint16 apc_control_main_24G_array[22][80]; 
		uint16 apc_control_main_26G_array[12][80]; 
	}LTE_NV_CALI_APC_CONTROL_WORD_MAIN;
	
	
	typedef struct _LTE_NV_CALI_APC_CONTROL_WORD_DIV
	{
		uint16 apc_control_div_24G_array[22][80]; 
		uint16 apc_control_div_26G_array[12][80]; 
	}LTE_NV_CALI_APC_CONTROL_WORD_DIV;
	
	typedef struct _LTE_NV_CALI_PARAM_T
	{
		uint16                              CALI_PARAM_VERSION;
		uint16                              CALI_PARAM_FLAG;
		
		LTE_NV_CALI_FAST_PLLLOCK            FAST_PLLLOCK;
		LTE_NV_CALI_AGC_GAIN_MAIN           AGC_GAIN_MAIN;
		LTE_NV_CALI_AGC_GAIN_DIV            AGC_GAIN_DIV;
		LTE_NV_CALI_AFC_CONTROL             AFC_CONTROL;
		LTE_NV_CALI_APC_CONTROL_WORD_MAIN   APC_CONTROL_WORD_MAIN;
		LTE_NV_CALI_APC_CONTROL_WORD_DIV    APC_CONTROL_WORD_DIV;    
	}LTE_NV_CALI_PARAM_T;
	
	







	
}





#define NV_MAX_LANG_NUM      (100)
#define NV_LANG_SUPPORT_BIT  (0x1)
#define NV_LANG_DISABLE_BIT  (0x2)
#define NV_LANG_ENABLE_BIT   (0x4)
#define NV_LANG_DEFAULT_BIT  (0x8)

#define IS_LANG_SUPPORT(f)		((f) & (BYTE)NV_LANG_SUPPORT_BIT)
#define IS_LANG_CAN_DISABLE(f)  ((f) & (BYTE)NV_LANG_DISABLE_BIT)
#define IS_LANG_ENABLE(f)		((f) & (BYTE)NV_LANG_ENABLE_BIT)
#define IS_LANG_DEFAULT(f)		((f) & (BYTE)NV_LANG_DEFAULT_BIT)
#define MAKE_LANG_ENABLE(f)		((f) | (BYTE)NV_LANG_ENABLE_BIT)
#define MAKE_LANG_DISABLE(f)	((f) & (~((BYTE)NV_LANG_ENABLE_BIT)))
#define MAKE_LANG_DEFAULT(f)	((f) | (BYTE)NV_LANG_DEFAULT_BIT)
#define MAKE_LANG_NOTDEFAULT(f)	((f) & (~((BYTE)NV_LANG_DEFAULT_BIT)))

typedef struct _NV_MULTI_LANGUE_CFG
{
	uint8 flag[NV_MAX_LANG_NUM];
	uint8 reserved[12];
}NV_MULTI_LANGUE_CFG;



#endif 