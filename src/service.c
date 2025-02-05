#include "service.h"
#include "hyperion_client.h"
#include "json_rpc_client.h"
#include "log.h"
#include "pthread.h"
#include "settings.h"
#include "toast.h"
#include "unicapture.h"
#include "version.h"
#include <errno.h>
#include <luna-service2/lunaservice.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

// This is a deprecated symbol present in meta-lg-webos-ndk but missing in
// latest buildroot NDK. It is required for proper public service registration
// before webOS 3.5.
//
// SECURITY_COMPATIBILITY flag present in CMakeList disables deprecation notices, see:
// https://github.com/webosose/luna-service2/blob/b74b1859372597fcd6f0f7d9dc3f300acbf6ed6c/include/public/luna-service2/lunaservice.h#L49-L53
bool LSRegisterPubPriv(const char* name, LSHandle** sh,
    bool public_bus,
    LSError* lserror) __attribute__((weak));

void* connection_loop(void* data)
{
    service_t* service = (service_t*)data;
    DBG("Starting connection loop");
    while (service->connection_loop_running) {
        if (service->unicapture.video_capture_running || service->unicapture.ui_capture_running) {
            INFO("Connecting hyperion-client..");
            if ((hyperion_client("webos", service->settings->address, service->settings->port,
                    service->settings->unix_socket, service->settings->priority))
                != 0) {
                ERR("Error! hyperion_client.");
            } else {
                INFO("hyperion-client connected!");
                service->connected = true;
                while (service->connection_loop_running && service->connected) {
                    int ret = hyperion_read();
                    if (ret == -11) {
                        INFO("no data to read, waiting...");
                        // usleep(100);
                    } else if (ret < 0) {
                        ERR("Error (%d)! Connection timeout.", ret);
                        break;
                    }
                }
                service->connected = false;
            }

            hyperion_destroy();

            if (service->connection_loop_running) {
                INFO("Connection destroyed, waiting...");
                sleep(1);
            }
        } else {
            usleep(1000);
        }
    }

    INFO("Ending connection loop");
    DBG("Connection loop exiting");
    return 0;
}

int service_feed_frame(void* data __attribute__((unused)), int width, int height, uint8_t* rgb_data)
{
    // service_t* service = (service_t*)data;
    int ret;
    if ((ret = hyperion_set_image(rgb_data, width, height)) != 0) {
        WARN("Frame sending failed: %d", ret);
        // service->connected = false;
        // hyperion_destroy();
    }

    return 0;
}

int service_change_priority(int priority)
{
    if (priority && getuid() == 0) {
        INFO("Setting priority to %d.", priority);
        if (setpriority(PRIO_PROCESS, getpid(), priority)) {
            WARN("Failed to change priority to %d: %s (%d)", priority, strerror(errno), errno);
            return 1;
        }
        return 0;
    }
    return 1;
}

int service_init(service_t* service, settings_t* settings)
{
    cap_backend_config_t config;
    config.resolution_width = settings->width;
    config.resolution_height = settings->height;
    config.fps = settings->fps;
    config.quirks = settings->quirks;

    service->settings = settings;

    service->unicapture.lut_table_file = settings->lut_table;
    unicapture_init(&service->unicapture);
    service->unicapture.vsync = settings->vsync;
    service->unicapture.fps = settings->fps;
    service->unicapture.callback = &service_feed_frame;
    service->unicapture.callback_data = (void*)service;
    service->unicapture.dump_frames = settings->dump_frames;
    service->unicapture.lut_table_file = settings->lut_table;

    unicapture_load_lut_table(&service->unicapture);

    service_change_priority(settings->process_priority);

    char* ui_backends[] = { "libgm_backend.so", "libhalgal_backend.so", NULL };
    char* video_backends[] = { "libvtcapture_backend.so", "libdile_vt_backend.so", NULL };
    char backend_name[FILENAME_MAX] = { 0 };

    service->unicapture.ui_capture = NULL;

    if (settings->no_gui) {
        INFO("UI capture disabled");
    } else {
        if (settings->ui_backend == NULL || strcmp(settings->ui_backend, "") == 0 || strcmp(settings->ui_backend, "auto") == 0) {
            INFO("Autodetecting UI backend...");
            if (unicapture_try_backends(&config, &service->ui_backend, ui_backends) == 0) {
                service->unicapture.ui_capture = &service->ui_backend;
            }
        } else {
            snprintf(backend_name, sizeof(backend_name), "%s_backend.so", settings->ui_backend);
            if (unicapture_init_backend(&config, &service->ui_backend, backend_name) == 0) {
                service->unicapture.ui_capture = &service->ui_backend;
            }
        }
    }

    service->unicapture.video_capture = NULL;

    if (settings->no_video) {
        INFO("Video capture disabled");
    } else {
        if (settings->video_backend == NULL || strcmp(settings->video_backend, "") == 0 || strcmp(settings->video_backend, "auto") == 0) {
            INFO("Autodetecting video backend...");
            if (unicapture_try_backends(&config, &service->video_backend, video_backends) == 0) {
                service->unicapture.video_capture = &service->video_backend;
            }
        } else {
            snprintf(backend_name, sizeof(backend_name), "%s_backend.so", settings->video_backend);
            if (unicapture_init_backend(&config, &service->video_backend, backend_name) == 0) {
                service->unicapture.video_capture = &service->video_backend;
            }
        }
    }

    return 0;
}

