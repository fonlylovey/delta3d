// dtcorebindings.cpp: Python bindings for dtCore library.
//
//////////////////////////////////////////////////////////////////////

#include "dtpython.h"

#include "dt.h"

using namespace boost::python;
using namespace dtCore;

// The individual class bindings

void initBaseBindings();
void initCameraBindings();
void initCloudDomeBindings();
void initCloudPlaneBindings();
void initDrawableBindings();
void initEnvEffectBindings();
void initEnvironmentBindings();
void initFlyMotionModelBindings();
void initInfiniteTerrainBindings();
void initInputDeviceBindings();
void initIsectorBindings();
void initJoystickBindings();
void initKeyboardBindings();
void initLogicalInputDeviceBindings();
void initMotionModelBindings();
void initMouseBindings();
void initObjectBindings();
void initOrbitMotionModelBindings();
void initParticleSystemBindings();
void initPhysicalBindings();
void initSceneBindings();
void initSkyBoxBindings();
void initSkyDomeBindings();
void initSoundBindings();
void initSystemBindings();
void initTrackerBindings();
void initTransformBindings();
void initTransformableBindings();
void initTripodBindings();
void initUFOMotionModelBindings();
void initUIDrawableBindings();
void initWalkMotionModelBindings();
void initWindowBindings();

void NotifyWrap(NotifySeverity ns, const char* msg)
{
   Notify(ns, msg);
}

BOOST_PYTHON_MODULE(dtCore)
{
   def("SetDataFilePathList", SetDataFilePathList);
   def("SetNotifyLevel", SetNotifyLevel);
   def("Notify", NotifyWrap);
   
   enum_<NotifySeverity>("NotifySeverity")
      .value("ALWAYS", ALWAYS)
      .value("FATAL", FATAL)
      .value("WARN", WARN)
      .value("NOTICE", NOTICE)
      .value("INFO", INFO)
      .value("DEBUG_INFO", DEBUG_INFO)
      .export_values();
      
   initBaseBindings();
   initSystemBindings();
   initSceneBindings();
   initTransformBindings();
   initTransformableBindings();
   initDrawableBindings();
   initEnvEffectBindings();
   initCloudDomeBindings();
   initCloudPlaneBindings();
   initSkyBoxBindings();
   initSkyDomeBindings();
   initEnvironmentBindings();
   initPhysicalBindings();
   initObjectBindings();
   initCameraBindings();
   initTripodBindings();
   initInputDeviceBindings();
   initKeyboardBindings();
   initMouseBindings();
   initJoystickBindings();
   initTrackerBindings();
   initLogicalInputDeviceBindings();
   initWindowBindings();
   initSoundBindings();
   initParticleSystemBindings();
   initUIDrawableBindings();
   initIsectorBindings();
   initInfiniteTerrainBindings();
   initMotionModelBindings();
   initWalkMotionModelBindings();
   initFlyMotionModelBindings();
   initUFOMotionModelBindings();
   initOrbitMotionModelBindings();
}
