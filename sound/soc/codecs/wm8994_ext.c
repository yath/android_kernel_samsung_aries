/*
 * wm8994_ext.c  --  WM8994 ALSA Soc Audio driver related extensions
 *
 *  Copyright (C) 2010/11 François SIMOND / twitter & XDA-developers @supercurio
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <sound/soc.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/version.h>
#include "wm8994_ext.h"

#ifndef MODULE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
#include "wm8994_samsung.h"
#else
#include "wm8994.h"
#endif
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
#include "../wm8994_samsung.h"
#else
#include "../wm8994.h"
#endif
#endif

#define SUBJECT "wm8994_ext.c"

#if 0
bool bypass_write_extension = false;

short unsigned int debug_log_level = LOG_VERBOSE;

#ifdef CONFIG_SND_WM8994_EXT_FM
bool fm_radio_headset_restore_bass = true;
bool fm_radio_headset_restore_highs = true;
bool fm_radio_headset_normalize_gain = true;
#endif

#ifdef CONFIG_SND_WM8994_EXT_RECORD_PRESETS
unsigned short recording_preset = 1;
unsigned short origin_recgain;
unsigned short origin_recgain_mixer;
#endif

#ifdef NEXUS_S
bool speaker_tuning = false;
#endif

// global active or kill switch
bool enable = false;
bool dac_osr128 = true;
bool adc_osr128 = false;
bool fll_tuning = true;
bool dac_direct = true;
bool mono_downmix = false;
// digital gain value in mili dB
int digital_gain = 0;
// 3D effect
bool stereo_expansion = false;
short unsigned int stereo_expansion_gain = 16;
bool headphone_eq = false;

#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
unsigned short hp_level[2] = { CONFIG_SND_WM8994_EXT_HP_LEVEL,
			       CONFIG_SND_WM8994_EXT_HP_LEVEL };;
#endif

#endif

// equalizer

short eq_bands[5] = { 3, 4, 4, 4, 3 };
char eq_band_coef_names[][2] = { "A", "B", "C", "PG" };

// keep here a pointer to the codec structure
struct snd_soc_codec *codec;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
#define DECLARE_WM8994(codec) struct wm8994_priv *wm8994 = codec->drvdata;
#else
#define DECLARE_WM8994(codec) struct wm8994_priv *wm8994 = codec->private_data;
#endif

bool debug_log(short unsigned int level)
{
    DECLARE_WM8994(codec);
	if (wm8994->ext.debug_log_level >= level)
		return true;

	return false;
}

#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
int hpvol(int channel)
{
	int vol;
    DECLARE_WM8994(codec);

	vol = wm8994->ext.hp_level[channel];

	if (is_path_media_or_fm_no_call_no_record()) {
		// negative digital gain compensation
		if (wm8994->ext.digital_gain < 0)
			vol = (vol - ((wm8994->ext.digital_gain / 100) + 5) / 10);

		if (vol > 62)
			return 62;
	}

	return vol;
}

void write_hpvol(unsigned short l, unsigned short r)
{
	unsigned short val;

	// we don't need the Volume Update flag when sending the first volume
	val = (WM8994_HPOUT1L_MUTE_N | l);
	val |= WM8994_HPOUT1L_ZC;
	wm8994_write(codec, WM8994_LEFT_OUTPUT_VOLUME, val);

	// this time we write the right volume plus the Volume Update flag.
	// This way, both volume are set at the same time
	val = (WM8994_HPOUT1_VU | WM8994_HPOUT1R_MUTE_N | r);
	val |= WM8994_HPOUT1L_ZC;
	wm8994_write(codec, WM8994_RIGHT_OUTPUT_VOLUME, val);
}

void update_hpvol(bool with_fade)
{
	unsigned short val;
	unsigned short i;
	short steps;
	unsigned short hp_level_old[2];
	unsigned short hp_level_registers[2] = { WM8994_LEFT_OUTPUT_VOLUME,
						 WM8994_RIGHT_OUTPUT_VOLUME };

	DECLARE_WM8994(codec);

	// don't affect headphone amplifier volume
	// when not on heapdhones or if call is active
	if (!is_path(HEADPHONES)
	    || (wm8994->codec_state & CALL_ACTIVE))
		return;

	wm8994->ext.bypass_write_extension = true;

	if (!with_fade) {
		write_hpvol(hpvol(0), hpvol(1));
		wm8994->ext.bypass_write_extension = false;
		return;
	}

	// read previous levels
	for (i = 0; i < 2; i++) {
		val = wm8994_read(codec, hp_level_registers[i]);
		val &= ~(WM8994_HPOUT1_VU_MASK);
		val &= ~(WM8994_HPOUT1L_ZC_MASK);
		val &= ~(WM8994_HPOUT1L_MUTE_N_MASK);
		hp_level_old[i] = val + (wm8994->ext.digital_gain / 1000);

		if (debug_log(LOG_INFOS))
			printk("wm8994_ext: previous hp_level[%hu]: %hu\n",
				i, val);
	}

	// calculate number of steps for volume fade
	steps = wm8994->ext.hp_level[0] - hp_level_old[0];
	if (debug_log(LOG_INFOS))
		printk("wm8994_ext: volume change steps: %hd "
		       "start: %hu, end: %hu\n",
		       steps,
		       hp_level_old[0],
		       wm8994->ext.hp_level[0]);

	while (steps != 0) {
		if (wm8994->ext.hp_level[0] < hp_level_old[0])
			steps++;
		else
			steps--;

		if (debug_log(LOG_INFOS))
			printk("wm8994_ext: volume: %hu\n",
			       (hpvol(0) - steps));

		write_hpvol(hpvol(0) - steps, hpvol(1) - steps);

		if (steps != 0)
			udelay(1000);
	}

	wm8994->ext.bypass_write_extension = false;
}
#endif

#ifdef CONFIG_SND_WM8994_EXT_FM
void update_fm_radio_headset_restore_freqs(bool with_mute)
{
	unsigned short val;
	DECLARE_WM8994(codec);

	bypass_write_extension = true;
	// apply only when FM radio is active
	if (wm8994->fmradio_path == FMR_OFF)
		return;

	if (with_mute) {
		wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1,
			     WM8994_AIF2DAC_MUTE |
			     WM8994_AIF2DAC_MUTERATE |
			     WM8994_AIF2DAC_UNMUTE_RAMP |
			     WM8994_AIF2DAC_DEEMP_MASK);
		msleep(180);
	}

	if (fm_radio_headset_restore_bass) {
		// disable Sidetone high-pass filter
		// was designed for voice and not FM radio
		wm8994_write(codec, WM8994_SIDETONE, 0x0000);
		// disable 4FS ultrasonic mode and
		// restore the hi-fi <4Hz hi pass filter
		wm8994_write(codec, WM8994_AIF2_ADC_FILTERS,
			     WM8994_AIF2ADCL_HPF |
			     WM8994_AIF2ADCR_HPF);
	} else {
		// default settings in GT-I9000 Froyo XXJPX kernel sources
		wm8994_write(codec, WM8994_SIDETONE, 0x01c0);
		wm8994_write(codec, WM8994_AIF2_ADC_FILTERS, 0xF800);
	}

	if (fm_radio_headset_restore_highs) {
		val = wm8994_read(codec, WM8994_AIF2_DAC_FILTERS_1);
		val &= ~(WM8994_AIF2DAC_DEEMP_MASK);
		wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, val);
	} else {
		wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, 0x0036);
	}

	// un-mute
	if (with_mute) {
		val = wm8994_read(codec, WM8994_AIF2_DAC_FILTERS_1);
		val &= ~(WM8994_AIF2DAC_MUTE_MASK);
		wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, val);
	}
	bypass_write_extension = false;
}

void update_fm_radio_headset_normalize_gain(bool with_mute)
{
	DECLARE_WM8994(codec);

	bypass_write_extension = true;
	// apply only when FM radio is active
	if (wm8994->fmradio_path == FMR_OFF)
		return;

	if (fm_radio_headset_normalize_gain) {
		// Bumped volume, change with Zero Cross
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_3_4_VOLUME, 0x52);
		wm8994_write(codec, WM8994_RIGHT_LINE_INPUT_3_4_VOLUME, 0x152);
		wm8994_write(codec, WM8994_AIF2_DRC_2, 0x0840);
		wm8994_write(codec, WM8994_AIF2_DRC_3, 0x2408);
		wm8994_write(codec, WM8994_AIF2_DRC_4, 0x0082);
		wm8994_write(codec, WM8994_AIF2_DRC_5, 0x0100);
		wm8994_write(codec, WM8994_AIF2_DRC_1, 0x019C);
	} else {
		// Original volume, change with Zero Cross
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_3_4_VOLUME, 0x4B);
		wm8994_write(codec, WM8994_RIGHT_LINE_INPUT_3_4_VOLUME, 0x14B);
		wm8994_write(codec, WM8994_AIF2_DRC_2, 0x0840);
		wm8994_write(codec, WM8994_AIF2_DRC_3, 0x2400);
		wm8994_write(codec, WM8994_AIF2_DRC_4, 0x0000);
		wm8994_write(codec, WM8994_AIF2_DRC_5, 0x0000);
		wm8994_write(codec, WM8994_AIF2_DRC_1, 0x019C);
	}
	bypass_write_extension = false;
}
#endif

#ifdef CONFIG_SND_WM8994_EXT_RECORD_PRESETS
void update_recording_preset(bool with_mute)
{
    DECLARE_WM8994(codec);
	if (!is_path(MAIN_MICROPHONE))
		return;

	switch (wm8994->ext.recording_preset) {
	case 0:
		// Original:
		// On Galaxy S: IN1L_VOL1=11000 (+19.5 dB)
		// On Nexus S: variable value
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME,
			     WM8994_IN1L_VU | wm8994->ext.origin_recgain);
		wm8994_write(codec, WM8994_INPUT_MIXER_3, wm8994->ext.origin_recgain_mixer);
		// DRC disabled
		wm8994_write(codec, WM8994_AIF1_DRC1_1, 0x0080);
		break;
	case 2:
		// High sensitivy:
		// Original - 4.5 dB, IN1L_VOL1=10101 (+15 dB)
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, 0x0115);
		wm8994_write(codec, WM8994_INPUT_MIXER_3,
			     WM8994_IN1L_TO_MIXINL |
			     WM8994_IN1L_MIXINL_VOL);
		// DRC Input: -6dB, Ouptut -3.75dB
		//     Above knee 1/8, Below knee 1/2
		//     Max gain 24 / Min gain -12
		wm8994_write(codec, WM8994_AIF1_DRC1_1,
			     WM8994_AIF1DRC1_SIG_DET_MODE |
			     WM8994_AIF1DRC1_QR |
			     WM8994_AIF1DRC1_ANTICLIP |
			     WM8994_AIF1ADC1L_DRC_ENA);
		wm8994_write(codec, WM8994_AIF1_DRC1_2, 0x0426);
		wm8994_write(codec, WM8994_AIF1_DRC1_3, 0x0019);
		wm8994_write(codec, WM8994_AIF1_DRC1_4, 0x0105);
		break;
	case 3:
		// Concert new: IN1L_VOL1=10110 (+4.5 dB)
		// +30dB input mixer gain deactivated
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, 0x010F);
		wm8994_write(codec, WM8994_INPUT_MIXER_3,
		             WM8994_IN1L_TO_MIXINL);
		// DRC Input: -4.5dB, Ouptut -6.75dB
		//     Above knee 1/4, Below knee 1/2
		//     Max gain 24 / Min gain -12
		wm8994_write(codec, WM8994_AIF1_DRC1_1,
			     WM8994_AIF1DRC1_SIG_DET_MODE |
			     WM8994_AIF1DRC1_QR |
			     WM8994_AIF1DRC1_ANTICLIP |
			     WM8994_AIF1ADC1L_DRC_ENA);
		wm8994_write(codec, WM8994_AIF1_DRC1_2, 0x0846);
		wm8994_write(codec, WM8994_AIF1_DRC1_3, 0x0011);
		wm8994_write(codec, WM8994_AIF1_DRC1_4, 0x00C9);
		break;
	case 4:
		// ULTRA LOUD:
		// Original - 36 dB - 30 dB IN1L_VOL1=00000 (-16.5 dB)
		// +30dB input mixer gain deactivated
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, 0x0100);
		wm8994_write(codec, WM8994_INPUT_MIXER_3,
		             WM8994_IN1L_TO_MIXINL);
		// DRC Input: -7.5dB, Ouptut -6dB
		//     Above knee 1/8, Below knee 1/4
		//     Max gain 36 / Min gain -12
		wm8994_write(codec, WM8994_AIF1_DRC1_1,
			     WM8994_AIF1DRC1_SIG_DET_MODE |
			     WM8994_AIF1DRC1_QR |
			     WM8994_AIF1DRC1_ANTICLIP |
			     WM8994_AIF1ADC1L_DRC_ENA);
		wm8994_write(codec, WM8994_AIF1_DRC1_2, 0x0847);
		wm8994_write(codec, WM8994_AIF1_DRC1_3, 0x001A);
		wm8994_write(codec, WM8994_AIF1_DRC1_4, 0x00C9);
		break;
	default:
		// make sure recording_preset is the default
		wm8994->ext.recording_preset = 1;
		// New Balanced: Original - 16.5 dB
		// IN1L_VOL1=01101 (+27 dB)
		// +30dB input mixer gain deactivated
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, 0x055D);
		wm8994_write(codec, WM8994_INPUT_MIXER_3,
		             WM8994_IN1L_TO_MIXINL);
		// DRC Input: -18.5dB, Ouptut -9dB
		//     Above knee 1/8, Below knee 1/2
		//     Max gain 18 / Min gain -12
		wm8994_write(codec, WM8994_AIF1_DRC1_1,
			     WM8994_AIF1DRC1_SIG_DET_MODE |
			     WM8994_AIF1DRC1_QR |
			     WM8994_AIF1DRC1_ANTICLIP |
			     WM8994_AIF1ADC1L_DRC_ENA);
		wm8994_write(codec, WM8994_AIF1_DRC1_2, 0x0845);
		wm8994_write(codec, WM8994_AIF1_DRC1_3, 0x0019);
		wm8994_write(codec, WM8994_AIF1_DRC1_4, 0x030C);
		break;
	}
}
#endif

bool is_path(int unified_path)
{
	DECLARE_WM8994(codec);

	switch (unified_path) {
	// speaker
	case SPEAKER:
#ifdef GALAXY_TAB
		return (wm8994->cur_path == SPK
			|| wm8994->cur_path == RING_SPK
			|| wm8994->fmradio_path == FMR_SPK
			|| wm8994->fmradio_path == FMR_SPK_MIX);
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
		return (wm8994->cur_path == SPK
			|| wm8994->cur_path == SPK_HP);
#else
		return (wm8994->cur_path == SPK
			|| wm8994->cur_path == RING_SPK);
#endif
#endif

	// headphones
	case HEADPHONES:

#ifdef NEXUS_S
		return (wm8994->cur_path == HP
			|| wm8994->cur_path == HP_NO_MIC);
#else
#ifdef GALAXY_TAB
		return (wm8994->cur_path == HP3P
			|| wm8994->cur_path == HP4P
			|| wm8994->fmradio_path == FMR_HP);
#else
#ifdef M110S
		return (wm8994->cur_path == HP);
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
		return (wm8994->cur_path == HP
			|| wm8994->cur_path == HP_NO_MIC
#ifndef M110S
			|| wm8994->fmradio_path == FMR_HP
#endif
			);
#else
		return (wm8994->cur_path == HP
			|| wm8994->fmradio_path == FMR_HP);
#endif
#endif
#endif
#endif

	// FM Radio on headphones
	case RADIO_HEADPHONES:
#ifdef NEXUS_S
		return false;
#else
#ifdef M110S
		return false;
#else
#ifdef GALAXY_TAB
		return (wm8994->codec_state & FMRADIO_ACTIVE)
		    && (wm8994->fmradio_path == FMR_HP);
#else
		return (wm8994->codec_state & FMRADIO_ACTIVE)
		    && (wm8994->fmradio_path == FMR_HP);
#endif
#endif
#endif

	// Standard recording presets
	// for M110S Gingerbread: added check non call
	case MAIN_MICROPHONE:
		return (wm8994->codec_state & CAPTURE_ACTIVE)
		    && (wm8994->rec_path == MAIN)
		    && !(wm8994->codec_state & CALL_ACTIVE);

	}
	return false;
}

bool is_path_media_or_fm_no_call_no_record() {

	DECLARE_WM8994(codec);

	if ((is_path(HEADPHONES)
	     && (wm8994->codec_state & PLAYBACK_ACTIVE)
	     && (wm8994->stream_state & PCM_STREAM_PLAYBACK)
	     && !(wm8994->codec_state & CALL_ACTIVE)
	     && (wm8994->rec_path == MIC_OFF)
	    ) || is_path(RADIO_HEADPHONES))
		return true;

	return false;
}

#ifdef NEXUS_S
void update_speaker_tuning(bool with_mute)
{
	DECLARE_WM8994(codec);

	if (!(is_path(SPEAKER) || (wm8994->codec_state & CALL_ACTIVE)))
		return;

	if (wm8994->ext.speaker_tuning) {
		// DRC settings
		wm8994_write(codec, WM8994_AIF1_DRC1_3, 0x0010);
		wm8994_write(codec, WM8994_AIF1_DRC1_4, 0x00EB);

		// hardware EQ
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_GAINS_1,   0x041D);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_GAINS_2,   0x4C00);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_1_A,  0x0FE3);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_1_B,  0x0403);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_1_PG, 0x0074);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_2_A,  0x1F03);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_2_B,  0xF0F9);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_2_C,  0x040A);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_2_PG, 0x03DA);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_3_A,  0x1ED2);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_3_B,  0xF11A);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_3_C,  0x040A);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_3_PG, 0x045D);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_4_A,  0x0E76);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_4_B,  0xFCE4);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_4_C,  0x040A);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_4_PG, 0x330D);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_5_A,  0xFC8F);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_5_B,  0x0400);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_5_PG, 0x323C);

		// Speaker Boost tuning
		wm8994_write(codec, WM8994_CLASSD,                 0x0170);
	} else {
		// DRC settings
		wm8994_write(codec, WM8994_AIF1_DRC1_3, 0x0028);
		wm8994_write(codec, WM8994_AIF1_DRC1_4, 0x0186);

		// hardware EQ
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_GAINS_1,   0x0019);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_GAINS_2,   0x6280);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_1_A,  0x0FC3);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_1_B,  0x03FD);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_1_PG, 0x00F4);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_2_A,  0x1F30);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_2_B,  0xF0CD);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_2_C,  0x040A);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_2_PG, 0x032C);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_3_A,  0x1C52);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_3_B,  0xF379);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_3_C,  0x040A);
		wm8994_write(codec, WM8994_AIF1_DAC1_EQ_BAND_3_PG, 0x0DC1);
		wm8994_write(codec, WM8994_CLASSD,                 0x0170);

		// Speaker Boost tuning
		wm8994_write(codec, WM8994_CLASSD,                 0x0168);
	}
}
#endif

unsigned short osr128_get_value(unsigned short val)
{
    DECLARE_WM8994(codec);

	if (wm8994->ext.dac_osr128 == 1)
		val |= WM8994_DAC_OSR128;
	else
		val &= ~WM8994_DAC_OSR128;

	if (wm8994->ext.adc_osr128 == 1)
		val |= WM8994_ADC_OSR128;
	else
		val &= ~WM8994_ADC_OSR128;

	return val;
}

void update_osr128(bool with_mute)
{
	unsigned short val;
    DECLARE_WM8994(codec);

	val = osr128_get_value(wm8994_read(codec, WM8994_OVERSAMPLING));
	wm8994->ext.bypass_write_extension = true;
	wm8994_write(codec, WM8994_OVERSAMPLING, val);
	wm8994->ext.bypass_write_extension = false;
}

unsigned short fll_tuning_get_value(unsigned short val)
{
    DECLARE_WM8994(codec);
	val = (val >> WM8994_FLL1_GAIN_WIDTH << WM8994_FLL1_GAIN_WIDTH);
	if (wm8994->ext.fll_tuning == 1)
		val |= 5;

	return val;
}

void update_fll_tuning(bool with_mute)
{
	unsigned short val;
    DECLARE_WM8994(codec);
	val = fll_tuning_get_value(wm8994_read(codec, WM8994_FLL1_CONTROL_4));
	wm8994->ext.bypass_write_extension = true;
	wm8994_write(codec, WM8994_FLL1_CONTROL_4, val);
	wm8994->ext.bypass_write_extension = false;
}

unsigned short mono_downmix_get_value(unsigned short val, bool can_reverse)
{
	DECLARE_WM8994(codec);

	// Takes care not switching to Stereo on speaker or during a call
	if (!is_path(SPEAKER) && !(wm8994->codec_state & CALL_ACTIVE)) {
        if (debug_log(LOG_VERBOSE))
            printk("%s: mono_downmix=%d\n", __func__, wm8994->ext.mono_downmix);
		if (wm8994->ext.mono_downmix) {
			val |= WM8994_AIF1DAC1_MONO;
		} else {
			if (can_reverse)
				val &= ~WM8994_AIF1DAC1_MONO;
		}
	}

	return val;
}

void update_mono_downmix(bool with_mute)
{
	unsigned short val1, val2, val3;
    DECLARE_WM8994(codec);

    if (debug_log(LOG_VERBOSE))
        printk("%s: mono_downmix=%d\n", __func__, wm8994->ext.mono_downmix);
	val1 = mono_downmix_get_value(wm8994_read
				      (codec, WM8994_AIF1_DAC1_FILTERS_1),
				      true);
	val2 = mono_downmix_get_value(wm8994_read
				      (codec, WM8994_AIF1_DAC2_FILTERS_1),
				      true);
	val3 = mono_downmix_get_value(wm8994_read
				      (codec, WM8994_AIF2_DAC_FILTERS_1),
				      true);

	wm8994->ext.bypass_write_extension = true;
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val1);
	wm8994_write(codec, WM8994_AIF1_DAC2_FILTERS_1, val2);
	wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, val3);
	wm8994->ext.bypass_write_extension = false;
}

unsigned short dac_direct_get_value(unsigned short val, bool can_reverse)
{
    DECLARE_WM8994(codec);
	if (is_path_media_or_fm_no_call_no_record()) {

		if (wm8994->ext.dac_direct) {
			if (val == WM8994_DAC1L_TO_MIXOUTL)
				return WM8994_DAC1L_TO_HPOUT1L;
		} else {
			if (val == WM8994_DAC1L_TO_HPOUT1L
			    && can_reverse)
				return WM8994_DAC1L_TO_MIXOUTL;
		}
	}

	return val;
}

void update_dac_direct(bool with_mute)
{
	unsigned short val1, val2;
    DECLARE_WM8994(codec);
	val1 = dac_direct_get_value(wm8994_read(codec,
						WM8994_OUTPUT_MIXER_1), true);
	val2 = dac_direct_get_value(wm8994_read(codec,
						WM8994_OUTPUT_MIXER_2), true);

	wm8994->ext.bypass_write_extension = true;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val1);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val2);
	wm8994->ext.bypass_write_extension = false;
}

unsigned short digital_gain_get_value(unsigned short val)
{
	// AIF gain to 0dB
	int aif_gain = 0xC0;
	int i;
	int step = -375;
    DECLARE_WM8994(codec);

	if (is_path_media_or_fm_no_call_no_record()) {

		if (wm8994->ext.digital_gain <= 0) {
			// clear the actual DAC volume for this value
			val &= ~(WM8994_DAC1R_VOL_MASK);

			// calculation with round
			i = ((wm8994->ext.digital_gain * 10 / step) + 5) / 10;
			aif_gain -= i;
			val |= aif_gain;

			if (debug_log(LOG_INFOS))
				printk("wm8994_ext: digital gain: %d mdB, "
				       "steps: %d, real AIF gain: %d mdB\n",
				wm8994->ext.digital_gain, i, i * step);
		}
	}

	return val;
}

void update_digital_gain(bool with_mute)
{
	unsigned short val1, val2;
    DECLARE_WM8994(codec);
	val1 = digital_gain_get_value(wm8994_read(codec,
						WM8994_AIF1_DAC1_LEFT_VOLUME));
	val2 = digital_gain_get_value(wm8994_read(codec,
						WM8994_AIF1_DAC1_RIGHT_VOLUME));

	wm8994->ext.bypass_write_extension = true;
	wm8994_write(codec, WM8994_AIF1_DAC1_LEFT_VOLUME,
		     WM8994_DAC1_VU | val1);
	wm8994_write(codec, WM8994_AIF1_DAC1_RIGHT_VOLUME,
		     WM8994_DAC1_VU | val2);
	wm8994->ext.bypass_write_extension = false;
}

void update_headphone_eq(bool with_mute)
{
	int gains_1;
	int gains_2;
	int i;
	int j;
	int k = 0;
	int first_reg = WM8994_AIF1_DAC1_EQ_BAND_1_A;
    DECLARE_WM8994(codec);

	if (!is_path_media_or_fm_no_call_no_record()) {
		// don't apply the EQ
		return;
	}

	if (debug_log(LOG_INFOS))
		printk("wm8994_ext: EQ gains (dB): %hd, %hd, %hd, %hd, %hd\n",
               wm8994->ext.eq_gains[0], wm8994->ext.eq_gains[1], wm8994->ext.eq_gains[2],
               wm8994->ext.eq_gains[3], wm8994->ext.eq_gains[4]);

	gains_1 =
	    ((wm8994->ext.eq_gains[0] + 12) << WM8994_AIF1DAC1_EQ_B1_GAIN_SHIFT) |
	    ((wm8994->ext.eq_gains[1] + 12) << WM8994_AIF1DAC1_EQ_B2_GAIN_SHIFT) |
	    ((wm8994->ext.eq_gains[2] + 12) << WM8994_AIF1DAC1_EQ_B3_GAIN_SHIFT) |
	    wm8994->ext.headphone_eq;

	gains_2 =
	    ((wm8994->ext.eq_gains[3] + 12) << WM8994_AIF1DAC1_EQ_B4_GAIN_SHIFT) |
	    ((wm8994->ext.eq_gains[4] + 12) << WM8994_AIF1DAC1_EQ_B5_GAIN_SHIFT);

	wm8994_write(codec, WM8994_AIF1_DAC1_EQ_GAINS_1, gains_1);
	wm8994_write(codec, WM8994_AIF1_DAC1_EQ_GAINS_2, gains_2);

	// don't send EQ configuration if its not enabled
	if (!wm8994->ext.headphone_eq)
		return;

	for (i = 0; i < ARRAY_SIZE(wm8994->ext.eq_band_values); i++) {
		if (debug_log(LOG_INFOS))
			printk("wm8994_ext: send EQ Band %d\n", i + 1);

		for (j = 0; j < eq_bands[i]; j++) {
			wm8994_write(codec,
				     first_reg + k, wm8994->ext.eq_band_values[i][j]);
			k++;
		}
	}
}

void update_stereo_expansion(bool with_mute)
{
	short unsigned int val;
    DECLARE_WM8994(codec);

	val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_2);
	if (wm8994->ext.stereo_expansion) {
		val &= ~(WM8994_AIF1DAC1_3D_GAIN_MASK);
		val |= (wm8994->ext.stereo_expansion_gain << WM8994_AIF1DAC1_3D_GAIN_SHIFT);
	}
	val &= ~(WM8994_AIF1DAC1_3D_ENA_MASK);
	val |= (wm8994->ext.stereo_expansion << WM8994_AIF1DAC1_3D_ENA_SHIFT);

	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_2, val);
}

void load_current_eq_values()
{
	int i;
	int j;
	int k = 0;
	int first_reg = WM8994_AIF1_DAC1_EQ_BAND_1_A;
    DECLARE_WM8994(codec);

	for (i = 0; i < ARRAY_SIZE(wm8994->ext.eq_band_values); i++)
		for (j = 0; j < eq_bands[i]; j++) {
			wm8994->ext.eq_band_values[i][j] =
			    wm8994_read(codec, first_reg + k);
			k++;
		}
}

void apply_saturation_prevention_drc()
{
	unsigned short val;
	unsigned short drc_gain = 0;
	int i;
	int step = 750;
    DECLARE_WM8994(codec);

	// don't apply the limiter if not playing media
	// (exclude FM radio, it has its own DRC settings)
	if (!is_path_media_or_fm_no_call_no_record()
	    || is_path(RADIO_HEADPHONES))
		return;

	// don't apply the limiter without stereo_expansion or headphone_eq
	// or a positive digital gain
	if (!(wm8994->ext.stereo_expansion
	      || wm8994->ext.headphone_eq
	      || wm8994->ext.digital_gain >= 0))
		return;

	// configure the DRC to avoid saturation: not actually compress signal
	// gain is unmodified. Should affect only what's higher than 0 dBFS

	// tune Attack & Decacy values
	val = wm8994_read(codec, WM8994_AIF1_DRC1_2);
	val &= ~(WM8994_AIF1DRC1_ATK_MASK);
	val &= ~(WM8994_AIF1DRC1_DCY_MASK);
	val |= (0x1 << WM8994_AIF1DRC1_ATK_SHIFT);
	val |= (0x4 << WM8994_AIF1DRC1_DCY_SHIFT);

	// set DRC maximum gain to 36 dB
	val &= ~(WM8994_AIF1DRC1_MAXGAIN_MASK);
	val |= (0x3 << WM8994_AIF1DRC1_MAXGAIN_SHIFT);

	wm8994_write(codec, WM8994_AIF1_DRC1_2, val);

	// Above knee: flat (what really avoid the saturation)
	val = wm8994_read(codec, WM8994_AIF1_DRC1_3);
	val |= (0x5 << WM8994_AIF1DRC1_HI_COMP_SHIFT);
	wm8994_write(codec, WM8994_AIF1_DRC1_3, val);

	val = wm8994_read(codec, WM8994_AIF1_DRC1_1);
	// disable Quick Release and Anti Clip
	// both do do more harm than good for this particular usage
	val &= ~(WM8994_AIF1DRC1_QR_MASK);
	val &= ~(WM8994_AIF1DRC1_ANTICLIP_MASK);

	// enable DRC
	val &= ~(WM8994_AIF1DAC1_DRC_ENA_MASK);
	val |= WM8994_AIF1DAC1_DRC_ENA;
	wm8994_write(codec, WM8994_AIF1_DRC1_1, val);

	val = wm8994_read(codec, WM8994_AIF1_DRC1_4);
	val &= ~(WM8994_AIF1DRC1_KNEE_IP_MASK);

	if (wm8994->ext.digital_gain >= 0) {
		// deal with positive digital gains
		i = ((wm8994->ext.digital_gain * 10 / step) + 5) / 10;
		drc_gain += i;
		val |= (drc_gain << WM8994_AIF1DRC1_KNEE_IP_SHIFT);

		if (debug_log(LOG_INFOS))
			printk("wm8994_ext: digital gain: %d mdB, "
			       "steps: %d, real DRC gain: %d mdB\n",
			wm8994->ext.digital_gain, i, i * step);

	}
	wm8994_write(codec, WM8994_AIF1_DRC1_4, val);
}

/*
 *
 * Declaring the controling misc devices
 *
 */
