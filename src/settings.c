#include "settings.h"
#include "log.h"
#include <stdio.h>

void settings_init(settings_t* settings)
{
    settings->ui_backend = strdup("auto");
    settings->video_backend = strdup("auto");

    settings->address = strdup("");
    settings->ipaddress = strdup("");
    settings->port = 19400;
    settings->priority = 150;
    settings->unix_socket = false;

    settings->fps = 30;
    settings->width = 320;
    settings->height = 180;
    settings->quirks = 0;
#ifdef HYPERION_OLD_OKLA
    settings->brightnessGain = 1.0;
    settings->saturationGain = 1.0;
    settings->defaultBrightnessGain = 1.0;
    settings->defaultSaturationGain = 1.0;
#else
    settings->hyperion_adjustments = true;

#endif

    settings->no_video = false;
    settings->no_gui = false;
    settings->autostart = false;
    settings->vsync = true;

    settings->dump_frames = false;
}

#ifndef HYPERION_OLD_OKLA
int settings_hyperion_load(settings_t* settings, jvalue_ref source)
{
    jvalue_ref value;
    if ((value = jobject_get(source, j_cstr_to_buffer("hyperion"))) && jis_object(value)) {
        jvalue_ref value2;
        // jboolean_get(value, &settings->hyperion_adjustments);

        if ((value2 = jobject_get(value, j_cstr_to_buffer("enabled"))) && jis_boolean(value2))
            jboolean_get(value2, &settings->hyperion_adjustments);

        INFO("Hyperion settings: %s", &settings->hyperion_adjustments ? "enabled" : "disabled");
        if ((value2 = jobject_get(value, j_cstr_to_buffer("adjustments"))) && jis_object(value)) {
            jobject_iter it;
            jobject_key_value key_value;
            jobject_iter_init(&it, value2);

            int adjustmentsSize = jobject_size(value2);

            hyperionAdjustments_t** adjustments = malloc(sizeof(hyperionAdjustments_t) * adjustmentsSize);

            INFO("Hyperion settings adjustments (%d): ", adjustmentsSize);
            int j = 0;

            while (jobject_iter_next(&it, &key_value)) {
                if (jis_string(key_value.key) && jis_object(key_value.value)) {

                    raw_buffer str = jstring_get(key_value.key);

                    jobject_iter it2;
                    jobject_key_value key_value2;
                    jobject_iter_init(&it2, key_value.value);
                    char* name = strdup(str.m_str);
                    int size = jobject_size(key_value.value);
                    hyperionAdjustment_t** kvmap = malloc(sizeof(hyperionAdjustment_t) * size);

                    INFO("  adjustments %d. %s: (%d)", j, name, size);
                    int i = 0;

                    while (jobject_iter_next(&it2, &key_value2)) {
                        if (jis_number(key_value2.value) && jis_string(key_value2.key)) {
                            kvmap[i] = malloc(sizeof(hyperionAdjustment_t));

                            raw_buffer str = jstring_get(key_value2.key);

                            jnumber_get_f64(key_value2.value, &kvmap[i]->gain);
                            kvmap[i]->name = strdup(str.m_str);
                            INFO("    %d. %s: %f", i, kvmap[i]->name, kvmap[i]->gain);
                            i++;
                        }
                    }
                    adjustments[j] = malloc(sizeof(hyperionAdjustments_t));
                    adjustments[j]->hdr_type = strdup(name);
                    adjustments[j]->adjustments = kvmap;
                    adjustments[j]->adjustments_count = size;
                    j++;
                }
            }
            settings->adjustments = adjustments;
            settings->adjustments_count = adjustmentsSize;
        }
    }
    for (unsigned int i = 0; i < settings->adjustments_count; i++) {
        DBG("adjustments %d. %s", i, settings->adjustments[i]->hdr_type);
        for (unsigned int j = 0; j < settings->adjustments[i]->adjustments_count; j++) {
            DBG("       %s: %f", settings->adjustments[i]->adjustments[j]->name, settings->adjustments[i]->adjustments[j]->gain);
        }
    }
    return 0;
}
#endif
int settings_load_json(settings_t* settings, jvalue_ref source)
{
    jvalue_ref value;

    if ((value = jobject_get(source, j_cstr_to_buffer("backend"))) && jis_string(value)) {
        free(settings->video_backend);
        raw_buffer str = jstring_get(value);
        settings->video_backend = strdup(str.m_str);
        jstring_free_buffer(str);
    }

    if ((value = jobject_get(source, j_cstr_to_buffer("uibackend"))) && jis_string(value)) {
        free(settings->ui_backend);
        raw_buffer str = jstring_get(value);
        settings->ui_backend = strdup(str.m_str);
        jstring_free_buffer(str);
    }

    if ((value = jobject_get(source, j_cstr_to_buffer("address"))) && jis_string(value)) {
        free(settings->address);
        raw_buffer str = jstring_get(value);
        settings->address = strdup(str.m_str);
        jstring_free_buffer(str);

        if (settings->unix_socket) {
            settings->ipaddress = "127.0.0.1";
        } else {
            settings->ipaddress = strdup(settings->address);
        }
    }
    if ((value = jobject_get(source, j_cstr_to_buffer("port"))) && jis_number(value))
        jnumber_get_i32(value, &settings->port);
    if ((value = jobject_get(source, j_cstr_to_buffer("priority"))) && jis_number(value))
        jnumber_get_i32(value, &settings->priority);
    if ((value = jobject_get(source, j_cstr_to_buffer("unix_socket"))) && jis_boolean(value))
        jboolean_get(value, &settings->unix_socket);

    if ((value = jobject_get(source, j_cstr_to_buffer("fps"))) && jis_number(value))
        jnumber_get_i32(value, &settings->fps);
    if ((value = jobject_get(source, j_cstr_to_buffer("width"))) && jis_number(value))
        jnumber_get_i32(value, &settings->width);
    if ((value = jobject_get(source, j_cstr_to_buffer("height"))) && jis_number(value))
        jnumber_get_i32(value, &settings->height);
    if ((value = jobject_get(source, j_cstr_to_buffer("quirks"))) && jis_number(value))
        jnumber_get_i32(value, &settings->quirks);

#ifdef HYPERION_OLD_OKLA
    if ((value = jobject_get(source, j_cstr_to_buffer("brightnessGain"))) && jis_number(value))
        jnumber_get_f64(value, &settings->brightnessGain);
    if ((value = jobject_get(source, j_cstr_to_buffer("saturationGain"))) && jis_number(value))
        jnumber_get_f64(value, &settings->saturationGain);
    if ((value = jobject_get(source, j_cstr_to_buffer("defaultBrightnessGain"))) && jis_number(value))
        jnumber_get_f64(value, &settings->defaultBrightnessGain);
    if ((value = jobject_get(source, j_cstr_to_buffer("defaultSaturationGain"))) && jis_number(value))
        jnumber_get_f64(value, &settings->defaultSaturationGain);
#else
    settings_hyperion_load(settings, source);
#endif

    if ((value = jobject_get(source, j_cstr_to_buffer("vsync"))) && jis_boolean(value))
        jboolean_get(value, &settings->vsync);
    if ((value = jobject_get(source, j_cstr_to_buffer("novideo"))) && jis_boolean(value))
        jboolean_get(value, &settings->no_video);
    if ((value = jobject_get(source, j_cstr_to_buffer("nogui"))) && jis_boolean(value))
        jboolean_get(value, &settings->no_gui);
    if ((value = jobject_get(source, j_cstr_to_buffer("autostart"))) && jis_boolean(value))
        jboolean_get(value, &settings->autostart);

    // settings_save_file(settings, "/tmp/newconfig.json");
    // DBG("saved");

    for (unsigned int i = 0; i < settings->adjustments_count; i++) {
        DBG("adjustments %d. %s", i, settings->adjustments[i]->hdr_type);
        for (unsigned int j = 0; j < settings->adjustments[i]->adjustments_count; j++) {
            DBG("       %s: %f", settings->adjustments[i]->adjustments[j]->name, settings->adjustments[i]->adjustments[j]->gain);
        }
    }
    return 0;
}

