#pragma once

#include <pbnjson.h>
#include <stdbool.h>

#define SETTINGS_PERSISTENCE_PATH "/media/developer/apps/usr/palm/services/org.webosbrew.piccap.service/config.json"

// #define HYPERION_OLD_OKLA

typedef struct _hyperionAdjustment_t {
    char* name;
    double gain;
} hyperionAdjustment_t;

typedef struct _hyperionAdjustments_t {
    char* hdr_type;
    hyperionAdjustment_t** adjustments;
    unsigned int adjustments_count;
} hyperionAdjustments_t;

// Settings stored in config.json file

typedef struct _settings_t {
    char* video_backend;
    char* ui_backend;

    char* address;
    char* ipaddress;
    int port;
    int priority;
    bool unix_socket;

    int fps;
    int width;
    int height;
#ifdef HYPERION_OLD_OKLA
    double brightnessGain;
    double saturationGain;
    double defaultBrightnessGain;
    double defaultSaturationGain;
#else
    bool hyperion_adjustments;
    hyperionAdjustments_t** adjustments;
    unsigned int adjustments_count;
#endif
    bool vsync;
    int quirks;

    bool no_video;
    bool no_gui;

    bool autostart;

    bool dump_frames;
} settings_t;

void settings_init(settings_t*);

int settings_load_json(settings_t* settings, jvalue_ref source);
int settings_save_json(settings_t* settings, jvalue_ref target);

int settings_load_file(settings_t* settings, char* source);
int settings_save_file(settings_t* settings, char* target);
