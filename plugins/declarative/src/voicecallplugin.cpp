#include "voicecallplugin.h"

#include "voicecallhandler.h"
#include "voicecallmanager.h"

#include "voicecallaudiorecorder.h"
#include "voicecallmodel.h"
#include "voicecallprovidermodel.h"

#include <QtQml>

namespace {

QObject *voice_call_audio_recorder_api_factory(QQmlEngine *qmlEngine, QJSEngine *)
{
    return new VoiceCallAudioRecorder(qmlEngine);
}

}

void VoiceCallPlugin::registerTypes(const char *uri)
{
    qmlRegisterUncreatableType<VoiceCallHandler>(uri, 1, 0, "VoiceCall", "uncreatable type");
    qmlRegisterUncreatableType<VoiceCallModel>(uri, 1, 0, "VoiceCallModel", "uncreatable type");
    qmlRegisterUncreatableType<VoiceCallProviderModel>(uri, 1, 0, "VoiceCallProviderModel", "uncreatable type");

    qmlRegisterSingletonType<VoiceCallAudioRecorder>(uri, 1, 0, "VoiceCallAudioRecorder", voice_call_audio_recorder_api_factory);

    qmlRegisterType<VoiceCallManager>(uri, 1, 0, "VoiceCallManager");
}