#ifndef HYPERION_OLD_OKLA
int settings_hyperion_save_json(settings_t* settings, jvalue_ref target)
{
    DBG("saving hyperion settings");
    jvalue_ref hyperion_body = jobject_create();
    jvalue_ref adjustments_jobj = jobject_create();
    for (unsigned int i = 0; i < settings->adjustments_count; i++) {
        DBG("adjustments %d. %s", i, settings->adjustments[i]->hdr_type);
        jvalue_ref adjustment_jobj = jobject_create();
        for (unsigned int j = 0; j < settings->adjustments[i]->adjustments_count; j++) {
            jobject_set(adjustment_jobj, j_cstr_to_buffer(settings->adjustments[i]->adjustments[j]->name), jnumber_create_f64(settings->adjustments[i]->adjustments[j]->gain));
        }
        jobject_set(adjustments_jobj, j_cstr_to_buffer(settings->adjustments[i]->hdr_type), adjustment_jobj);
    }

    // Assemble top-level json
    jobject_set(hyperion_body, j_cstr_to_buffer("enabled"), jboolean_create(settings->hyperion_adjustments));
    jobject_set(hyperion_body, j_cstr_to_buffer("adjustments"), adjustments_jobj);

    jobject_set(target, j_cstr_to_buffer("hyperion"), hyperion_body);
    DBG("saving hyperion settings done");
    return 0;
}
#endif