int service_destroy(service_t* service)
{
    int ret = service_stop(service);
    INFO("Cleaning UI capture...");
    if (service->unicapture.ui_capture != NULL && service->unicapture.ui_capture->initialized) {
        service->unicapture.ui_capture->cleanup(service->unicapture.ui_capture->state);
        service->unicapture.ui_capture->initialized = false;
    }

    INFO("Cleaning Video capture...");
    if (service->unicapture.video_capture != NULL && service->unicapture.video_capture->initialized) {
        service->unicapture.video_capture->cleanup(service->unicapture.video_capture->state);
        service->unicapture.video_capture->initialized = false;
    }

    return ret;
}

int service_start(service_t* service)
{
    if (service->running) {
        return 1;
    }

    service->running = true;
    unicapture_start(&service->unicapture);
    /*
    int res = unicapture_start(&service->unicapture);

    if (res != 0) {
        service->running = false;
        return res;
    }
*/
    service->connection_loop_running = true;
    if (pthread_create(&service->connection_thread, NULL, connection_loop, service) != 0) {
        unicapture_stop(&service->unicapture);
        service->connection_loop_running = false;
        service->running = false;
        return -1;
    }
    if (service->settings->notifications)
        createToast(service, "Capture started");

    return 0;
}

int service_stop(service_t* service)
{
    if (!service->running) {
        return 1;
    }

    service->connection_loop_running = false;
    unicapture_stop(&service->unicapture);
    pthread_join(service->connection_thread, NULL);
    service->running = false;
    if (service->settings->notifications)
        createToast(service, "Capture stopped");
    return 0;
}

bool service_method_start(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref jobj = jobject_create();

    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(service_start(service) == 0));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);
    return false;
}

bool service_method_stop(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref jobj = jobject_create();

    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(service_stop(service) == 0));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);
    return false;
}

bool service_method_startorstop(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref jobj = jobject_create();

    bool ret = service->running ? service_stop(service) : service_start(service);
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(ret == 0));
    jobject_set(jobj, j_cstr_to_buffer("isRunning"), jboolean_create(service->running));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);
    return false;
}

bool service_method_status(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    jvalue_ref jobj = jobject_create();
    LSError lserror;
    LSErrorInit(&lserror);

    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(true));
    jobject_set(jobj, j_cstr_to_buffer("elevated"), jboolean_create(getuid() == 0));
    jobject_set(jobj, j_cstr_to_buffer("version"), jstring_create(HYPERION_WEBOS_VERSION));
    jobject_set(jobj, j_cstr_to_buffer("isRunning"), jboolean_create(service->running));
    jobject_set(jobj, j_cstr_to_buffer("connected"), jboolean_create(service->connected));
    jobject_set(jobj, j_cstr_to_buffer("videoBackend"), service->video_backend.name ? jstring_create(service->video_backend.name) : jnull());
    jobject_set(jobj, j_cstr_to_buffer("videoRunning"), jboolean_create(service->unicapture.video_capture_running));
    jobject_set(jobj, j_cstr_to_buffer("uiBackend"), service->ui_backend.name ? jstring_create(service->ui_backend.name) : jnull());
    jobject_set(jobj, j_cstr_to_buffer("uiRunning"), jboolean_create(service->unicapture.ui_capture_running));
    jobject_set(jobj, j_cstr_to_buffer("framerate"), jnumber_create_f64(service->unicapture.metrics.framerate));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);

    return true;
}