static ssize_t debug_log_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
    DECLARE_WM8994(codec);
	return sprintf(buf, "%u\n", wm8994->ext.debug_log_level);
}

static ssize_t debug_log_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
    DECLARE_WM8994(codec);
	sscanf(buf, "%hu", &wm8994->ext.debug_log_level);
	return size;
}

#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
static ssize_t headphone_amplifier_level_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
    DECLARE_WM8994(codec);
	// output median of left and right headphone amplifier volumes
	return sprintf(buf, "%u\n", (wm8994->ext.hp_level[0] + wm8994->ext.hp_level[1]) / 2);
}

static ssize_t headphone_amplifier_level_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t size)
{
	unsigned short vol;
    DECLARE_WM8994(codec);

	if (sscanf(buf, "%hu", &vol) == 1) {

		// hard limit to 62 because 63 introduces distortions
		if (vol > 62)
			vol = 62;

		// left and right are set to the same volumes by this control
		wm8994->ext.hp_level[0] = wm8994->ext.hp_level[1] = vol;

		update_digital_gain(false);
		update_hpvol(true);
	}
	return size;
}
#endif

#define DECLARE_BOOL_SHOW(name) 					       \
static ssize_t name##_show(struct device *dev,				       \
struct device_attribute *attr, char *buf)				       \
{									       \
    DECLARE_WM8994(codec); \
	return sprintf(buf,"%u\n",(wm8994->ext.name ? 1 : 0));			       \
}

