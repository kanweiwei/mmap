#include <napi.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#endif

#ifdef _WIN32
Napi::Value MmapSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  std::string path = info[0].As<Napi::String>().Utf8Value();

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

  auto finalizer = [hFile, hMap, pView](Napi::Env env, void *data)
  {
    ::UnmapViewOfFile(pView);
    ::CloseHandle(hMap);
    ::CloseHandle(hFile);
  };

  Napi::ArrayBuffer arrayBuffer = Napi::ArrayBuffer::New(env, pView, fileSize, finalizer);

  return arrayBuffer;
}

#else
Napi::Value MmapSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  std::string path = info[0].As<Napi::String>().Utf8Value();

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

  close(fd);

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
  exports.Set(Napi::String::New(env, "mmapSync"), Napi::Function::New(env, MmapSync));
  return exports;
}

NODE_API_MODULE(addon, Init)