bool service_method_get_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref jobj = jobject_create();

    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(true));
    settings_save_json(service->settings, jobj);

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);

    return true;
}

bool service_method_set_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    LSError lserror;
    LSErrorInit(&lserror);

    JSchemaInfo schema;
    jvalue_ref parsed;

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)), DOMOPT_NOOPT, &schema);

    if (jis_null(parsed)) {
        j_release(&parsed);
        return false;
    }

    int res = settings_load_json(service->settings, parsed);
    if (res == 0) {
        if (settings_save_file(service->settings, SETTINGS_PERSISTENCE_PATH) != 0) {
            WARN("Settings save failed");
        }

        const char* startup_directory = "/var/lib/webosbrew/init.d";
        const char* startup_symlink = "/var/lib/webosbrew/init.d/piccapautostart";
        const char* startup_script = "/media/developer/apps/usr/palm/services/org.webosbrew.piccap.service/piccapautostart";

        if (unlink(startup_symlink) != 0 && errno != ENOENT) {
            WARN("Startup symlink removal failed: %s", strerror(errno));
        }

        if (service->settings->autostart) {
            mkdir(startup_directory, 0755);
            if (symlink(startup_script, startup_symlink) != 0) {
                WARN("Startup symlink creation failed: %s", strerror(errno));
            }
        }
    }

    if (service_destroy(service) == 0) {
        service_init(service, service->settings);
        service_start(service);
    } else {
        service_init(service, service->settings);
    }

    jvalue_ref jobj = jobject_create();
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(res == 0));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&parsed);
    j_release(&jobj);

    return true;
}

LSMethod methods[] = {
    { "start", service_method_start, LUNA_METHOD_FLAGS_NONE },
    { "stop", service_method_stop, LUNA_METHOD_FLAGS_NONE },
    { "startorstop", service_method_startorstop, LUNA_METHOD_FLAGS_NONE },
    { "status", service_method_status, LUNA_METHOD_FLAGS_NONE },
    { "getSettings", service_method_get_settings, LUNA_METHOD_FLAGS_NONE },
    { "setSettings", service_method_set_settings, LUNA_METHOD_FLAGS_NONE },
    { 0, 0, 0 }
};

static bool power_callback(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    JSchemaInfo schema;
    jvalue_ref parsed;
    service_t* service = (service_t*)data;

    INFO("Power status callback message: %s", LSMessageGetPayload(msg));

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)), DOMOPT_NOOPT, &schema);

    // Parsing failed
    if (jis_null(parsed)) {
        j_release(&parsed);
        return true;
    }

    jvalue_ref state_ref = jobject_get(parsed, j_cstr_to_buffer("state"));
    if (!jis_valid(state_ref)) {
        DBG("power_callback: luna-reply does not contain 'state'");
        j_release(&parsed);
        return true;
    }

    raw_buffer state_buf = jstring_get(state_ref);
    const char* state_str = state_buf.m_str;
    bool target_state = strcmp(state_str, "Active") == 0;
    bool processing = jobject_containskey(parsed, j_cstr_to_buffer("processing"));

    if (!service->running && target_state && service->power_paused && !processing) {
        INFO("Resuming after power pause...");
        service->power_paused = false;
        if (service->video_connected || service->unicapture.ui_capture != NULL) {
            service_start(service);
        }
    }

    if (service->running && !target_state && !service->power_paused && !processing) {
        INFO("Pausing due to power event...");
        service->power_paused = true;
        service_stop(service);
    }

    jstring_free_buffer(state_buf);
    j_release(&parsed);

    return true;
}

