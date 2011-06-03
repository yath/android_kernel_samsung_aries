/*
 * wm8994_ext.h  --  WM8994 Soc Audio driver extensions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _WM8994_EXT_H
#define _WM8994_EXT_H

#define WM8994_EXT_VERSION 9

#if defined(CONFIG_MACH_HERRING) || defined (CONFIG_SAMSUNG_GALAXYS)	       \
	|| defined (CONFIG_SAMSUNG_GALAXYSB)				       \
	|| defined (CONFIG_SAMSUNG_CAPTIVATE)				       \
	|| defined (CONFIG_SAMSUNG_VIBRANT)				       \
	|| defined (CONFIG_SAMSUNG_FASCINATE)				       \
	|| defined (CONFIG_SAMSUNG_EPIC)
#define NEXUS_S
#endif

#ifdef CONFIG_FB_S3C_AMS701KA
#define GALAXY_TAB
#endif

#ifdef CONFIG_M110S
#define M110S
#endif

#ifdef CONFIG_TDMB_T3700
#define M110S
#endif

struct wm8994_ext_priv {
    int enable;
    bool bypass_write_extension;
    short unsigned int debug_log_level;
#ifdef CONFIG_SND_WM8994_EXT_FM
    bool fm_radio_headset_restore_bass;
    bool fm_radio_headset_restore_highs;
    bool fm_radio_headset_normalize_gain;
#endif
    
#ifdef CONFIG_SND_WM8994_EXT_RECORD_PRESETS
    unsigned short recording_preset;
    unsigned short origin_recgain;
    unsigned short origin_recgain_mixer;
#endif
    
#ifdef NEXUS_S
    bool speaker_tuning;
#endif
    bool dac_osr128;
    bool adc_osr128;
    bool fll_tuning;
    bool dac_direct;
    bool mono_downmix;
    // digital gain value in mili dB
    int digital_gain;
    bool headphone_eq;
    // 3D effect
    bool stereo_expansion;
    short unsigned int stereo_expansion_gain;
    unsigned short hp_level[2];
    short eq_gains[5];
    unsigned int eq_band_values[5][4];
};

enum debug_log { LOG_OFF, LOG_INFOS, LOG_VERBOSE };
bool debug_log(short unsigned int level);

enum unified_path { HEADPHONES, RADIO_HEADPHONES, SPEAKER, MAIN_MICROPHONE };

bool is_path(int unified_path);
bool is_path_media_or_fm_no_call_no_record(void);
unsigned int wm8994_ext_write(struct snd_soc_codec *codec,
				      unsigned int reg, unsigned int value);
void wm8994_ext_fmradio_headset(void);
void wm8994_ext_pcm_probe(struct snd_soc_codec *codec);
void wm8994_ext_pcm_remove(void);
void wm8994_ext_record_main_mic(void);
void wm8994_ext_playback_speaker(void);

void load_current_eq_values(void);
void apply_saturation_prevention_drc(void);

void update_hpvol(bool with_fade);
void update_fm_radio_headset_restore_freqs(bool with_mute);
void update_fm_radio_headset_normalize_gain(bool with_mute);
void update_recording_preset(bool with_mute);
void update_full_bitwidth(bool with_mute);
void update_osr128(bool with_mute);
void update_fll_tuning(bool with_mute);
void update_mono_downmix(bool with_mute);
void update_dac_direct(bool with_mute);
void update_digital_gain(bool with_mute);
void update_stereo_expansion(bool with_mute);
void update_headphone_eq(bool with_mute);
void update_enable(void);

/* ALSA mixers */
int wm8994_get_speaker_tuning(struct snd_kcontrol *kcontrol,
                              struct snd_ctl_elem_value *ucontrol);
int wm8994_set_speaker_tuning(struct snd_kcontrol *kcontrol,
                              struct snd_ctl_elem_value *ucontrol);
int wm8994_get_fm_radio_headset_restore_bass(struct snd_kcontrol *kcontrol,
                                             struct snd_ctl_elem_value *ucontrol);
int wm8994_set_fm_radio_headset_restore_bass(struct snd_kcontrol *kcontrol,
                                             struct snd_ctl_elem_value *ucontrol);
int wm8994_get_fm_radio_headset_restore_highs(struct snd_kcontrol *kcontrol,
                                              struct snd_ctl_elem_value *ucontrol);
int wm8994_set_fm_radio_headset_restore_highs(struct snd_kcontrol *kcontrol,
                                              struct snd_ctl_elem_value *ucontrol);
int wm8994_get_fm_radio_headset_normalize_gain(struct snd_kcontrol *kcontrol,
                                               struct snd_ctl_elem_value *ucontrol);
int wm8994_set_fm_radio_headset_normlize_gain(struct snd_kcontrol *kcontrol,
                                              struct snd_ctl_elem_value *ucontrol);
int wm8994_get_output_oversampling(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol);
int wm8994_set_output_oversampling(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol);
int wm8994_get_input_oversampling(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol);
int wm8994_set_input_oversampling(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol);
int wm8994_get_fll_tuning(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_value *ucontrol);
int wm8994_set_fll_tuning(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_value *ucontrol);
int wm8994_get_dac_direct(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_value *ucontrol);
int wm8994_set_dac_direct(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_value *ucontrol);
int wm8994_get_headphone_eq(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);
int wm8994_set_headphone_eq(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);
int wm8994_get_stereo_expansion(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol);
int wm8994_set_stereo_expansion(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol);
int wm8994_get_mono_downmix(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);
int wm8994_set_mono_downmix(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);
int wm8994_get_headphone_amplifier_level(struct snd_kcontrol *kcontrol,
                                         struct snd_ctl_elem_value *ucontrol);
int wm8994_set_headphone_amplifier_level(struct snd_kcontrol *kcontrol,
                                         struct snd_ctl_elem_value *ucontrol);
int wm8994_get_recording_preset(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol);
int wm8994_set_recording_preset(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol);

int wm8994_get_digital_gain(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);
int wm8994_set_digital_gain(struct snd_kcontrol *kcontrol,
                            struct snd_ctl_elem_value *ucontrol);
#endif
