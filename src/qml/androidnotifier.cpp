// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/androidnotifier.h>

#include <jni.h>
#include <android/log.h>

extern "C" {
    JNIEXPORT jboolean JNICALL Java_org_bitcoincore_qt_BitcoinQtService_register(JNIEnv * env, jobject obj);
    JNIEXPORT jboolean JNICALL Java_org_bitcoincore_qt_BitcoinQtActivity_register(JNIEnv * env, jobject obj);
    JNIEXPORT jboolean JNICALL Java_org_bitcoincore_qt_BitcoinQtActivity_showNotificationOnboarding(JNIEnv * env, jobject obj);
}

static JavaVM * g_vm = nullptr;
static jobject g_activity_obj;
static jobject g_obj;
static AndroidNotifier * g_notifier = nullptr;

JNIEXPORT jboolean JNICALL Java_org_bitcoincore_qt_BitcoinQtActivity_register(JNIEnv * env, jobject obj)
{
    __android_log_print(ANDROID_LOG_INFO, "AndroidNotifier", "JVM IS REGISTERED");
    env->GetJavaVM(&g_vm);
    g_activity_obj = env->NewGlobalRef(obj);
    assert(g_activity_obj);

    return (jboolean) true;
}

JNIEXPORT jboolean JNICALL Java_org_bitcoincore_qt_BitcoinQtService_register(JNIEnv * env, jobject obj)
{
    env->GetJavaVM(&g_vm);
    g_obj = env->NewGlobalRef(obj);

    return (jboolean) true;
}

namespace {

JNIEnv* getJNIEnv(JavaVM* javaVM) {
    JNIEnv* env;
    jint result = javaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        javaVM->AttachCurrentThread(&env, nullptr);
    } else if (result != JNI_OK) {
        // Error handling
        return nullptr;
    }

    return env;
}

}

AndroidNotifier::AndroidNotifier(const NodeModel & node_model,
                                 QObject * parent)
: QObject(parent)
, m_node_model(node_model)
{
    QObject::connect(&node_model, &NodeModel::blockTipHeightChanged,
                     this, &AndroidNotifier::onBlockTipHeightChanged);
    QObject::connect(&node_model, &NodeModel::numOutboundPeersChanged,
                     this, &AndroidNotifier::onNumOutboundPeersChanged);
    QObject::connect(&node_model, &NodeModel::pauseChanged,
                     this, &AndroidNotifier::onPausedChanged);
    QObject::connect(&node_model, &NodeModel::verificationProgressChanged,
                     this, &AndroidNotifier::onVerificationProgressChanged);

    if (g_notifier == nullptr) {
        g_notifier = this;
    }
}

void AndroidNotifier::onBlockTipHeightChanged()
{
    if (g_vm != nullptr && g_obj != NULL) {
        JNIEnv * env = getJNIEnv(g_vm);
        if (env == nullptr) {
            return;
        }
        jclass clazz = env->GetObjectClass(g_obj);
        jmethodID mid = env->GetMethodID(clazz, "updateBlockTipHeight", "(I)V");
        env->CallVoidMethod(g_obj, mid, m_node_model.blockTipHeight());
    }
}

void AndroidNotifier::onNumOutboundPeersChanged()
{
    if (g_vm != nullptr && g_obj != NULL) {
        JNIEnv * env = getJNIEnv(g_vm);
        if (env == nullptr) {
            return;
        }
        jclass clazz = env->GetObjectClass(g_obj);
        jmethodID mid = env->GetMethodID(clazz, "updateNumberOfPeers", "(I)V");
        env->CallVoidMethod(g_obj, mid, m_node_model.numOutboundPeers());
    }
}

void AndroidNotifier::onVerificationProgressChanged()
{
    if (g_vm != nullptr && g_obj != NULL) {
        JNIEnv * env = getJNIEnv(g_vm);
        if (env == nullptr) {
            return;
        }
        jclass clazz = env->GetObjectClass(g_obj);
        jmethodID mid = env->GetMethodID(clazz, "updateVerificationProgress", "(D)V");
        env->CallVoidMethod(g_obj, mid, static_cast<jdouble>(m_node_model.verificationProgress()));
    }
}

void AndroidNotifier::onPausedChanged()
{
    if (g_vm != nullptr && g_obj != NULL) {
        JNIEnv * env = getJNIEnv(g_vm);
        if (env == nullptr) {
            return;
        }
        jclass clazz = env->GetObjectClass(g_obj);
        jmethodID mid = env->GetMethodID(clazz, "updatePaused", "(Z)V");
        env->CallVoidMethod(g_obj, mid, static_cast<jboolean>(m_node_model.pause()));
    }
}

void AndroidNotifier::triggerOnboardingMessage()
{
    Q_EMIT showNotificationOnboarding();
}

void AndroidNotifier::notificationsEnabledChanged(bool enabled)
{
    if (g_vm != nullptr) {
        JNIEnv * env = getJNIEnv(g_vm);
        if (env == nullptr) {
            return;
        }
        jclass clazz = env->GetObjectClass(g_activity_obj);
        jmethodID mid = env->GetMethodID(clazz, "notificationsEnabledChanged", "(Z)V");
        env->CallVoidMethod(g_activity_obj, mid, static_cast<jboolean>(enabled));
    }
}