static int hdr_callback(const char* hdr_type, bool hdr_enabled, void* data)
{
    service_t* service = (service_t*)data;
    char* address = service->settings->unix_socket ? "127.0.0.1" : service->settings->address;

    int ret = set_hdr_state(address, RPC_PORT, hdr_enabled);
    if (ret != 0) {
        ERR("hdr_callback: set_hdr_state failed, ret: %d", ret);
    }

#ifdef HYPERION_OLD_OKLA
    ret = set_bri_sat(address, RPC_PORT, hdr_enabled ? service->settings->brightnessGain : service->settings->defaultBrightnessGain, hdr_enabled ? service->settings->saturationGain : service->settings->defaultSaturationGain);
    if (ret != 0) {
        ERR("hdr_callback: set_bri_sat failed, ret: %d", ret);
    }
#else
    if (service->settings->hyperion.hyperion_adjustments) {
        // DBG("hdr_callback looking for adjustments");
        const char* s_hdr_type = hdr_enabled ? "hdr" : "sdr";
        hyperionAdjustments_t* def_adj = malloc(sizeof(hyperionAdjustments_t));
        def_adj->hdr_type = NULL;

        for (unsigned int i = 0; i < service->settings->hyperion.adjustments_count; i++) {
            if (hdr_enabled && def_adj->hdr_type == NULL && strcasecmp(s_hdr_type, service->settings->hyperion.adjustments[i]->hdr_type) == 0) {
                DBG("Found adjustment for '%s'.", s_hdr_type);
                def_adj = service->settings->hyperion.adjustments[i];
            }
            if (strcasecmp(hdr_type, service->settings->hyperion.adjustments[i]->hdr_type) == 0) {
                DBG("Found adjustment for '%s'.", hdr_type);
                def_adj = service->settings->hyperion.adjustments[i];
            }
        }

        if (def_adj->hdr_type != NULL) {
            DBG("hdr_callback: using adjustment '%s'.", def_adj->hdr_type);
            ret = hyperion_set_adjustments(address, RPC_PORT, def_adj, service->settings->hyperion.instances, service->settings->hyperion.instances_count);
            if (ret != 0) {
                ERR("hdr_callback: hyperion_set_adjustments failed, ret: %d", ret);
            } else {
                DBG("hdr_callback: hyperion_set_adjustments success");
            }
        } else {
            ERR("hdr_callback: didn't found adjustment for '%s' or '%s', hyperion_set_adjustments not called.", hdr_type, s_hdr_type);
        }
    }
#endif
    return ret;
}

