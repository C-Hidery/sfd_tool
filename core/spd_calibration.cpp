/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 *
 * 移植自 SPD Flash Tool Calibration.cpp：
 *   GSM_VerAToA / GSM_VerBToB / XTD_Ver3To3 / XTD_Ver4To4 / LTE_Ver1To1
 * 以及各自的 endian 转换函数。RD 的这些函数只在“新旧版本号一致”时被调用，
 * 作用是按结构体字段做端序转换后整体用旧值覆盖新值。
 */
#include "spd_calibration.h"

#include <cstring>

namespace spd_cali
{
namespace
{
constexpr std::size_t align4(std::size_t n) { return (n + 3) & ~static_cast<std::size_t>(3); }
}  // namespace

std::size_t cali_expected_size(std::uint16_t item_id, std::uint16_t version)
{
    if (item_id == 0x2)  // GSM
    {
        switch (version)
        {
        case 0xFF0A: return align4(sizeof(calibration_struct_va::calibration_param_T));
        case 0xFF0B: return align4(sizeof(calibration_struct_vb::calibration_param_T));
        case 0xFF0C: return 0;  // Ver C：直接整体拷贝，长度以 item 自身为准
        default: return 0;
        }
    }
    if (item_id == 0x516)  // TD
    {
        switch (version)
        {
        case 3: return align4(sizeof(td_calibration_struct_v3::td_calibration_T));
        case 4: return align4(sizeof(td_calibration_struct_v4::td_calibration_T));
        case 5: return 0;  // Ver 5：整体拷贝
        default: return 0;
        }
    }
    return 0;
}

void ConvSHORTBuf(uint16 *pBuf,int nNum)
{
	if(pBuf == NULL || nNum == 0)
	{
		return;
	}

	for( int i = 0; i < nNum; ++i, ++pBuf )
	{
		*pBuf = CONV_SHORT(*pBuf);
	}
}

void ConvDWORDBuf(uint32 *pBuf,int nNum)
{
	if(pBuf == NULL || nNum == 0)
	{
		return;
	}

	for( int i = 0; i < nNum; ++i, ++pBuf )
	{
		*pBuf = CONV_DWORD(*pBuf);
	}
}
                           

void GSMCaliEndianConvVerA(calibration_struct_va::calibration_param_T &para)
{














	
	para.calibration_param_version = CONV_SHORT(para.calibration_param_version);

	
	ConvSHORTBuf((uint16 *)&para.rf_param_dsp_use,sizeof(para.rf_param_dsp_use)/sizeof(uint16));

	

	
	para.misc_cal_mcu_use.batt_mV_per_4bits		= CONV_DWORD(para.misc_cal_mcu_use.batt_mV_per_4bits);
	para.misc_cal_mcu_use.clock_32k_cal_duration= CONV_SHORT(para.misc_cal_mcu_use.clock_32k_cal_duration);
	para.misc_cal_mcu_use.clock_32k_cal_interval= CONV_SHORT(para.misc_cal_mcu_use.clock_32k_cal_interval);

	para.mic_gain			= CONV_SHORT(para.mic_gain);
	para.auxi_mic_gain		= CONV_SHORT(para.auxi_mic_gain);
	para.PM_version			= CONV_SHORT(para.PM_version);
	para.software_version	= CONV_SHORT(para.software_version);
	para.station_num		= CONV_SHORT(para.station_num);
	para.date				= CONV_DWORD(para.date);

	
	ConvDWORDBuf((uint32 *)&para.adc,sizeof(para.adc)/sizeof(uint32));	
}

void GSMCaliEndianConvVerB(calibration_struct_vb::calibration_param_T &para)
{
	
	para.calibration_param_version = CONV_SHORT(para.calibration_param_version);

	
	ConvSHORTBuf((uint16 *)&para.rf_param_dsp_use,sizeof(para.rf_param_dsp_use)/sizeof(uint16));

	

	
	para.misc_cal_mcu_use.batt_mV_per_4bits		= CONV_DWORD(para.misc_cal_mcu_use.batt_mV_per_4bits);
	para.misc_cal_mcu_use.clock_32k_cal_duration= CONV_SHORT(para.misc_cal_mcu_use.clock_32k_cal_duration);
	para.misc_cal_mcu_use.clock_32k_cal_interval= CONV_SHORT(para.misc_cal_mcu_use.clock_32k_cal_interval);

	para.mic_gain			= CONV_SHORT(para.mic_gain);
	para.auxi_mic_gain		= CONV_SHORT(para.auxi_mic_gain);
	para.PM_version			= CONV_SHORT(para.PM_version);
	para.software_version	= CONV_SHORT(para.software_version);
	para.station_num		= CONV_SHORT(para.station_num);
	para.date				= CONV_DWORD(para.date);

	
	ConvDWORDBuf((uint32 *)&para.adc,sizeof(para.adc)/sizeof(uint32));	
}


void BTConvEndian(BT_CONFIG &para)
{
	para.xtal_dac = CONV_SHORT(para.xtal_dac); 
}

void XTDCaliEndianConvVer3(td_calibration_struct_v3::td_calibration_T &para)
{

















	ConvSHORTBuf((uint16 *)&para.cali_version, (sizeof(para)-sizeof(uint32)*(50+281) -500*sizeof(uint8))/sizeof(uint16));
	ConvDWORDBuf((uint32 *)&para.rf_Register_Initial, (sizeof(uint32)*(50+281))/sizeof(uint32));
}

void XTDCaliEndianConvVer4(td_calibration_struct_v4::td_calibration_T &para)
{










	ConvSHORTBuf((uint16 *)&para.cali_version, sizeof(para)/sizeof(uint16));
}

void LTECaliEndianConvVer1(lte_calibration_struct_v1::LTE_NV_CALI_PARAM_T &para)
{

	ConvSHORTBuf((uint16 *)&para.CALI_PARAM_VERSION, sizeof(para)/sizeof(uint16));
}


void gsm_cali_ver_a_to_a( LPBYTE pFileBuf,DWORD dwOffsetFile,BOOL bFileBigEndian,LPBYTE pModuBuf,DWORD dwOffsetModu,BOOL bModuBigEndian )
{
	int i=0;
    calibration_struct_va::calibration_param_T para_modu,para_file;
    memcpy( &para_modu,pModuBuf+dwOffsetModu,sizeof( para_modu ) );
    memcpy( &para_file,pFileBuf+dwOffsetFile,sizeof( para_file ) );

	if( bFileBigEndian != bModuBigEndian)
	{
		GSMCaliEndianConvVerA(para_modu);
	}
    
	if(kGsmCaliVaPolicy == 0 || kGsmCaliVaPolicy == 1 )
	{
		
		for( i=0;i<calibration_struct_va::NUM_TX_LEVEL;i++ )
		{
			para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_delta_timing[i] = para_file.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_delta_timing[i];
			para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_delta_timing[i] = para_file.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_delta_timing[i];
			para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_delta_timing[i] = para_file.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_delta_timing[i];
			para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_delta_timing[i] = para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_delta_timing[i];
		}

		if(kGsmCaliVaPolicy == 1) 
		{
			
			for( i=0;i<calibration_struct_va::NUM_TX_LEVEL;i++ )
			{
				para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_PA_power_on_duration[i] = para_file.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_PA_power_on_duration[i];
				para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_PA_power_on_duration[i] = para_file.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_PA_power_on_duration[i];
				para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_PA_power_on_duration[i] = para_file.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_PA_power_on_duration[i];
				para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_PA_power_on_duration[i] = para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_PA_power_on_duration[i];
			}
			
			memcpy(&para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_table,
				   &para_file.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_table,
				   sizeof(para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_table));
			
			memcpy(&para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_table,
				   &para_file.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_table,
				   sizeof(para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_table));

			memcpy(&para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_table,
				   &para_file.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_table,
				   sizeof(para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_table));

			memcpy(&para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_table,
				   &para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_table,
				   sizeof(para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_table));

			
			memcpy(&(para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.reserved[0]),
				   &(para_file.rf_param_dsp_use.rf_gsm_param_dsp_use.reserved[0]),
				   sizeof(para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.reserved));
			
			memcpy(&(para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.reserved[0]),
				   &(para_file.rf_param_dsp_use.rf_dcs_param_dsp_use.reserved[0]),
				   sizeof(para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.reserved));
			
			memcpy(&(para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.reserved[0]),
				   &(para_file.rf_param_dsp_use.rf_pcs_param_dsp_use.reserved[0]),
				   sizeof(para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.reserved));
			
			memcpy(&(para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.reserved[0]),
				   &(para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use.reserved[0]),
				   sizeof(para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.reserved));

		}

		memcpy( pFileBuf + dwOffsetFile,&para_modu,sizeof( para_modu ) );
	}
	else if( kGsmCaliVaPolicy == 2 )
	{
		
		memcpy(&para_file.adc,&para_modu.adc,sizeof(para_file.adc));

		
		memcpy(&para_file.rf_param_dsp_use.rf_common_param_dsp_use,
			   &para_modu.rf_param_dsp_use.rf_common_param_dsp_use,
	     sizeof(para_file.rf_param_dsp_use.rf_common_param_dsp_use));

		calibration_struct_va::RF_param_band_DSP_use_T *pFile = NULL;
		calibration_struct_va::RF_param_band_DSP_use_T *pModu = NULL;

		for(int i = 0; i< 4; i++)
		{	
			switch(i)
			{
			case 0:
				pFile = &para_file.rf_param_dsp_use.rf_gsm_param_dsp_use;
				pModu = &para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use;
				break;
			case 1:
				pFile = &para_file.rf_param_dsp_use.rf_dcs_param_dsp_use;
				pModu = &para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use;
				break;
			case 2:
				pFile = &para_file.rf_param_dsp_use.rf_pcs_param_dsp_use;
				pModu = &para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use;
			    break;
			case 3:
				pFile = &para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use;
				pModu = &para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use;
			    break;
			default:
			    break;
			}
			
			
			
			memcpy( pFile->agc_ctrl_word,
					pModu->agc_ctrl_word,
			 sizeof(pFile->agc_ctrl_word));

			
			memcpy( pFile->RX_compensate_value,
					pModu->RX_compensate_value,
			 sizeof(pFile->RX_compensate_value));

			
			memcpy(&pFile->max_rf_gain_index,
				   &pModu->max_rf_gain_index,
			 sizeof(pFile->max_rf_gain_index));	

			
			
			memcpy( pFile->rf_ramp_param_constant_up,
					pModu->rf_ramp_param_constant_up,
			 sizeof(pFile->rf_ramp_param_constant_up));

			
			memcpy( pFile->rf_ramp_param_constant_down,
					pModu->rf_ramp_param_constant_down,
			 sizeof(pFile->rf_ramp_param_constant_down));

			
			memcpy( pFile->RF_ramppwr_step_factor,
					pModu->RF_ramppwr_step_factor,
			 sizeof(pFile->RF_ramppwr_step_factor));

			
			memcpy( pFile->reserved1,
					pModu->reserved1,
			 sizeof(pFile->reserved1));
		}

		memcpy( pFileBuf + dwOffsetFile,&para_file,sizeof( para_file ) );
	}  
	else
	{
		memcpy( pFileBuf + dwOffsetFile,&para_modu,sizeof( para_modu ) );
	}
}

void gsm_cali_ver_b_to_b( LPBYTE pFileBuf,DWORD dwOffsetFile,BOOL bFileBigEndian,LPBYTE pModuBuf,DWORD dwOffsetModu,BOOL bModuBigEndian )
{
	int i=0;
	calibration_struct_vb::calibration_param_T para_modu,para_file;
	memcpy( &para_modu,pModuBuf+dwOffsetModu,sizeof( para_modu ) );
	memcpy( &para_file,pFileBuf+dwOffsetFile,sizeof( para_file ) );

	if( bFileBigEndian != bModuBigEndian)
	{
		GSMCaliEndianConvVerB(para_modu);
	}

	if(kGsmCaliVaPolicy == 0 || kGsmCaliVaPolicy == 1 )
	{
		
		for( i=0;i<calibration_struct_vb::NUM_TX_LEVEL;i++ )
		{
			para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_delta_timing[i] = para_file.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_delta_timing[i];
			para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_delta_timing[i] = para_file.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_delta_timing[i];
			para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_delta_timing[i] = para_file.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_delta_timing[i];
			para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_delta_timing[i] = para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_delta_timing[i];
		}

		if(kGsmCaliVaPolicy == 1) 
		{
			
			for( i=0;i<calibration_struct_vb::NUM_TX_LEVEL;i++ )
			{
				para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_PA_power_on_duration[i] = para_file.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_PA_power_on_duration[i];
				para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_PA_power_on_duration[i] = para_file.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_PA_power_on_duration[i];
				para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_PA_power_on_duration[i] = para_file.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_PA_power_on_duration[i];
				para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_PA_power_on_duration[i] = para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_PA_power_on_duration[i];
			}
			
			memcpy(&para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_table,
				&para_file.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_table,
				sizeof(para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.RF_ramp_table));

			memcpy(&para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_table,
				&para_file.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_table,
				sizeof(para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.RF_ramp_table));

			memcpy(&para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_table,
				&para_file.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_table,
				sizeof(para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.RF_ramp_table));

			memcpy(&para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_table,
				&para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_table,
				sizeof(para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.RF_ramp_table));

			
			memcpy(&(para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.reserved[0]),
				&(para_file.rf_param_dsp_use.rf_gsm_param_dsp_use.reserved[0]),
				sizeof(para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use.reserved));

			memcpy(&(para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.reserved[0]),
				&(para_file.rf_param_dsp_use.rf_dcs_param_dsp_use.reserved[0]),
				sizeof(para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use.reserved));

			memcpy(&(para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.reserved[0]),
				&(para_file.rf_param_dsp_use.rf_pcs_param_dsp_use.reserved[0]),
				sizeof(para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use.reserved));

			memcpy(&(para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.reserved[0]),
				&(para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use.reserved[0]),
				sizeof(para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use.reserved));

		}

		memcpy( pFileBuf + dwOffsetFile,&para_modu,sizeof( para_modu ) );
	}
	else if( kGsmCaliVaPolicy == 2 )
	{
		
		memcpy(&para_file.adc,&para_modu.adc,sizeof(para_file.adc));

		
		memcpy(&para_file.rf_param_dsp_use.rf_common_param_dsp_use,
			&para_modu.rf_param_dsp_use.rf_common_param_dsp_use,
			sizeof(para_file.rf_param_dsp_use.rf_common_param_dsp_use));

		calibration_struct_vb::RF_param_band_DSP_use_T *pFile = NULL;
		calibration_struct_vb::RF_param_band_DSP_use_T *pModu = NULL;

		for(int i = 0; i< 4; i++)
		{	
			switch(i)
			{
			case 0:
				pFile = &para_file.rf_param_dsp_use.rf_gsm_param_dsp_use;
				pModu = &para_modu.rf_param_dsp_use.rf_gsm_param_dsp_use;
				break;
			case 1:
				pFile = &para_file.rf_param_dsp_use.rf_dcs_param_dsp_use;
				pModu = &para_modu.rf_param_dsp_use.rf_dcs_param_dsp_use;
				break;
			case 2:
				pFile = &para_file.rf_param_dsp_use.rf_pcs_param_dsp_use;
				pModu = &para_modu.rf_param_dsp_use.rf_pcs_param_dsp_use;
				break;
			case 3:
				pFile = &para_file.rf_param_dsp_use.rf_gsm850_param_dsp_use;
				pModu = &para_modu.rf_param_dsp_use.rf_gsm850_param_dsp_use;
				break;
			default:
				break;
			}

			
			
			memcpy( pFile->agc_ctrl_word,
				pModu->agc_ctrl_word,
				sizeof(pFile->agc_ctrl_word));

			
			memcpy( pFile->RX_compensate_value,
				pModu->RX_compensate_value,
				sizeof(pFile->RX_compensate_value));

            
            memcpy( pFile->rssi_campensate_div,
                pModu->rssi_campensate_div,
                sizeof(pFile->rssi_campensate_div));

			
			memcpy(&pFile->max_rf_gain_index,
				&pModu->max_rf_gain_index,
				sizeof(pFile->max_rf_gain_index));	

            
            memcpy( pFile->agc_ctrl_word_div,
                pModu->agc_ctrl_word_div,
                sizeof(pFile->agc_ctrl_word_div));

            
            memcpy( &pFile->max_rf_gain_index_div,
                &pModu->max_rf_gain_index_div,
                sizeof(pFile->max_rf_gain_index_div));

			
			
			memcpy( pFile->rf_ramp_param_constant_up,
				pModu->rf_ramp_param_constant_up,
				sizeof(pFile->rf_ramp_param_constant_up));

			
			memcpy( pFile->rf_ramp_param_constant_down,
				pModu->rf_ramp_param_constant_down,
				sizeof(pFile->rf_ramp_param_constant_down));

			
			memcpy( pFile->RF_ramppwr_step_factor,
				pModu->RF_ramppwr_step_factor,
				sizeof(pFile->RF_ramppwr_step_factor));

			
			memcpy( pFile->reserved1,
				pModu->reserved1,
				sizeof(pFile->reserved1));
		}

		memcpy( pFileBuf + dwOffsetFile,&para_file,sizeof( para_file ) );
	}  
	else
	{
		memcpy( pFileBuf + dwOffsetFile,&para_modu,sizeof( para_modu ) );
	}
}

void xtd_cali_ver3_to3( LPBYTE pFileBuf,DWORD dwOffsetFile,BOOL bFileBigEndian,LPBYTE pModuBuf,DWORD dwOffsetModu,BOOL bModuBigEndian )
{
	
    td_calibration_struct_v3::td_calibration_T para_modu;
    td_calibration_struct_v3::td_calibration_T para_file;

    memcpy( &para_modu,pModuBuf+dwOffsetModu,sizeof( para_modu ) );
    memcpy( &para_file,pFileBuf+dwOffsetFile,sizeof( para_file ) );

	if( bFileBigEndian != bModuBigEndian)
	{
		XTDCaliEndianConvVer3(para_modu);
	}

	para_file.rf_afc_dac = para_modu.rf_afc_dac;
	para_file.rf_afc_slope = para_modu.rf_afc_slope;

    memcpy(&(para_file.agc_ctl_word[0]),&(para_modu.agc_ctl_word[0]),
		(BYTE*)&(para_modu.agc_ctl_word[td_calibration_struct_v3::AGC_CTL_LEN_QS3200 - 1])-(BYTE*)&(para_modu.agc_ctl_word[0])+sizeof(uint16));

    memcpy(&(para_file.agc_gain_word[0]),&(para_modu.agc_gain_word[0]),
		(BYTE*)&(para_modu.agc_gain_word[td_calibration_struct_v3::AGC_GAIN_LEN_QS3200-1])-(BYTE*)&(para_modu.agc_gain_word[0])+sizeof(uint16));

	memcpy(&(para_file.TDPA_APC_control_word_index[0]),&(para_modu.TDPA_APC_control_word_index[0]),
		(BYTE*)&(para_modu.TDPA_APC_control_word_index[td_calibration_struct_v3::APC_CTL_LEN_QS3200-1])-(BYTE*)&(para_modu.TDPA_APC_control_word_index[0])+sizeof(uint16));

	memcpy( pFileBuf + dwOffsetFile,&para_file,sizeof( para_file ) );
}

void xtd_cali_ver4_to4( LPBYTE pFileBuf,DWORD dwOffsetFile,BOOL bFileBigEndian,LPBYTE pModuBuf,DWORD dwOffsetModu,BOOL bModuBigEndian )
{
	
    td_calibration_struct_v4::td_calibration_T para_modu;
    td_calibration_struct_v4::td_calibration_T para_file;

    memcpy( &para_modu,pModuBuf+dwOffsetModu,sizeof( para_modu ) );
    memcpy( &para_file,pFileBuf+dwOffsetFile,sizeof( para_file ) );

	if( bFileBigEndian != bModuBigEndian)
	{
		XTDCaliEndianConvVer4(para_modu);
	}

	memcpy( pFileBuf + dwOffsetFile,&para_modu,sizeof( para_modu ) );
}


void lte_cali_ver1_to1( LPBYTE pFileBuf,DWORD dwOffsetFile,BOOL bFileBigEndian,LPBYTE pModuBuf,DWORD dwOffsetModu,BOOL bModuBigEndian )
{
	
    lte_calibration_struct_v1::LTE_NV_CALI_PARAM_T para_modu;
    lte_calibration_struct_v1::LTE_NV_CALI_PARAM_T para_file;
	
    memcpy( &para_modu,pModuBuf+dwOffsetModu,sizeof( para_modu ) );
    memcpy( &para_file,pFileBuf+dwOffsetFile,sizeof( para_file ) );
	
	if( bFileBigEndian != bModuBigEndian)
	{
		LTECaliEndianConvVer1(para_modu);
	}
	
	memcpy( pFileBuf + dwOffsetFile,&para_modu,sizeof( para_modu ) );
}
}  // namespace spd_cali