#define DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(name, updater, with_mute)	       \
static ssize_t name##_store(struct device *dev, struct device_attribute *attr, \
	const char *buf, size_t size)					       \
{									       \
	unsigned short state;						       \
    DECLARE_WM8994(codec); \
                                               \
	if (sscanf(buf, "%hu", &state) == 1) {				       \
		wm8994->ext.name = state == 0 ? false : true;			       \
		if (debug_log(LOG_INFOS))				       \
			printk("wm8994_ext: %s: %u\n", #updater, state);     \
		updater(with_mute);					       \
	}								       \
	return size;							       \
}

#ifdef NEXUS_S
DECLARE_BOOL_SHOW(speaker_tuning);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(speaker_tuning,
				    update_speaker_tuning,
				    false);
#endif

#ifdef CONFIG_SND_WM8994_EXT_FM
DECLARE_BOOL_SHOW(fm_radio_headset_restore_bass);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(fm_radio_headset_restore_bass,
				    update_fm_radio_headset_restore_freqs,
				    true);

DECLARE_BOOL_SHOW(fm_radio_headset_restore_highs);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(fm_radio_headset_restore_highs,
				    update_fm_radio_headset_restore_freqs,
				    true);

DECLARE_BOOL_SHOW(fm_radio_headset_normalize_gain);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(fm_radio_headset_normalize_gain,
				    update_fm_radio_headset_normalize_gain,
				    false);
#endif

#ifdef CONFIG_SND_WM8994_EXT_RECORD_PRESETS
static ssize_t recording_preset_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
    DECLARE_WM8994(codec);
	return sprintf(buf, "%d\n", wm8994->ext.recording_preset);
}

