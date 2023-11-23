#include <napi.h>
#include <windows.h>

class MmapTask : public Napi::AsyncWorker
{
public:
  MmapTask(const Napi::CallbackInfo &info)
      : Napi::AsyncWorker(info.Env()), callback_(Napi::Persistent(info[1].As<Napi::Function>()))
  {
    path_ = info[0].As<Napi::String>();
  }

  void Execute()
  {
    // 将文件路径转换为文件句柄
    hFile = ::CreateFile(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
      SetError("Failed to open file");
      return;
    }

    hMap = ::CreateFileMapping(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (hMap == NULL)
    {
      ::CloseHandle(hFile);
      SetError("Failed to create mapping");
      return;
    }

    pView = ::MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (pView == NULL)
    {
      ::CloseHandle(hMap);
      ::CloseHandle(hFile);
      SetError("Failed to map view of file");
      return;
    }

    fileSize = ::GetFileSize(hFile, nullptr);
  }

  void OnOK()
  {
    Napi::Env env = Env();
    Napi::HandleScope scope(env);

    auto finalizer = [this](Napi::Env env, void *data)
    {
      ::UnmapViewOfFile(pView);
      ::CloseHandle(hMap);
      ::CloseHandle(hFile);
    };

    Napi::ArrayBuffer arrayBuffer = Napi::ArrayBuffer::New(env, pView, fileSize, finalizer);

    callback_.Call({env.Null(), arrayBuffer});
  }

private:
  std::string path_;
  Napi::FunctionReference callback_;
  HANDLE hFile;
  HANDLE hMap;
  void *pView;
  DWORD fileSize;
};

Napi::Value MmapAsync(const Napi::CallbackInfo &info)
{
  (new MmapTask(info))->Queue();

  return info.Env().Undefined();
}
// 定义同步版本
Napi::Value MmapSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  std::string path = info[0].As<Napi::String>();

  // 将文件路径转换为文件句柄
  HANDLE hFile = ::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
  {
    Napi::Error::New(env, "Failed to open file").ThrowAsJavaScriptException();
    return env.Null();
  }

  HANDLE hMap = ::CreateFileMapping(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (hMap == NULL)
  {
    ::CloseHandle(hFile);
    Napi::Error::New(env, "Failed to create mapping").ThrowAsJavaScriptException();
    return env.Null();
  }

  void *pView = ::MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
  if (pView == NULL)
  {
    ::CloseHandle(hMap);
    ::CloseHandle(hFile);
    Napi::Error::New(env, "Failed to map view of file").ThrowAsJavaScriptException();
    return env.Null();
  }

  DWORD fileSize = ::GetFileSize(hFile, nullptr);

  // 使用完毕后清理资源
  auto finalizer = [hFile, hMap, pView](Napi::Env env, void *data)
  {
    ::UnmapViewOfFile(pView);
    ::CloseHandle(hMap);
    ::CloseHandle(hFile);
  };

  Napi::ArrayBuffer arrayBuffer = Napi::ArrayBuffer::New(env, pView, fileSize, finalizer);

  return arrayBuffer;
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  exports.Set(Napi::String::New(env, "mmapAsync"), Napi::Function::New(env, MmapAsync));
  exports.Set(Napi::String::New(env, "mmapSync"), Napi::Function::New(env, MmapSync));

  return exports;
}

NODE_API_MODULE(addon, Init)