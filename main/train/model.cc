#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <esp_heap_caps.h>

#include "trained.h"
#include "model.h"

// Globals, used for compatibility with Arduino-style sketches.
namespace {
  const tflite::Model* model = nullptr;             // 指向TensorFlow Lite模型的指针
  tflite::MicroInterpreter* interpreter = nullptr;  // 指向MicroInterpreter实例的指针
  TfLiteTensor* input = nullptr;                    // 分别指向模型输入和输出张量的指针。
  TfLiteTensor* output = nullptr;
  constexpr int kTensorArenaSize = 600 * 1024;      // 为张量分配的内存区域大小，这里是2000字节。
  static uint8_t *tensor_arena;
}


float quantization_scale = 0.003921568859368563;  // 1/255









