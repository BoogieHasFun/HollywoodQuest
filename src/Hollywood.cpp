#include "Hollywood.hpp"

#include "main.hpp"

#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "custom-types/shared/register.hpp"

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/AudioListener.hpp"
#include "UnityEngine/StereoTargetEyeMask.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/Matrix4x4.hpp"

#include "CustomTypes/CameraCapture.hpp"
#include "CustomTypes/AudioCapture.hpp"
#include "UnityEngine/Rect.hpp"

extern "C" {
#include "libavcodec/jni.h"
}

void Hollywood::initialize() {
    static bool init;
    if (init) return;
    init = true;

    HLogger.fmtLog<Paper::LogLevel::DBG>("Initializing Hollywood");
    // il2cpp_functions::Init();
    // custom_types::Register::AutoRegister();

    // For MediaCodec Android, though it does not support encoding. We can do this anyways
    auto jni = Modloader::getJni();
    JavaVM *myVM;
    jni->GetJavaVM(&myVM);
    if (av_jni_set_java_vm(myVM, NULL) < 0) {
        HLogger.fmtLog<Paper::LogLevel::INF>("Unable to enable JNI");
    } else {
        HLogger.fmtLog<Paper::LogLevel::INF>("Successfully enabled JNI");
    }
}

inline UnityEngine::Matrix4x4 MatrixTranslate(UnityEngine::Vector3 const& vector) {
    UnityEngine::Matrix4x4 result;
    result.m00 = 1;
    result.m01 = 0;
    result.m02 = 0;
    result.m03 = vector.x;
    result.m10 = 0;
    result.m11 = 1;
    result.m12 = 0;
    result.m13 = vector.y;
    result.m20 = 0;
    result.m21 = 0;
    result.m22 = 1;
    result.m23 = vector.z;
    result.m30 = 0;
    result.m31 = 0;
    result.m32 = 0;
    result.m33 = 1;
    return result;
}

Hollywood::CameraCapture * Hollywood::SetCameraCapture(UnityEngine::Camera *camera, CameraRecordingSettings const& recordingSettings) {

    camera->set_stereoTargetEye(UnityEngine::StereoTargetEyeMask::None);

    // Idk what this does
    camera->set_orthographic(false);

    camera->set_fieldOfView(recordingSettings.fov);

    // Force it to render into texture
    camera->set_forceIntoRenderTexture(true);

    // Set aspect ratio accordingly
    camera->set_aspect(float(recordingSettings.width) / float(recordingSettings.height));

    static auto set_pixelRect = il2cpp_utils::resolve_icall<void, UnityEngine::Camera *, UnityEngine::Rect &>(
            "UnityEngine.Camera::set_pixelRect_Injected");

    auto pixelRect = UnityEngine::Rect(0, 0, (float) recordingSettings.width, (float) recordingSettings.height);
    set_pixelRect(camera, pixelRect);


    auto texture = UnityEngine::RenderTexture::New_ctor(recordingSettings.width, recordingSettings.height, 24,
                                                        (UnityEngine::RenderTextureFormat) UnityEngine::RenderTextureFormat::Default,
                                                        (UnityEngine::RenderTextureReadWrite) UnityEngine::RenderTextureReadWrite::Default);
    texture->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);
    texture->set_filterMode(UnityEngine::FilterMode::Bilinear);
    texture->Create();
    UnityEngine::RenderTexture::set_active(texture);
    UnityEngine::Object::DontDestroyOnLoad(texture);
    auto cameraCapture = camera->get_gameObject()->AddComponent<Hollywood::CameraCapture *>();
    cameraCapture->readOnlyTexture = texture;
    camera->set_targetTexture(texture);
    camera->set_aspect(float(recordingSettings.width) / float(recordingSettings.height));


//    static auto set_cullingMatrix = il2cpp_utils::resolve_icall<void, UnityEngine::Camera *, UnityEngine::Matrix4x4>(
//            "UnityEngine.Camera::set_cullingMatrix_Injected");
//
//    set_cullingMatrix(camera, UnityEngine::Matrix4x4::Ortho(-99999, 99999, -99999, 99999, 0.001f, 99999) *
//                              MatrixTranslate(UnityEngine::Vector3::get_forward() * -99999 / 2) *
//                              camera->get_worldToCameraMatrix());

    return cameraCapture;
}

Hollywood::AudioCapture * Hollywood::SetAudioCapture(UnityEngine::AudioListener *listener) {
    return listener->get_gameObject()->AddComponent<Hollywood::AudioCapture *>();
}