static bool videooutput_callback(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    JSchemaInfo schema;
    jvalue_ref parsed;
    service_t* service = (service_t*)data;

    // INFO("Videooutput status callback message: %s", LSMessageGetPayload(msg));

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)), DOMOPT_NOOPT, &schema);

    // Parsing failed
    if (jis_null(parsed)) {
        j_release(&parsed);
        return false; // was true; why?
    }

    // Get to the information we want (hdrType)
    jvalue_ref video_ref = jobject_get(parsed, j_cstr_to_buffer("video"));
    if (jis_null(video_ref) || !jis_valid(video_ref)) {
        j_release(&parsed);
        return false;
    }
    jvalue_ref video_0_ref = jarray_get(video_ref, 0); // should always be index 0 = main screen ?!
    if (jis_null(video_0_ref) || !jis_valid(video_0_ref)) {
        j_release(&parsed);
        return false;
    }

    char* appid = NULL;
    jvalue_ref appid_ref = jobject_get(video_0_ref, j_cstr_to_buffer("appId"));
    if (!jis_null(appid_ref) && jis_valid(appid_ref) && jis_string(appid_ref)) {
        raw_buffer str = jstring_get(appid_ref);
        appid = strdup(str.m_str);
        jstring_free_buffer(str);
    }

    jvalue_ref video_connected_ref = jobject_get(video_0_ref, j_cstr_to_buffer("connected"));
    if (!jis_null(video_connected_ref) && jis_valid(video_connected_ref) && jis_boolean(video_connected_ref)) {
        jboolean_get(video_connected_ref, &service->video_connected);
        DBG("videooutput_callback: connected %d, appid %s", service->video_connected, appid);
        if (!service->video_connected && service->unicapture.ui_capture == NULL) {
            service_stop(service);
        } else {
            bool shouldStart = true;

            if (service->settings->included_apps.count > 0) {
                shouldStart = false;
                for (unsigned int i = 0; i < service->settings->included_apps.count; i++) {
                    if (strcmp(appid, service->settings->included_apps.apps[i]) == 0) {
                        DBG("INCLUDED APP: '%s', starting capture.", appid);
                        shouldStart = true;
                    }
                }
            } else if (service->settings->excluded_apps.count > 0) {
                shouldStart = true;
                for (unsigned int i = 0; i < service->settings->excluded_apps.count && shouldStart; i++) {
                    if (strcmp(appid, service->settings->excluded_apps.apps[i]) == 0) {
                        DBG("EXCLUDED APP: '%s', stopping capture.", appid);
                        shouldStart = false;
                    }
                }
            }
            if (shouldStart) {
                service_start(service);
            } else {
                service_stop(service);
            }
        }
    }

    jvalue_ref video_info_ref = jobject_get(video_0_ref, j_cstr_to_buffer("videoInfo"));
    if (jis_null(video_info_ref) || !jis_valid(video_info_ref)) {
        j_release(&parsed);
        return false;
    }
    jvalue_ref hdr_type_ref = jobject_get(video_info_ref, j_cstr_to_buffer("hdrType"));
    if (jis_null(hdr_type_ref) || !jis_valid(hdr_type_ref) || !jis_string(hdr_type_ref)) {
        j_release(&parsed);
        return false;
    }

    bool hdr_enabled;
    raw_buffer hdr_type_buf = jstring_get(hdr_type_ref);
    const char* hdr_type_str = hdr_type_buf.m_str;

    if (strcmp(hdr_type_str, "none") == 0) {
        INFO("videooutput_callback: hdrType: %s --> SDR mode", hdr_type_str);
        hdr_enabled = false;
    } else {
        INFO("videooutput_callback: hdrType: %s --> HDR mode", hdr_type_str);
        hdr_enabled = true;
    }
    /*

    int ret = set_hdr_state(address, RPC_PORT, hdr_enabled);
    if (ret != 0) {
        ERR("videooutput_callback: set_hdr_state failed, ret: %d", ret);
    }

#ifdef HYPERION_OLD_OKLA
    ret = set_bri_sat(address, RPC_PORT, hdr_enabled ? service->settings->brightnessGain : service->settings->defaultBrightnessGain, hdr_enabled ? service->settings->saturationGain : service->settings->defaultSaturationGain);
    if (ret != 0) {
        ERR("videooutput_callback: set_bri_sat failed, ret: %d", ret);
    }
#else
    if (service->settings->hyperion_adjustments) {
        const char* s_hdr_type = hdr_enabled ? "hdr" : "sdr";
        for (unsigned int i = 0; i < service->settings->hyperion.adjustments_count; i++) {
            if (strcasecmp(s_hdr_type, service->settings->hyperion.adjustments[i]->hdr_type) == 0) {
                ret = hyperion_set_adjustments(address, RPC_PORT, service->settings->hyperion.adjustments[i]);
                if (ret != 0) {
                    ERR("videooutput_callback: hyperion_set_adjustments failed, ret: %d", ret);
                }
            }
        }
    }
#endif
*/
    hdr_callback(hdr_type_str, hdr_enabled, service);

    jstring_free_buffer(hdr_type_buf);
    j_release(&parsed);

    return true;
}