static ssize_t recording_preset_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	unsigned short preset_number;
    DECLARE_WM8994(codec);
	if (sscanf(buf, "%hu", &preset_number) == 1) {
		wm8994->ext.recording_preset = preset_number;
		update_recording_preset(false);
	}
	return size;
}
#endif

DECLARE_BOOL_SHOW(dac_osr128);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(dac_osr128,
				    update_osr128,
				    false);

DECLARE_BOOL_SHOW(adc_osr128);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(adc_osr128,
				    update_osr128,
				    false);

DECLARE_BOOL_SHOW(fll_tuning);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(fll_tuning,
				    update_fll_tuning,
				    false);

DECLARE_BOOL_SHOW(mono_downmix);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(mono_downmix,
				    update_mono_downmix,
				    false);

DECLARE_BOOL_SHOW(dac_direct);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(dac_direct,
				    update_dac_direct,
				    false);

static ssize_t digital_gain_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
    DECLARE_WM8994(codec);
	return sprintf(buf, "%d\n", wm8994->ext.digital_gain);
}

static ssize_t digital_gain_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	int new_digital_gain;
    DECLARE_WM8994(codec);
	if (sscanf(buf, "%d", &new_digital_gain) == 1) {
		if (new_digital_gain <= 36000 && new_digital_gain >= -71625) {
			if (new_digital_gain > wm8994->ext.digital_gain) {
				// reduce analog volume first
				wm8994->ext.digital_gain = new_digital_gain;
#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
				update_hpvol(false);
#endif
				update_digital_gain(false);
			} else {
				// reduce digital volume first
				wm8994->ext.digital_gain = new_digital_gain;
				update_digital_gain(false);
#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
				update_hpvol(false);
#endif
			}
		}
		apply_saturation_prevention_drc();
	}
	return size;
}

