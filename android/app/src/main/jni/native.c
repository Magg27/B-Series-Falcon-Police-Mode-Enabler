#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pme.h"

typedef struct {
    JNIEnv *environment;
    jobject port;
    jmethodID read_method;
    jmethodID write_method;
} UsbSession;

static int usb_write(void *context, const uint8_t *data, size_t length, int timeout_ms)
{
    UsbSession *session = context;
    JNIEnv *environment = session->environment;
    jbyteArray bytes = (*environment)->NewByteArray(environment, (jsize)length);
    if (!bytes) return -1;
    (*environment)->SetByteArrayRegion(environment, bytes, 0, (jsize)length, (const jbyte *)data);
    (*environment)->CallVoidMethod(environment, session->port, session->write_method, bytes, (jint)timeout_ms);
    (*environment)->DeleteLocalRef(environment, bytes);
    if ((*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        return -1;
    }
    return (int)length;
}

static int usb_read(void *context, uint8_t *data, size_t length, int timeout_ms)
{
    UsbSession *session = context;
    JNIEnv *environment = session->environment;
    jbyteArray bytes = (*environment)->NewByteArray(environment, (jsize)length);
    if (!bytes) return -1;
    jint amount = (*environment)->CallIntMethod(environment, session->port, session->read_method,
                                               bytes, (jint)timeout_ms);
    if ((*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        (*environment)->DeleteLocalRef(environment, bytes);
        return -1;
    }
    if (amount < 0 || (size_t)amount > length) {
        (*environment)->DeleteLocalRef(environment, bytes);
        return -1;
    }
    if (amount) (*environment)->GetByteArrayRegion(environment, bytes, 0, amount, (jbyte *)data);
    (*environment)->DeleteLocalRef(environment, bytes);
    return amount;
}

JNIEXPORT jstring JNICALL
Java_au_magg_policemode_MainActivity_nativeRun(JNIEnv *environment, jobject activity,
                                                  jobject serial_port, jint operation,
                                                  jboolean allow_untested)
{
    (void)activity;
    jclass port_class = (*environment)->GetObjectClass(environment, serial_port);
    if (!port_class) return (*environment)->NewStringUTF(environment, "USB serial port unavailable.");
    UsbSession session = {
        .environment = environment,
        .port = serial_port,
        .read_method = (*environment)->GetMethodID(environment, port_class, "read", "([BI)I"),
        .write_method = (*environment)->GetMethodID(environment, port_class, "write", "([BI)V")
    };
    (*environment)->DeleteLocalRef(environment, port_class);
    if (!session.read_method || !session.write_method) {
        if ((*environment)->ExceptionCheck(environment)) (*environment)->ExceptionClear(environment);
        return (*environment)->NewStringUTF(environment, "USB serial driver methods unavailable.");
    }
    PmeTransport transport = {.context = &session, .read = usb_read, .write = usb_write};
    PmeState state;
    char message[512];
    pme_run(&transport, operation, allow_untested, &state, message, sizeof(message));
    return (*environment)->NewStringUTF(environment, message);
}
