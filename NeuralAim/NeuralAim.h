#ifndef NEURALAIM_H
#define NEURALAIM_H

#include "config.h"
#ifdef USE_CUDA
#include "trt_detector.h"
#else
#include "dml_detector.h"
#endif
#include "mouse.h"
#include "MouseInput.h"
#include "Arduino.h"
#include "RP2350.h"
#include "detection_buffer.h"
#include "KmboxNetConnection.h"
#include "KmboxAConnection.h"
#include "Makcu.h"
#include "rzctl.h"
#include <memory>
#include <mutex>

extern Config config;
#ifdef USE_CUDA
extern TrtDetector trt_detector;
#else
extern DirectMLDetector* dml_detector;
#endif
extern DetectionBuffer detectionBuffer;
extern MouseThread* globalMouseThread;
extern GhubMouse* gHub;
extern RzctlMouse* razerControl;
extern Arduino* arduinoSerial;
extern RP2350* rp2350Serial;
extern KmboxNetConnection* kmboxNetSerial;
extern KmboxAConnection* kmboxASerial;
extern MakcuConnection* makcuSerial;
extern std::unique_ptr<IMouseInput> activeMouseInputOwner;
extern std::atomic<bool> input_method_changed;
extern std::atomic<bool> aiming;
extern std::atomic<bool> shooting;
extern std::atomic<bool> zooming;
extern std::mutex configMutex;
extern std::mutex inputDevicesMutex;

#endif // NEURALAIM_H