static bool picture_callback(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    JSchemaInfo schema;
    jvalue_ref parsed;
    service_t* service = (service_t*)data;

    // INFO("getSystemSettings/picture status callback message: %s", LSMessageGetPayload(msg));

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)), DOMOPT_NOOPT, &schema);

    // Parsing failed
    if (jis_null(parsed)) {
        j_release(&parsed);
        return false; // was true; why?
    }

    // Get to the information we want (dynamicRange)
    jvalue_ref dimension_ref = jobject_get(parsed, j_cstr_to_buffer("dimension"));
    if (jis_null(dimension_ref) || !jis_valid(dimension_ref)) {
        j_release(&parsed);
        return false;
    }
    jvalue_ref dynamic_range_ref = jobject_get(dimension_ref, j_cstr_to_buffer("dynamicRange"));
    if (jis_null(dynamic_range_ref) || !jis_valid(dynamic_range_ref) || !jis_string(dynamic_range_ref)) {
        j_release(&parsed);
        return false;
    }

    bool hdr_enabled;
    raw_buffer dynamic_range_buf = jstring_get(dynamic_range_ref);
    const char* dynamic_range_str = dynamic_range_buf.m_str;

    if (strcmp(dynamic_range_str, "sdr") == 0) {
        INFO("picture_callback: dynamicRange: %s --> SDR mode", dynamic_range_str);
        hdr_enabled = false;
    } else {
        INFO("picture_callback: dynamicRange: %s --> HDR mode", dynamic_range_str);
        hdr_enabled = true;
    }
    /*
    int ret = set_hdr_state(address, RPC_PORT, hdr_enabled);
    if (ret != 0) {
        ERR("videooutput_callback: set_hdr_state failed, ret: %d", ret);
    }
#ifdef HYPERION_OLD_OKLA
    ret = set_bri_sat(address, RPC_PORT, hdr_enabled ? service->settings->brightnessGain : service->settings->defaultBrightnessGain, hdr_enabled ? service->settings->saturationGain : service->settings->defaultSaturationGain);
    if (ret != 0) {
        ERR("videooutput_callback: set_bri_sat failed, ret: %d", ret);
    }
#else
    if (service->settings->hyperion_adjustments) {
        const char* s_hdr_type = hdr_enabled ? "hdr" : "sdr";
        for (unsigned int i = 0; i < service->settings->hyperion.adjustments_count; i++) {
            if (strcasecmp(s_hdr_type, service->settings->hyperion.adjustments[i]->hdr_type) == 0) {
                ret = hyperion_set_adjustments(address, RPC_PORT, service->settings->hyperion.adjustments[i]);
                if (ret != 0) {
                    ERR("videooutput_callback: hyperion_set_adjustments failed, ret: %d", ret);
                }
            }
        }
    }
#endif
*/
    hdr_callback(dynamic_range_str, hdr_enabled, service);

    jstring_free_buffer(dynamic_range_buf);
    j_release(&parsed);

    return true;
}

static bool sensor_callback(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    JSchemaInfo schema;
    jvalue_ref parsed;
    service_t* service = (service_t*)data;

    DBG("Sensor status callback message: %s", LSMessageGetPayload(msg));

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)), DOMOPT_NOOPT, &schema);

    // Parsing failed
    if (jis_null(parsed)) {
        j_release(&parsed);
        DBG("sensor_callback: parsing failed.");
        return true;
    }

    jvalue_ref sensorData_ref = jobject_get(parsed, j_cstr_to_buffer("sensorData"));
    if (!jis_valid(sensorData_ref)) {
        DBG("sensor_callback: luna-reply does not contain 'state'");
        j_release(&parsed);
        return true;
    }
    jvalue_ref luminance_value;
    jvalue_ref visibleLuminance_value;

    int luminance;
    int visibleLuminance;

    if ((luminance_value = jobject_get(sensorData_ref, j_cstr_to_buffer("luminance"))) && jis_number(luminance_value)) {
        jnumber_get_i32(luminance_value, &luminance);
    } else {
        DBG("sensor_callback: luna-reply does not contain 'luminance'");
    }

    if ((visibleLuminance_value = jobject_get(sensorData_ref, j_cstr_to_buffer("visibleLuminance"))) && jis_number(visibleLuminance_value)) {
        jnumber_get_i32(visibleLuminance_value, &visibleLuminance);
    } else {
        DBG("sensor_callback: luna-reply does not contain 'visibleLuminance'");
    }
    // DBG("sensor_callback: luminance: %d, visibleLuminance: %d", luminance, visibleLuminance);

    if (service->settings->lumen_threshold == 0) {
        return true;
    }
    if (service->power_paused) {
        return true;
    }

    if (visibleLuminance > service->settings->lumen_threshold) {
        if (service->running && !service->lumen_paused) {
            DBG("sensor_callback: stopping capture due to %d visibleLuminance.", visibleLuminance);
            service->lumen_paused = true;
            service_stop(service);
        }
    } else {
        if (!service->running && service->lumen_paused) {
            DBG("sensor_callback: starting capture due to %d visibleLuminance.", visibleLuminance);
            service->lumen_paused = false;
            service_start(service);
        }
    }

    j_release(&parsed);

    return true;
}

