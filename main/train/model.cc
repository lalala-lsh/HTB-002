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

void setup() 
{
  // 加载模型
  model = tflite::GetModel(trained_tflite);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model provided is schema version %d not equal to supported "
                "version %d.", model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }
  MicroPrintf("GetModel finish!\n");

  // tensor_arena分配内存
  if (tensor_arena == NULL) {
    tensor_arena = (uint8_t *)heap_caps_aligned_alloc(8, kTensorArenaSize, MALLOC_CAP_SPIRAM); 
  }
  if (tensor_arena == NULL) {
    printf("Couldn't allocate memory of %d bytes\n", kTensorArenaSize);
    return;
  }else{
     printf("Allocate memory of %d bytes\n", kTensorArenaSize);
  }

  // 创建一个MicroMutableOpResolver实例并添加模型需要的运算
  static tflite::MicroMutableOpResolver<9> resolver;
  if (resolver.AddSoftmax() != kTfLiteOk) {
    return;
  }
  resolver.AddConv2D();
  resolver.AddMul();
  resolver.AddAdd();
  resolver.AddMaxPool2D();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddLogistic();

  // 创建了一个静态（static）的 tflite::MicroInterpreter 对象 static_interpreter
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // 调用 MicroInterpreter 实例的 AllocateTensors() 方法为模型中的所有张量分配内存
  TfLiteStatus allocate_status = interpreter->AllocateTensors(); 
  if (allocate_status != kTfLiteOk) {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);
  output = interpreter->output(0);

  //MicroPrintf("-----Init finish-----\n");
}

float quantization_scale = 0.003921568859368563;  // 1/255
float loop(uint8_t* jpeg_new_buffer_uint8)
{
  // 设置模型输入张量
  // input属于TfLiteTensor结构体类型
  // input->data属于TfLitePtrUnion，是一个联合体（union）类型

  for (int i = 0; i < 96 * 96 * 3; i++) {
    // 输入归一化后的float类型数据作为张量
    float normalized = jpeg_new_buffer_uint8[i] / 255.0f;  // 归一化到 [0, 1]
    input->data.int8[i] = static_cast<int8_t>(normalized / quantization_scale - 128);
  }

  // Run inference, and report any error, 执行模型推理
  TfLiteStatus invoke_status = interpreter->Invoke();
  if (invoke_status != kTfLiteOk) {
    MicroPrintf("Run inference, and report any error");
    return -1;
  }

  // 修改输出处理逻辑,获取三个类别的概率值
  int8_t class_0 = output->data.uint8[0]; // 0-闭眼
  int8_t class_1 = output->data.uint8[1]; // 1-睁眼  
  int8_t class_2 = output->data.uint8[2]; // 2-背景

  // 反量化得到三个类别的实际概率值
  float prob_0 = (class_0 - output->params.zero_point) * output->params.scale;
  float prob_1 = (class_1 - output->params.zero_point) * output->params.scale;
  float prob_2 = (class_2 - output->params.zero_point) * output->params.scale;

  // 返回预测类别:
  // 0 表示闭眼
  // 1 表示睁眼  
  // 2 表示背景
  if(prob_0 > prob_1 && prob_0 > prob_2) {
    return 0;
  } else if(prob_1 > prob_0 && prob_1 > prob_2) {
    return 1;
  } else {
    return 2;
  }
}