DECLARE_BOOL_SHOW(headphone_eq);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(headphone_eq,
				    update_headphone_eq,
				    false);

#define DECLARE_EQ_GAIN_SHOW(band)					       \
static ssize_t headphone_eq_b##band##_gain_show(struct device *dev,	       \
					 struct device_attribute *attr,	       \
					 char *buf)			       \
{									       \
    DECLARE_WM8994(codec); \
	return sprintf(buf, "%d\n", wm8994->ext.eq_gains[band-1]);			       \
}

#define DECLARE_EQ_GAIN_STORE(band)					       \
static ssize_t headphone_eq_b##band##_gain_store(struct device *dev,	       \
					  struct device_attribute *attr,       \
					  const char *buf, size_t size)	       \
{									       \
	short new_gain;							       \
    DECLARE_WM8994(codec); \
	if (sscanf(buf, "%hd", &new_gain) == 1) {			       \
		if (new_gain >= -12 && new_gain <= 12) {		       \
			wm8994->ext.eq_gains[band-1] = new_gain;			       \
			update_headphone_eq(false);			       \
		}							       \
	}								       \
	return size;							       \
}

DECLARE_EQ_GAIN_SHOW(1);
DECLARE_EQ_GAIN_STORE(1);
DECLARE_EQ_GAIN_SHOW(2);
DECLARE_EQ_GAIN_STORE(2);
DECLARE_EQ_GAIN_SHOW(3);
DECLARE_EQ_GAIN_STORE(3);
DECLARE_EQ_GAIN_SHOW(4);
DECLARE_EQ_GAIN_STORE(4);
DECLARE_EQ_GAIN_SHOW(5);
DECLARE_EQ_GAIN_STORE(5);

static ssize_t headphone_eq_bands_values_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	int i;
	int j;
	int k = 0;
	int first_reg = WM8994_AIF1_DAC1_EQ_BAND_1_A;
	int bands_size = ARRAY_SIZE(eq_bands);
	char *name;

	for (i = 0; i < bands_size; i++)
		for (j = 0; j < eq_bands[i]; j++) {

			// display 3-coef bands properly (hi & lo shelf)
			if (j + 1 == eq_bands[i])
				name = eq_band_coef_names[3];
			else
				name = eq_band_coef_names[j];

			sprintf(buf, "%s%d %s 0x%04X\n", buf,
				i + 1, name,
				wm8994_read(codec, first_reg + k));
			k++;
		}

	return sprintf(buf, "%s", buf);
}

static ssize_t headphone_eq_bands_values_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t size)
{
	int i;
	short unsigned int val;
	short unsigned int band;
	char coef_name[2];
	unsigned int bytes_read = 0;
    DECLARE_WM8994(codec);

	while (sscanf(buf, "%hu %s %hx%n",
		      &band, coef_name, &val, &bytes_read) == 3) {

		buf += bytes_read;

		if (band < 1 || band > 5)
			continue;

		for (i = 0; i < ARRAY_SIZE(eq_band_coef_names); i++) {
			// loop through band coefficient letters
			if (strncmp(eq_band_coef_names[i], coef_name, 2) == 0) {
				if (eq_bands[band-1] == 3 && i == 3)
					// deal with high and low shelves
					wm8994->ext.eq_band_values[band-1][2] = val;
				else
					// parametric bands
					wm8994->ext.eq_band_values[band-1][i] = val;

				if (debug_log(LOG_INFOS))
					printk("wm8994_ext: read EQ from "
					       "sysfs: EQ Band %hd %s: 0x%04X\n"
					       , band, coef_name, val);
				break;
			}
		}
	}
    update_headphone_eq(false);

	return size;
}

DECLARE_BOOL_SHOW(stereo_expansion);
DECLARE_BOOL_STORE_UPDATE_WITH_MUTE(stereo_expansion,
				    update_stereo_expansion,
				    false);
static ssize_t stereo_expansion_gain_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
    DECLARE_WM8994(codec);
	return sprintf(buf, "%u\n", wm8994->ext.stereo_expansion_gain);
}

static ssize_t stereo_expansion_gain_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	short unsigned val;
    DECLARE_WM8994(codec);

	if (sscanf(buf, "%hu", &val) == 1)
		if (val >= 0 && val < 32) {
			wm8994->ext.stereo_expansion_gain = val;
			update_stereo_expansion(false);
		}

	return size;
}

#ifdef CONFIG_SND_WM8994_EXT_DEVELOPMENT
static ssize_t show_wm8994_register_dump(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	// modified version of register_dump from wm8994_aries.c
	// r = wm8994 register
	int r;

	for (r = 0; r <= 0x6; r++)
		sprintf(buf, "0x%X 0x%X\n", r, wm8994_read(codec, r));

	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x15, wm8994_read(codec, 0x15));

	for (r = 0x18; r <= 0x3C; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x4C, wm8994_read(codec, 0x4C));

	for (r = 0x51; r <= 0x5C; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x60, wm8994_read(codec, 0x60));
	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x101, wm8994_read(codec, 0x101));
	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x110, wm8994_read(codec, 0x110));
	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x111, wm8994_read(codec, 0x111));

	for (r = 0x200; r <= 0x212; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x220; r <= 0x224; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x240; r <= 0x244; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x300; r <= 0x317; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x400; r <= 0x411; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x420; r <= 0x423; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x440; r <= 0x444; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x450; r <= 0x454; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x480; r <= 0x493; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x4A0; r <= 0x4B3; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x500; r <= 0x503; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x510, wm8994_read(codec, 0x510));
	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x520, wm8994_read(codec, 0x520));
	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x521, wm8994_read(codec, 0x521));

	for (r = 0x540; r <= 0x544; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x580; r <= 0x593; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	for (r = 0x600; r <= 0x614; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x620, wm8994_read(codec, 0x620));
	sprintf(buf, "%s0x%X 0x%X\n", buf, 0x621, wm8994_read(codec, 0x621));

	for (r = 0x700; r <= 0x70A; r++)
		sprintf(buf, "%s0x%X 0x%X\n", buf, r, wm8994_read(codec, r));

	return sprintf(buf, "%s", buf);
}

static ssize_t store_wm8994_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	short unsigned int reg = 0;
	short unsigned int val = 0;
	int unsigned bytes_read = 0;
    DECLARE_WM8994(codec);

	while (sscanf(buf, "%hx %hx%n", &reg, &val, &bytes_read) == 2) {
		buf += bytes_read;
		if (debug_log(LOG_INFOS));
			printk("wm8994_ext: read from sysfs: %X, %X\n",
			       reg, val);

		wm8994->ext.bypass_write_extension = true;
		wm8994_write(codec, reg, val);
		wm8994->ext.bypass_write_extension = false;
	}
	return size;
}
#endif

static ssize_t wm8994_ext_version(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", WM8994_EXT_VERSION);
}

DECLARE_BOOL_SHOW(enable);
static ssize_t enable_store(struct device *dev,
			    struct device_attribute *attr, const char *buf,
			    size_t size)
{
	unsigned short state;
	bool bool_state;
    DECLARE_WM8994(codec);

	if (sscanf(buf, "%hu", &state) == 1) {
		bool_state = state == 0 ? false : true;
		if (state != wm8994->ext.enable) {
			wm8994->ext.enable = bool_state;
			update_enable();
		}
	}
	return size;
}

static DEVICE_ATTR(debug_log, S_IRUGO | S_IWUGO,
		   debug_log_show,
		   debug_log_store);

#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
static DEVICE_ATTR(headphone_amplifier_level, S_IRUGO | S_IWUGO,
		   headphone_amplifier_level_show,
		   headphone_amplifier_level_store);

int wm8994_get_headphone_amplifier_level(struct snd_kcontrol *kcontrol,
                                         struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.hp_level[0] + wm8994->ext.hp_level[1];
}

int wm8994_set_headphone_amplifier_level(struct snd_kcontrol *kcontrol,
                                         struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned short vol = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);

    // hard limit to 62 because 63 introduces distortions
    if (vol > 62)
        vol = 62;
    
    // left and right are set to the same volumes by this control
    wm8994->ext.hp_level[0] = wm8994->ext.hp_level[1] = vol;
    
    update_digital_gain(false);
    update_hpvol(true);
    return 0;
}
#endif

