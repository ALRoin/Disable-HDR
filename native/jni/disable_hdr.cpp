/*
 * Disable HDR - Zygisk module
 *
 * Injects DisableHdrHook.install() (see DisableHdrHook.java) into every target app
 * process via a raw classes.dex loaded through InMemoryDexClassLoader.
 * No LSPosed/Xposed manager required - this is a self-contained Zygisk
 * module, same approach used by HMA-OSS's Zygisk-only branch.
 *
 * classes.dex is read directly from disk in preAppSpecialize(), while the
 * process is still running as root (privilege drop to the app's own UID
 * happens during specialization, not before). This avoids needing Zygisk's
 * companion-process mechanism entirely - some Zygisk implementations
 * (e.g. certain Zygisk Next builds) can reject companion socket requests
 * for reasons outside our control, so we sidestep that path completely.
 */

#include <jni.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include "zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;

class DisableHdrModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (args == nullptr || args->nice_name == nullptr) {
            return;
        }

        const char *process_name = env->GetStringUTFChars(args->nice_name, nullptr);
        if (process_name == nullptr) return;

        std::string name(process_name);
        env->ReleaseStringUTFChars(args->nice_name, process_name);

        // Ignore system processes and isolated services
        if (name.empty() || 
            name.find("system_server") != std::string::npos ||
            name.find("com.android.systemui") != std::string::npos ||
            name.find("isolated") != std::string::npos) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        should_inject = true;
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!should_inject) return;

        // Fetch companion module DEX file descriptor provided by Zygisk
        int fd = api->connectCompanion();
        if (fd < 0) return;

        off_t size = lseek(fd, 0, SEEK_END);
        if (size <= 0) {
            close(fd);
            return;
        }
        lseek(fd, 0, SEEK_SET);

        // Load DEX payload in target application process
        loadDexAndInit(fd, size);
        close(fd);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool should_inject = false;

    void loadDexAndInit(int fd, off_t size) {
        jobject byte_buffer = env->NewDirectByteBuffer(
            mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0), size
        );
        if (!byte_buffer) return;

        jclass class_loader_cls = env->FindClass("dalvik/system/InMemoryDexClassLoader");
        jclass class_cls = env->FindClass("java/lang/Class");
        jclass thread_cls = env->FindClass("java/lang/Thread");

        jmethodID get_cl_mid = env->GetMethodID(thread_cls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
        jmethodID current_thread_mid = env->GetStaticMethodID(thread_cls, "currentThread", "()Ljava/lang/Thread;");
        jobject current_thread = env->CallStaticObjectMethod(thread_cls, current_thread_mid);
        jobject parent_cl = env->CallObjectMethod(current_thread, get_cl_mid);

        jmethodID dex_loader_init = env->GetMethodID(
            class_loader_cls, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V"
        );
        jobject dex_loader = env->NewObject(class_loader_cls, dex_loader_init, byte_buffer, parent_cl);

        jmethodID load_class_mid = env->GetMethodID(
            class_loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"
        );
        jstring hook_class_name = env->NewStringUTF("com.disablehdr.hook.DisableHdrHook");
        auto hook_class = static_cast<jclass>(env->CallObjectMethod(dex_loader, load_class_mid, hook_class_name));

        if (hook_class) {
            jmethodID init_mid = env->GetStaticMethodID(hook_class, "init", "()V");
            if (init_mid) {
                env->CallStaticVoidMethod(hook_class, init_mid);
            }
        }
    }
};

REGISTER_ZYGISK_MODULE(DisableHdrModule)