int settings_save_json(settings_t* settings, jvalue_ref target)
{
    jobject_set(target, j_cstr_to_buffer("backend"), jstring_create(settings->video_backend));
    jobject_set(target, j_cstr_to_buffer("uibackend"), jstring_create(settings->ui_backend));

    jobject_set(target, j_cstr_to_buffer("address"), jstring_create(settings->address));
    jobject_set(target, j_cstr_to_buffer("port"), jnumber_create_i32(settings->port));
    jobject_set(target, j_cstr_to_buffer("priority"), jnumber_create_i32(settings->priority));
    jobject_set(target, j_cstr_to_buffer("unix_socket"), jboolean_create(settings->unix_socket));

    jobject_set(target, j_cstr_to_buffer("fps"), jnumber_create_i32(settings->fps));
    jobject_set(target, j_cstr_to_buffer("width"), jnumber_create_i32(settings->width));
    jobject_set(target, j_cstr_to_buffer("height"), jnumber_create_i32(settings->height));
    jobject_set(target, j_cstr_to_buffer("quirks"), jnumber_create_i32(settings->quirks));
#ifdef HYPERION_OLD_OKLA
    jobject_set(target, j_cstr_to_buffer("brightnessGain"), jnumber_create_f64(settings->brightnessGain));
    jobject_set(target, j_cstr_to_buffer("saturationGain"), jnumber_create_f64(settings->saturationGain));
    jobject_set(target, j_cstr_to_buffer("defaultBrightnessGain"), jnumber_create_f64(settings->defaultBrightnessGain));
    jobject_set(target, j_cstr_to_buffer("defaultSaturationGain"), jnumber_create_f64(settings->defaultSaturationGain));
#else
    settings_hyperion_save_json(settings, target);
#endif
    jobject_set(target, j_cstr_to_buffer("vsync"), jboolean_create(settings->vsync));
    jobject_set(target, j_cstr_to_buffer("novideo"), jboolean_create(settings->no_video));
    jobject_set(target, j_cstr_to_buffer("nogui"), jboolean_create(settings->no_gui));
    jobject_set(target, j_cstr_to_buffer("autostart"), jboolean_create(settings->autostart));

    return 0;
}

int settings_load_file(settings_t* settings, char* source)
{
    int ret = 0;

    JSchemaInfo schema;
    jvalue_ref parsed;

    FILE* f = fopen(source, "rb");
    // File read failed
    if (f == NULL) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* json = calloc(fsize + 1, 1);
    fread(json, fsize, 1, f);
    fclose(f);

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(json), DOMOPT_NOOPT, &schema);

    // Parsing failed
    if (jis_null(parsed)) {
        free(json);
        j_release(&parsed);
        return -2;
    }

    ret = settings_load_json(settings, parsed);

    free(json);
    j_release(&parsed);
    return ret;
}

int settings_save_file(settings_t* settings, char* target)
{
    jvalue_ref jobj = jobject_create();
    int ret = settings_save_json(settings, jobj);

    if (ret != 0) {
        j_release(&jobj);
        return -2;
    }

    FILE* fd = fopen(target, "wb");
    if (fd == NULL) {
        j_release(&jobj);
        return -1;
    }

    fputs(jvalue_tostring_simple(jobj), fd);
    fclose(fd);

    j_release(&jobj);
    return 0;
}