#ifdef NEXUS_S
static DEVICE_ATTR(speaker_tuning, S_IRUGO | S_IWUGO,
		   speaker_tuning_show,
		   speaker_tuning_store);
int wm8994_get_speaker_tuning(struct snd_kcontrol *kcontrol,
                              struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.speaker_tuning ? 1 : 0;
}
int wm8994_set_speaker_tuning(struct snd_kcontrol *kcontrol,
                              struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.speaker_tuning = ucontrol->value.enumerated.item[0] == 0 ? false : true;
    update_speaker_tuning(false);
    return 0;
}
#endif

#ifdef CONFIG_SND_WM8994_EXT_FM
static DEVICE_ATTR(fm_radio_headset_restore_bass, S_IRUGO | S_IWUGO,
		   fm_radio_headset_restore_bass_show,
		   fm_radio_headset_restore_bass_store);
int wm8994_get_fm_radio_headset_restore_bass(struct snd_kcontrol *kcontrol,
                                             struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.fm_radio_headset_restore_bass ? 1 : 0;
}
int wm8994_set_fm_radio_headset_restore_bass(struct snd_kcontrol *kcontrol,
                                             struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.fm_radio_restore_bass = ucontrol->value.enumerated.item[0] == 0 ? false : true;
    update_fm_radio_headset_restore_freqs(true);
    return 0;
}

static DEVICE_ATTR(fm_radio_headset_restore_highs, S_IRUGO | S_IWUGO,
		   fm_radio_headset_restore_highs_show,
		   fm_radio_headset_restore_highs_store);
int wm8994_get_fm_radio_headset_restore_highs(struct snd_kcontrol *kcontrol,
                                              struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.fm_radio_headset_restore_highs ? 1 : 0;
}
int wm8994_set_fm_radio_headset_restore_highs(struct snd_kcontrol *kcontrol,
                                              struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.fm_radio_restore_highs = ucontrol->value.enumerated.item[0] == 0 ? false : true;
    update_fm_radio_headset_restore_freqs(true);
    return 0;
}

static DEVICE_ATTR(fm_radio_headset_normalize_gain, S_IRUGO | S_IWUGO,
		   fm_radio_headset_normalize_gain_show,
		   fm_radio_headset_normalize_gain_store);
int wm8994_get_fm_radio_headset_normalize_gain(struct snd_kcontrol *kcontrol,
                                               struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.fm_radio_headset_normalize_gain ? 1 : 0;
}
int wm8994_set_fm_radio_headset_normlize_gain(struct snd_kcontrol *kcontrol,
                                              struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.fm_radio_normalize_gain = ucontrol->value.enumerated.item[0] == 0 ? false : true;
    update_fm_radio_headset_normalize_gain(false);
    return 0;
}
#endif

#ifdef CONFIG_SND_WM8994_EXT_RECORD_PRESETS
static DEVICE_ATTR(recording_preset, S_IRUGO | S_IWUGO,
		   recording_preset_show,
		   recording_preset_store);
int wm8994_get_recording_preset(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.recording_preset;
}

int wm8994_set_recording_preset(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned short preset_number = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);
    wm8994->ext.recording_preset = preset_number;
    update_recording_preset(false);
    return 0;
}
#endif

static DEVICE_ATTR(dac_osr128, S_IRUGO | S_IWUGO,
		   dac_osr128_show,
		   dac_osr128_store);
int wm8994_get_output_oversampling(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);

	return wm8994->ext.dac_osr128;
}
int wm8994_set_output_oversampling(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);

	wm8994->ext.dac_osr128 = ucontrol->value.enumerated.item[0];
	update_osr128(false);
    return wm8994->ext.dac_osr128;
}

static DEVICE_ATTR(adc_osr128, S_IRUGO | S_IWUGO,
		   adc_osr128_show,
		   adc_osr128_store);
int wm8994_set_input_oversampling(struct snd_kcontrol *kcontrol,
                                  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);

	wm8994->ext.adc_osr128 = ucontrol->value.enumerated.item[0];
	update_osr128(false);

    return wm8994->ext.adc_osr128;
}
int wm8994_get_input_oversampling(struct snd_kcontrol *kcontrol,
                                  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	return wm8994->ext.adc_osr128;
}

static DEVICE_ATTR(fll_tuning, S_IRUGO | S_IWUGO,
		   fll_tuning_show,
		   fll_tuning_store);
int wm8994_get_fll_tuning(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.fll_tuning ? 1 : 0;
}
int wm8994_set_fll_tuning(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.fll_tuning = ucontrol->value.enumerated.item[0] == 0 ? false : true;
	update_fll_tuning(false);
    return 0;
}

static DEVICE_ATTR(dac_direct, S_IRUGO | S_IWUGO,
		   dac_direct_show,
		   dac_direct_store);
int wm8994_get_dac_direct(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.dac_direct ? 1 : 0;
}
int wm8994_set_dac_direct(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.dac_direct = ucontrol->value.enumerated.item[0] == 0 ? false : true;
	update_dac_direct(false);
    return 0;
}

static DEVICE_ATTR(digital_gain, S_IRUGO | S_IWUGO,
		   digital_gain_show,
		   digital_gain_store);
int wm8994_get_digital_gain(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.digital_gain;
}

int wm8994_set_digital_gain(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int new_digital_gain = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);

    if (new_digital_gain <= 36000 && new_digital_gain >= -71625) {
        if (new_digital_gain > wm8994->ext.digital_gain) {
            // reduce analog volume first
            wm8994->ext.digital_gain = new_digital_gain;
#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
            update_hpvol(false);
#endif
            update_digital_gain(false);
        } else {
            // reduce digital volume first
            wm8994->ext.digital_gain = new_digital_gain;
            update_digital_gain(false);
#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
            update_hpvol(false);
#endif
        }
    }
    apply_saturation_prevention_drc();
    return 0;
}

static DEVICE_ATTR(headphone_eq, S_IRUGO | S_IWUGO,
		   headphone_eq_show,
		   headphone_eq_store);
int wm8994_get_headphone_eq(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.headphone_eq ? 1 : 0;
}
int wm8994_set_headphone_eq(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.headphone_eq = ucontrol->value.enumerated.item[0] == 0 ? false : true;
	update_headphone_eq(false);
    return 0;
}

static DEVICE_ATTR(headphone_eq_b1_gain, S_IRUGO | S_IWUGO,
		   headphone_eq_b1_gain_show,
		   headphone_eq_b1_gain_store);
int wm8994_get_headphone_eq_b1_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.eq_gains[0];
}

int wm8994_set_headphone_eq_b1_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	short new_gain = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);

    if (new_gain >= -12 && new_gain <= 12) {
        wm8994->ext.eq_gains[0] = new_gain;
        update_headphone_eq(false);
    }
    return 0;
}

static DEVICE_ATTR(headphone_eq_b2_gain, S_IRUGO | S_IWUGO,
		   headphone_eq_b2_gain_show,
		   headphone_eq_b2_gain_store);
int wm8994_get_headphone_eq_b2_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.eq_gains[1];
}

int wm8994_set_headphone_eq_b2_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	short new_gain = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);

    if (new_gain >= -12 && new_gain <= 12) {
        wm8994->ext.eq_gains[1] = new_gain;
        update_headphone_eq(false);
    }
    return 0;
}

static DEVICE_ATTR(headphone_eq_b3_gain, S_IRUGO | S_IWUGO,
		   headphone_eq_b3_gain_show,
		   headphone_eq_b3_gain_store);
int wm8994_get_headphone_eq_b3_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.eq_gains[2];
}

int wm8994_set_headphone_eq_b3_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	short new_gain = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);

    if (new_gain >= -12 && new_gain <= 12) {
        wm8994->ext.eq_gains[2] = new_gain;
        update_headphone_eq(false);
    }
    return 0;
}

static DEVICE_ATTR(headphone_eq_b4_gain, S_IRUGO | S_IWUGO,
		   headphone_eq_b4_gain_show,
		   headphone_eq_b4_gain_store);
int wm8994_get_headphone_eq_b4_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.eq_gains[3];
}

int wm8994_set_headphone_eq_b4_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	short new_gain = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);

    if (new_gain >= -12 && new_gain <= 12) {
        wm8994->ext.eq_gains[3] = new_gain;
        update_headphone_eq(false);
    }
    return 0;
}

static DEVICE_ATTR(headphone_eq_b5_gain, S_IRUGO | S_IWUGO,
		   headphone_eq_b5_gain_show,
		   headphone_eq_b5_gain_store);
int wm8994_get_headphone_eq_b5_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.eq_gains[4];
}

int wm8994_set_headphone_eq_b5_gain(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	short new_gain = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);

    if (new_gain >= -12 && new_gain <= 12) {
        wm8994->ext.eq_gains[4] = new_gain;
        update_headphone_eq(false);
    }
    return 0;
}

