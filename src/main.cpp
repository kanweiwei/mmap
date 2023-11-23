#include <napi.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32

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
    // Windows 的实现
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
#endif

#ifdef _WIN32
Napi::Value MmapAsync(const Napi::CallbackInfo &info)
{
  (new MmapTask(info))->Queue();

  return info.Env().Undefined();
}
#endif

#ifdef _WIN32
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

#elif defined(__APPLE__)

class MmapTask : public Napi::AsyncWorker
{
public:
  MmapTask(const Napi::CallbackInfo &info)
      : Napi::AsyncWorker(info.Env()), callback_(Napi::Persistent(info[1].As<Napi::Function>()))
  {
    path_ = info[0].As<Napi::String>();
  }

  void Execute() override
  {
    int fd = open(path_.c_str(), O_RDONLY);
    if (fd < 0)
    {
      SetError("Failed to open file");
      return;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1)
    {
      close(fd);
      SetError("Failed to get file size");
      return;
    }

    void *pView = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (pView == MAP_FAILED)
    {
      close(fd);
      SetError("Failed to map view of file");
      return;
    }

    pView_ = pView;
    size_ = sb.st_size;

    // No longer need the file descriptor
    close(fd);
  }

  void OnOK() override
  {
    Napi::Env env = Env();
    Napi::HandleScope scope(env);

    auto finalizer = [this](Napi::Env env, void *data)
    {
      munmap(pView_, size_);
    };

    Napi::ArrayBuffer arrayBuffer = Napi::ArrayBuffer::New(env, pView_, size_, finalizer);
    callback_.Call({env.Null(), arrayBuffer});
  }

private:
  std::string path_;
  Napi::FunctionReference callback_;
  void *pView_;
  size_t size_;
};

Napi::Value MmapAsync(const Napi::CallbackInfo &info)
{
  (new MmapTask(info))->Queue();

  return info.Env().Undefined();
}

Napi::Value MmapSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  std::string path = info[0].As<Napi::String>();

  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0)
  {
    Napi::Error::New(env, "Failed to open file").ThrowAsJavaScriptException();
    return env.Null();
  }

  struct stat sb;
  if (fstat(fd, &sb) == -1)
  {
    close(fd);
    Napi::Error::New(env, "Failed to get file size").ThrowAsJavaScriptException();
    return env.Null();
  }

  void *pView = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (pView == MAP_FAILED)
  {
    close(fd);
    Napi::Error::New(env, "Failed to map view of file").ThrowAsJavaScriptException();
    return env.Null();
  }

  // 我们不再需要文件描述符了，可以关闭
  close(fd);

  // 使用完毕后清理资源
  auto finalizer = [pView, sb](Napi::Env env, void *data)
  {
    munmap(pView, sb.st_size);
  };

  Napi::ArrayBuffer arrayBuffer = Napi::ArrayBuffer::New(env, pView, sb.st_size, finalizer);

  return arrayBuffer;
}
#endif

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  exports.Set(Napi::String::New(env, "mmapAsync"), Napi::Function::New(env, MmapAsync));
  exports.Set(Napi::String::New(env, "mmapSync"), Napi::Function::New(env, MmapSync));

  return exports;
}

NODE_API_MODULE(addon, Init)