int service_register(service_t* service, GMainLoop* loop)
{
    // LSHandle* handle = NULL;
    // LSHandle* handleLegacy = NULL;
    LSError lserror;

    LSErrorInit(&lserror);

    bool registeredLegacy = false;
    bool registered = false;

    if (&LSRegisterPubPriv != 0) {
        DBG("Try register on LSRegister");
        registered = LSRegister(SERVICE_NAME, &service->handle, &lserror);
        DBG("Try legacy register on LSRegisterPubPriv");
        registeredLegacy = LSRegisterPubPriv(SERVICE_NAME, &service->handleLegacy, true, &lserror);
    } else {
        DBG("Try register on LSRegister");
        registered = LSRegister(SERVICE_NAME, &service->handle, &lserror);
    }

    if (!registered && !registeredLegacy) {
        ERR("Unable to register on Luna bus: %s", lserror.message);
        LSErrorFree(&lserror);
        return -1;
    }

    LSRegisterCategory(service->handle, "/", methods, NULL, NULL, &lserror);
    LSCategorySetData(service->handle, "/", service, &lserror);

    LSGmainAttach(service->handle, loop, &lserror);

    if (!LSCall(service->handle, "luna://com.webos.service.tvpower/power/getPowerState", "{\"subscribe\":true}", power_callback, (void*)service, NULL, &lserror)) {
        WARN("Power state monitoring call failed: %s", lserror.message);
    }

    if (!LSCall(service->handle, "luna://com.webos.service.videooutput/getStatus", "{\"subscribe\":true}", videooutput_callback, (void*)service, NULL, &lserror)) {
        WARN("videooutput/getStatus call failed: %s", lserror.message);
    }

    if (!LSCall(service->handle, "luna://com.webos.settingsservice/getSystemSettings", "{\"category\":\"picture\",\"subscribe\":true}", picture_callback, (void*)service, NULL, &lserror)) {
        WARN("settingsservice/getSystemSettings call failed: %s", lserror.message);
    }

    if (!LSCall(service->handle, "luna://com.webos.service.pqcontroller/getEyeqSensorData", "{\"subscribe\":true}", sensor_callback, (void*)service, NULL, &lserror)) {
        WARN("pqcontroller/getEyeqSensorDatas call failed: %s", lserror.message);
    }

    if (service->settings->lumen_threshold > 0) {
        if (!LSCall(service->handle, "luna://com.webos.settingsservice/setSystemSettings", "{\"category\":\"aiPicture\",\"settings\":{\"isAiPictureActing\":true}}", NULL, (void*)service, NULL, &lserror)) {
            WARN("settingsservice/setSystemSettings call failed: %s", lserror.message);
        }
    }

    if (registeredLegacy) {
        LSRegisterCategory(service->handleLegacy, "/", methods, NULL, NULL, &lserror);
        LSCategorySetData(service->handleLegacy, "/", service, &lserror);
        LSGmainAttach(service->handleLegacy, loop, &lserror);

        if (!LSCall(service->handleLegacy, "luna://com.webos.service.tvpower/power/getPowerState", "{\"subscribe\":true}", power_callback, (void*)service, NULL, &lserror)) {
            WARN("Power state monitoring call failed: %s", lserror.message);
        }

        if (!LSCall(service->handleLegacy, "luna://com.webos.service.videooutput/getStatus", "{\"subscribe\":true}", videooutput_callback, (void*)service, NULL, &lserror)) {
            WARN("videooutput/getStatus call failed: %s", lserror.message);
        }

        if (!LSCall(service->handleLegacy, "luna://com.webos.settingsservice/getSystemSettings", "{\"category\":\"picture\",\"subscribe\":true}", picture_callback, (void*)service, NULL, &lserror)) {
            WARN("settingsservice/getSystemSettings call failed: %s", lserror.message);
        }

        if (!LSCall(service->handleLegacy, "luna://com.webos.service.pqcontroller/getEyeqSensorDatas", "{\"subscribe\":true}", sensor_callback, (void*)service, NULL, &lserror)) {
            WARN("pqcontroller/getEyeqSensorDatas call failed: %s", lserror.message);
        }
    }

    LSErrorFree(&lserror);
    return 0;
}