static DEVICE_ATTR(headphone_eq_bands_values, S_IRUGO | S_IWUGO,
		   headphone_eq_bands_values_show,
		   headphone_eq_bands_values_store);
#define DECLARE_HEADPHONE_EQ_BAND_SHOW(name,band,coeff)\
int wm8884_get_headphone_eq_band_##name##_value(struct snd_kcontrol *kcontrol, \
                                                struct snd_ctl_elem_value *ucontrol) \
{ \
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol); \
    DECLARE_WM8994(codec); \
    return wm8994->ext.eq_band_values[band][coeff];   \
}

#define DECLARE_HEADPHONE_EQ_BAND_STORE(name,band,coeff) \
int wm8994_set_headphone_eq_band_##name##_value(struct snd_kcontrol *kcontrol, \
                                                struct snd_ctl_elem_value *ucontrol) \
{ \
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol); \
	int val = ucontrol->value.integer.value[0]; \
    DECLARE_WM8994(codec); \
    wm8994->ext.eq_band_values[band][coeff] = val; \
    update_headphone_eq(false); \
    return 0; \
}

DECLARE_HEADPHONE_EQ_BAND_SHOW(1_A,0,0)
DECLARE_HEADPHONE_EQ_BAND_STORE(1_A,0,0)
DECLARE_HEADPHONE_EQ_BAND_SHOW(1_B,0,1)
DECLARE_HEADPHONE_EQ_BAND_STORE(1_B,0,1)
DECLARE_HEADPHONE_EQ_BAND_SHOW(1_PG,0,2)
DECLARE_HEADPHONE_EQ_BAND_STORE(1_PG,0,2)

DECLARE_HEADPHONE_EQ_BAND_SHOW(2_A,1,0)
DECLARE_HEADPHONE_EQ_BAND_STORE(2_A,1,0)
DECLARE_HEADPHONE_EQ_BAND_SHOW(2_B,1,1)
DECLARE_HEADPHONE_EQ_BAND_STORE(2_B,1,1)
DECLARE_HEADPHONE_EQ_BAND_SHOW(2_C,1,2)
DECLARE_HEADPHONE_EQ_BAND_STORE(2_C,1,2)
DECLARE_HEADPHONE_EQ_BAND_SHOW(2_PG,1,3)
DECLARE_HEADPHONE_EQ_BAND_STORE(2_PG,1,3)

DECLARE_HEADPHONE_EQ_BAND_SHOW(3_A,2,0)
DECLARE_HEADPHONE_EQ_BAND_STORE(3_A,2,0)
DECLARE_HEADPHONE_EQ_BAND_SHOW(3_B,2,1)
DECLARE_HEADPHONE_EQ_BAND_STORE(3_B,2,1)
DECLARE_HEADPHONE_EQ_BAND_SHOW(3_C,2,2)
DECLARE_HEADPHONE_EQ_BAND_STORE(3_C,2,2)
DECLARE_HEADPHONE_EQ_BAND_SHOW(3_PG,2,3)
DECLARE_HEADPHONE_EQ_BAND_STORE(3_PG,2,3)

DECLARE_HEADPHONE_EQ_BAND_SHOW(4_A,3,0)
DECLARE_HEADPHONE_EQ_BAND_STORE(4_A,3,0)
DECLARE_HEADPHONE_EQ_BAND_SHOW(4_B,3,1)
DECLARE_HEADPHONE_EQ_BAND_STORE(4_B,3,1)
DECLARE_HEADPHONE_EQ_BAND_SHOW(4_C,3,2)
DECLARE_HEADPHONE_EQ_BAND_STORE(4_C,3,2)
DECLARE_HEADPHONE_EQ_BAND_SHOW(4_PG,3,3)
DECLARE_HEADPHONE_EQ_BAND_STORE(4_PG,3,3)

DECLARE_HEADPHONE_EQ_BAND_SHOW(5_A,4,0)
DECLARE_HEADPHONE_EQ_BAND_STORE(5_A,4,0)
DECLARE_HEADPHONE_EQ_BAND_SHOW(5_B,4,1)
DECLARE_HEADPHONE_EQ_BAND_STORE(5_B,4,1)
DECLARE_HEADPHONE_EQ_BAND_SHOW(5_PG,4,2)
DECLARE_HEADPHONE_EQ_BAND_STORE(5_PG,4,2)

static DEVICE_ATTR(stereo_expansion, S_IRUGO | S_IWUGO,
		   stereo_expansion_show,
		   stereo_expansion_store);
int wm8994_get_stereo_expansion(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.stereo_expansion ? 1 : 0;
}
int wm8994_set_stereo_expansion(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.stereo_expansion = ucontrol->value.enumerated.item[0] == 0 ? false : true;
	update_stereo_expansion(false);
    return 0;
}

static DEVICE_ATTR(stereo_expansion_gain, S_IRUGO | S_IWUGO,
		   stereo_expansion_gain_show,
		   stereo_expansion_gain_store);
int wm8994_get_stereo_expansion_gain(struct snd_kcontrol *kcontrol,
                                     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);

    return wm8994->ext.stereo_expansion_gain;
}

int wm8994_set_stereo_expansion_gain(struct snd_kcontrol *kcontrol,
                                     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int val = ucontrol->value.integer.value[0];
    DECLARE_WM8994(codec);

    if (val >= 0 && val < 32) {
        wm8994->ext.stereo_expansion_gain = val;
        update_stereo_expansion(false);
    }
    return 0;
}

static DEVICE_ATTR(mono_downmix, S_IRUGO | S_IWUGO,
		   mono_downmix_show,
		   mono_downmix_store);
int wm8994_get_mono_downmix(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
    return wm8994->ext.mono_downmix ? 1 : 0;
}
int wm8994_set_mono_downmix(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    DECLARE_WM8994(codec);
	wm8994->ext.mono_downmix = ucontrol->value.enumerated.item[0] == 0 ? false : true;
	update_mono_downmix(false);
    return 0;
}

#ifdef CONFIG_SND_WM8994_EXT_DEVELOPMENT
static DEVICE_ATTR(wm8994_register_dump, S_IRUGO,
		   show_wm8994_register_dump,
		   NULL);

static DEVICE_ATTR(wm8994_write, S_IWUSR,
		   NULL,
		   store_wm8994_write);
#endif

static DEVICE_ATTR(version, S_IRUGO,
		   wm8994_ext_version,
		   NULL);

static DEVICE_ATTR(enable, S_IRUGO | S_IWUGO,
		   enable_show,
		   enable_store);

static struct attribute *wm8994_ext_attributes[] = {
	&dev_attr_debug_log.attr,
#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
	&dev_attr_headphone_amplifier_level.attr,
#endif
#ifdef NEXUS_S
	&dev_attr_speaker_tuning.attr,
#endif
#ifdef CONFIG_SND_WM8994_EXT_FM
	&dev_attr_fm_radio_headset_restore_bass.attr,
	&dev_attr_fm_radio_headset_restore_highs.attr,
	&dev_attr_fm_radio_headset_normalize_gain.attr,
#endif
#ifdef CONFIG_SND_WM8994_EXT_RECORD_PRESETS
	&dev_attr_recording_preset.attr,
#endif
	&dev_attr_dac_osr128.attr,
	&dev_attr_adc_osr128.attr,
	&dev_attr_fll_tuning.attr,
	&dev_attr_dac_direct.attr,
	&dev_attr_digital_gain.attr,
	&dev_attr_headphone_eq.attr,
	&dev_attr_headphone_eq_b1_gain.attr,
	&dev_attr_headphone_eq_b2_gain.attr,
	&dev_attr_headphone_eq_b3_gain.attr,
	&dev_attr_headphone_eq_b4_gain.attr,
	&dev_attr_headphone_eq_b5_gain.attr,
	&dev_attr_headphone_eq_bands_values.attr,
	&dev_attr_stereo_expansion.attr,
	&dev_attr_stereo_expansion_gain.attr,
	&dev_attr_mono_downmix.attr,
#ifdef CONFIG_SND_WM8994_EXT_DEVELOPMENT
	&dev_attr_wm8994_register_dump.attr,
	&dev_attr_wm8994_write.attr,
#endif
	&dev_attr_version.attr,
	NULL
};

