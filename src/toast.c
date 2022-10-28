#include "toast.h"
#include "log.h"
#include <pbnjson.h>

int createToast(service_t* service, char* toast)
{
    if (service->power_paused) {
        INFO("SKipping toast due to power paused.");
        return 1;
    }
    jvalue_ref message_body_obj = jobject_create();

    jobject_set(message_body_obj, j_cstr_to_buffer("message"), jstring_create(toast));
    jobject_set(message_body_obj, j_cstr_to_buffer("sourceId"), jstring_create("org.webosbrew.piccap.service"));
    jobject_set(message_body_obj, j_cstr_to_buffer("onlyToast"), jboolean_create(true));
    jobject_set(message_body_obj, j_cstr_to_buffer("noaction"), jboolean_create(true));
    jobject_set(message_body_obj, j_cstr_to_buffer("isSysReq"), jboolean_create(true));
    // jobject_set(message_body_obj, j_cstr_to_buffer("iconUrl"), jstring_create("file:///media/developer/apps/usr/palm/applications/org.webosbrew.piccap/logo_small.png"));
    jobject_set(message_body_obj, j_cstr_to_buffer("iconUrl"), jstring_create("/media/developer/apps/usr/palm/applications/org.webosbrew.piccap/logo_small.png"));
    // jobject_set(message_body_obj, j_cstr_to_buffer("iconUrl"), jstring_create("logo_small.png"));

    // char message_body[500];
    // sprintf(message_body, "{\"message\": \"%s\", \"iconUrl\": \"file:///usr/palm/notificationmgr/images/toast-notification-icon.png\", \"onlyToast\": true}", toast);

    const char* message_body = jvalue_tostring_simple(message_body_obj);

    LSError lserror;
    LSErrorInit(&lserror);
    int ret = 0;
    if (service->handle != NULL) {
        if (!(ret = LSCall(service->handle, "luna://com.webos.notification/createToast", message_body, NULL, NULL, NULL, &lserror))) {
            WARN("Toast call failed: %s", lserror.message);
        }
    }
    if (service->handleLegacy != NULL) {
        if (!(ret = LSCall(service->handleLegacy, "luna://com.webos.notification/createToast", message_body, NULL, NULL, NULL, &lserror))) {
            WARN("Legacy Toast call failed: %s", lserror.message);
        }
    }
    return ret;
}