static struct attribute *wm8994_ext_control_attributes[] = {
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group wm8994_ext_group = {
	.attrs = wm8994_ext_attributes,
};

static struct attribute_group wm8994_ext_control_group = {
	.attrs = wm8994_ext_control_attributes,
};

static struct miscdevice wm8994_ext_device = {
	.minor = MISC_DYNAMIC_MINOR,
//	.name = "wm8994_ext",
	.name = "voodoo_sound",
};

static struct miscdevice wm8994_ext_control_device = {
	.minor = MISC_DYNAMIC_MINOR,
//	.name = "wm8994_ext_control",
	.name = "voodoo_sound_control",
};

void wm8994_ext_pcm_remove()
{
	printk("wm8994_ext: removing driver v%d\n", WM8994_EXT_VERSION);
	sysfs_remove_group(&wm8994_ext_device.this_device->kobj,
			   &wm8994_ext_group);
	misc_deregister(&wm8994_ext_device);
}

void update_enable()
{
    DECLARE_WM8994(codec);
	if (wm8994->ext.enable) {
		printk("wm8994_ext: initializing driver v%d\n",
		       WM8994_EXT_VERSION);

#ifdef CONFIG_SND_WM8994_EXT_DEVELOPMENT
		printk("wm8994_ext: codec development tools enabled\n");
#endif

		misc_register(&wm8994_ext_device);
		if (sysfs_create_group(&wm8994_ext_device.this_device->kobj,
				       &wm8994_ext_group) < 0) {
			printk("%s sysfs_create_group fail\n", __FUNCTION__);
			pr_err("Failed to create sysfs group for (%s)!\n",
			       wm8994_ext_device.name);
		}
	} else
		wm8994_ext_pcm_remove();
}

/*
 *
 * Driver Hooks
 *
 */

#ifdef CONFIG_SND_WM8994_EXT_FM
void wm8994_ext_fmradio_headset()
{
	// global kill switch
	if (!enable)
		return;

	if (!fm_radio_headset_restore_bass
	    && !fm_radio_headset_restore_highs
	    && !fm_radio_headset_normalize_gain)
		return;

	update_fm_radio_headset_restore_freqs(false);
	update_fm_radio_headset_normalize_gain(false);
}
#endif

#ifdef CONFIG_SND_WM8994_EXT_RECORD_PRESETS
void wm8994_ext_record_main_mic()
{
    DECLARE_WM8994(codec);
	// global kill switch
	if (!wm8994->ext.enable)
		return;

	if (wm8994->ext.recording_preset == 0)
		return;

	wm8994->ext.origin_recgain = wm8994_read(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME);
	wm8994->ext.origin_recgain_mixer = wm8994_read(codec, WM8994_INPUT_MIXER_3);
	update_recording_preset(false);
}
#endif

#ifdef NEXUS_S
void wm8994_ext_playback_speaker()
{
    DECLARE_WM8994(codec);
	// global kill switch
	if (!wm8994->ext.enable)
		return;
	if (!wm8994->ext.speaker_tuning)
		return;

	update_speaker_tuning(false);
}
#endif

unsigned int wm8994_ext_write(struct snd_soc_codec *codec_,
				      unsigned int reg, unsigned int value)
{
	DECLARE_WM8994(codec_);

	// global kill switch
	if (!wm8994->ext.enable)
		return value;

	// modify some registers before those being written to the codec
	// be sure our pointer to codec is up to date
	codec = codec_;

	if (!wm8994->ext.bypass_write_extension) {

#ifdef CONFIG_SND_WM8994_EXT_HP_LEVEL_CONTROL
		if (is_path(HEADPHONES)
		    && !(wm8994->codec_state & CALL_ACTIVE)) {

			if (reg == WM8994_LEFT_OUTPUT_VOLUME)
				value =
				    (WM8994_HPOUT1_VU |
				     WM8994_HPOUT1L_MUTE_N |
				     hpvol(0));

			if (reg == WM8994_RIGHT_OUTPUT_VOLUME)
				value =
				    (WM8994_HPOUT1_VU |
				     WM8994_HPOUT1R_MUTE_N |
				     hpvol(1));
		}
#endif

#ifdef CONFIG_SND_WM8994_EXT_FM
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
		// FM tuning virtual hook for Gingerbread
		if (is_path(RADIO_HEADPHONES)) {
			if (reg == WM8994_AIF2_DRC_1
			    || reg == WM8994_AIF2_DAC_FILTERS_1)
				wm8994_ext_fmradio_headset();
		}
#else
		// FM tuning virtual hook for Froyo
		if (is_path(RADIO_HEADPHONES)) {
			if (reg == WM8994_INPUT_MIXER_2
			    || reg == WM8994_AIF2_DRC_1
			    || reg == WM8994_ANALOGUE_HP_1)
				wm8994_ext_fmradio_headset();
		}
#endif
#endif
		// global Oversampling tuning
		if (reg == WM8994_OVERSAMPLING)
			value = osr128_get_value(value);

		// global Anti-Jitter tuning
		if (reg == WM8994_FLL1_CONTROL_4)
			value = fll_tuning_get_value(value);

		// global Mono downmix tuning
		if (reg == WM8994_AIF1_DAC1_FILTERS_1
		    || reg == WM8994_AIF1_DAC2_FILTERS_1
		    || reg == WM8994_AIF2_DAC_FILTERS_1)
			value = mono_downmix_get_value(value, false);

		// DAC direct tuning virtual hook
		if (reg == WM8994_OUTPUT_MIXER_1
		    || reg == WM8994_OUTPUT_MIXER_2)
			value = dac_direct_get_value(value, false);

		// Digital Headroom virtual hook
		if (reg == WM8994_AIF1_DAC1_LEFT_VOLUME
		    || reg == WM8994_AIF1_DAC1_RIGHT_VOLUME)
			value = digital_gain_get_value(value);

		// Headphones EQ & 3D virtual hook
		if (reg == WM8994_AIF1_DAC1_FILTERS_1
		    || reg == WM8994_AIF1_DAC2_FILTERS_1
		    || reg == WM8994_AIF2_DAC_FILTERS_1) {
			wm8994->ext.bypass_write_extension = true;
			apply_saturation_prevention_drc();
			update_headphone_eq(false);
			update_stereo_expansion(false);
			wm8994->ext.bypass_write_extension = false;
		}

	}
	if (debug_log(LOG_VERBOSE))
	// log every write to dmesg
#ifdef NEXUS_S
		printk("wm8994_ext: codec_state=%u, stream_state=%u, "
		       "cur_path=%i, rec_path=%i, "
		       "power_state=%i\n",
		       wm8994->codec_state, wm8994->stream_state,
		       wm8994->cur_path, wm8994->rec_path,
		       wm8994->power_state);
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
		printk("wm8994_ext: wm8994_write 0x%03X 0x%04X "
		       "codec_state=%u, stream_state=%u, "
		       "cur_path=%i, rec_path=%i, "
#ifndef M110S
		       "fmradio_path=%i, fmr_mix_path=%i, "
#endif
		       "input_source=%i, "
#ifndef M110S
		       "output_source=%i, "
#endif
		       "power_state=%i\n",
		       reg, value,
		       wm8994->codec_state, wm8994->stream_state,
#ifndef M110S
		       wm8994->fmradio_path, wm8994->fmr_mix_path,
#endif
		       wm8994->cur_path, wm8994->rec_path,
		       wm8994->input_source,
#ifndef M110S
		       wm8994->output_source,
#endif
		       wm8994->power_state);
#else
		printk("wm8994_ext: wm8994_write 0x%03X 0x%04X "
		       "codec_state=%u, stream_state=%u, "
		       "cur_path=%i, rec_path=%i, "
#ifndef M110S
		       "fmradio_path=%i, fmr_mix_path=%i, "
#endif
#ifdef CONFIG_S5PC110_KEPLER_BOARD
		       "call_record_path=%i, call_record_ch=%i, "
		       "AUDIENCE_state=%i, "
		       "Fac_SUB_MIC_state=%i, TTY_state=%i, "
#endif
		       "power_state=%i, "
#ifndef M110S
		       "recognition_active=%i, ringtone_active=%i"
#endif
		       "\n",
		       reg, value,
		       wm8994->codec_state, wm8994->stream_state,
		       wm8994->cur_path, wm8994->rec_path,
#ifndef M110S
		       wm8994->fmradio_path, wm8994->fmr_mix_path,
#endif
#ifdef CONFIG_S5PC110_KEPLER_BOARD
		       wm8994->call_record_path, wm8994->call_record_ch,
		       wm8994->AUDIENCE_state,
		       wm8994->Fac_SUB_MIC_state, wm8994->TTY_state,
#endif
		       wm8994->power_state
#ifndef M110S
		       ,wm8994->recognition_active,
		       wm8994->ringtone_active
#endif
		       );
#endif
#endif
	return value;
}

void wm8994_ext_pcm_probe(struct snd_soc_codec *codec_)
{
    DECLARE_WM8994(codec_);

	// make a copy of the codec pointer
	codec = codec_;

	misc_register(&wm8994_ext_control_device);
	if (sysfs_create_group(&wm8994_ext_control_device.this_device->kobj,
			       &wm8994_ext_control_group) < 0) {
		printk("%s sysfs_create_group fail\n", __FUNCTION__);
		pr_err("Failed to create sysfs group for device (%s)!\n",
		       wm8994_ext_control_device.name);
	}

	// initialize eq_band_values[] from default codec EQ values
	load_current_eq_values();

	wm8994->ext.enable = true;
	update_enable();
}
