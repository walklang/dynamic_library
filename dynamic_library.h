///////////////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2017 The Authors of ANT(http:://ant.sh) . All Rights Reserved. 
// Use of this source code is governed by a BSD-style license that can be 
// found in the LICENSE file. 
//
///////////////////////////////////////////////////////////////////////////////////////////

#ifndef UTILS_DYNAMIC_LIBRARY_INCLUDE_H_
#define UTILS_DYNAMIC_LIBRARY_INCLUDE_H_

#include <memory>
#include <Windows.h>

#include "basictypes.h"


// These code move from https://github.com/Bugzl/test.

namespace internal {

// The arraysize(arr) macro returns the # of elements in an array arr.
// The expression is a compile-time constant, and therefore can be
// used in defining new arrays, for example.  If you use arraysize on
// a pointer by mistake, you will get a compile-time error.
//
// One caveat is that arraysize() doesn't accept any array of an
// anonymous type or a type defined inside a function.  In these rare
// cases, you have to use the unsafe ARRAYSIZE_UNSAFE() macro below.  This is
// due to a limitation in C++'s template system.  The limitation might
// eventually be removed, but it hasn't happened yet.

// This template function declaration is used in defining arraysize.
// Note that the function doesn't need an implementation, as we only
// use its type.
template <typename T, size_t N>
char(&ArraySizeHelper(T(&array)[N]))[N];

// That gcc wants both of these prototypes seems mysterious. VC, for
// its part, can't decide which to use (another mystery). Matching of
// template overloads: the final frontier.
#ifndef _MSC_VER
template <typename T, size_t N>
char(&ArraySizeHelper(const T(&array)[N]))[N];
#endif

#define arraysize(array) (sizeof(ArraySizeHelper(array)))

const wchar_t kSeparators[] = L"\\/";
const size_t kSeparatorsLength = arraysize(kSeparators);
const wchar_t kCurrentDirectory[] = L".";

std::wstring::size_type FindDriveLetter(
    const std::wstring& path) {
  // This is dependent on an ASCII-based character set, but that's a
  // reasonable assumption.  iswalpha can be too inclusive here.
  if (path.length() >= 2 && path[1] == L':' &&
      ((path[0] >= L'A' && path[0] <= L'Z') ||
       (path[0] >= L'a' && path[0] <= L'z'))) {
    return 1;
  }
  return std::wstring::npos;
}

bool IsSeparator(wchar_t character) {
  for (size_t i = 0; i < kSeparatorsLength - 1; ++i) {
    if (character == kSeparators[i]) {
      return true;
    }
  }
  return false;
}

std::wstring StripTrailingSeparators(std::wstring path) {
  auto start = FindDriveLetter(path) + 2;
  std::wstring::size_type last_stripped = std::wstring::npos;
  for (std::wstring::size_type pos = path.length();
       pos > start && IsSeparator(path[pos - 1]); --pos) {
    // If the string only has two separators and they're at the beginning,
    // don't strip them, unless the string began with more than two separators.
    if (pos != start + 1 || last_stripped == start + 2 ||
        !IsSeparator(path[start - 1])) {
      path.resize(pos - 1);
      last_stripped = pos;
    }
  }
  return path;
}

bool GetCurrentDirectory(std::wstring* dir) {
  wchar_t system_buffer[MAX_PATH] = {0};
  auto len = ::GetCurrentDirectory(MAX_PATH, system_buffer);
  if (len == 0 || len > MAX_PATH) return false;
  std::wstring dir_str(system_buffer);
  *dir = StripTrailingSeparators(dir_str);
  return true;
}

bool SetCurrentDirectory(const std::wstring& directory) {
  BOOL ret = ::SetCurrentDirectory(directory.c_str());
  return ret != 0;
}

std::wstring GetParent(std::wstring path) {
  path = StripTrailingSeparators(path);

  // The drive letter, if any, always needs to remain in the output.  If there
  // is no drive letter, as will always be the case on platforms which do not
  // support drive letters, letter will be npos, or -1, so the comparisons and
  // resizes below using letter will still be valid.
  auto letter = FindDriveLetter(path);

  auto last_separator =
      path.find_last_of(kSeparators, std::wstring::npos, kSeparatorsLength - 1);
  if (last_separator == std::wstring::npos) {
    // path_ is in the current directory.
    path.resize(letter + 1);
  } else if (last_separator == letter + 1) {
    // path_ is in the root directory.
    path.resize(letter + 2);
  } else if (last_separator == letter + 2 && IsSeparator(path[letter + 1])) {
    // path_ is in "//" (possibly with a drive letter); leave the double
    // separator intact indicating alternate root.
    path.resize(letter + 3);
  } else if (last_separator != 0) {
    // path_ is somewhere else, trim the basename.
    path.resize(last_separator);
  }

  path = StripTrailingSeparators(path);
  if (!path.length()) path = kCurrentDirectory;

  return path;
}

typedef HMODULE(WINAPI* LoadLibraryFunction)(const wchar_t* file_name);

// LoadLibrary() opens the file off disk.
HMODULE LoadNativeLibraryHelper(const std::wstring& library_path,
                                LoadLibraryFunction load_library_api) {
  // Switch the current directory to the library directory as the library
  // may have dependencies on DLLs in this directory.
  bool restore_directory = false;
  std::wstring current_directory;
  if (internal::GetCurrentDirectory(&current_directory)) {
    auto plugin_path = internal::GetParent(library_path);
    if (!plugin_path.empty()) {
      internal::SetCurrentDirectory(plugin_path);
      restore_directory = true;
    }
  }
  HMODULE module = (*load_library_api)(library_path.c_str());
  if (restore_directory) internal::SetCurrentDirectory(current_directory);

  return module;
}

HMODULE LoadLibrary(const std::wstring& path,
                                     std::string* error) {
  return ::internal::LoadNativeLibraryHelper(path, ::LoadLibraryW);
}


void UnloadNativeLibrary(HMODULE library) {
  if (library == nullptr) return;
  ::FreeLibrary(library);
}


// Returns the result whether |library_name| had been loaded.
// It will be true if |library_name| is empty.
bool WellKnownLibrary(const std::wstring& library_name) {
  if (library_name.empty()) return false;
  HMODULE wellknown_handler = ::GetModuleHandle(library_name.c_str());
  return nullptr != wellknown_handler;
}


void* GetFunctionPointerFromNativeLibrary(HMODULE library,
                                                           const char* name) {
  if (name == nullptr) return nullptr;
  return ::GetProcAddress(library, name);
}

void* GetFunctionPointerFromNativeLibrary(
    const std::wstring& library_name, const char* name) {
  if (name == nullptr) return nullptr;
  HMODULE wellknown_handler = ::GetModuleHandle(library_name.c_str());
  if (nullptr == wellknown_handler) return nullptr;
  return GetFunctionPointerFromNativeLibrary(wellknown_handler, name);
}

}  // namespace internal


namespace utils {

template<typename R, typename... P>
struct FunctorTraits { using Type = R(WINAPI *)(P...); };

class DynamicLibrary {
 public:
    explicit DynamicLibrary() {}
    explicit DynamicLibrary(const HMODULE& library) : library_(library) {}
    explicit DynamicLibrary(const std::wstring& path)
        : library_(internal::LoadLibrary(path, nullptr)) {}
    explicit DynamicLibrary(LPCTSTR filename) : library_name_(filename) {}
    virtual ~DynamicLibrary() { internal::UnloadNativeLibrary(library_); }

    bool is_valid() const { return !!library_ || internal::WellKnownLibrary(library_name_); }

    // Only validate when this object hold the wellknown library's handler.
    const std::wstring& library_name() const { return library_name_; }

    void* GetFunctionPointer(const char* function_name) const {
        if (!is_valid()) return nullptr;
        if (library_name_.empty()) return internal::GetFunctionPointerFromNativeLibrary(library_, function_name);
        return internal::GetFunctionPointerFromNativeLibrary(library_name_, function_name);
    }

    template<typename R, typename... P>
    typename FunctorTraits<R, P...>::Type GetFunctionPointer(const std::string& InterfaceName) const {
        if (!is_valid() && InterfaceName.empty()) return nullptr;
        using Type = FunctorTraits<R, P...>::Type;
        return reinterpret_cast<Type>(DynamicLibrary::GetFunctionPointer(InterfaceName.c_str()));
    }

    void Reset(HMODULE library) {
        internal::UnloadNativeLibrary(library_);
        library_ = library;
    }

    // Returns the native library handle and removes it from this object. The
    // caller must manage the lifetime of the handle.
    // Otherwise, when we hold the wellknown library's handler, result should be nullptr.
    auto Release() {
        auto result = library_;
        library_ = nullptr;
        return result;
    }

private:
    std::wstring library_name_;
    HMODULE library_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(DynamicLibrary);
};

template<typename R, typename... P>
typename FunctorTraits<R, P...>::Type GetFunctionPointer(const HMODULE& library, const std::string& InterfaceName) {
    if (!library || InterfaceName.empty()) return nullptr;
    using Type = FunctorTraits<R, P...>::Type;
    return reinterpret_cast<Type>(GetFunctionPointerFromNativeLibrary(library, InterfaceName.c_str()));
}

template<typename R, typename... P>
typename FunctorTraits<R, P...>::Type GetFunctionPointer(const DynamicLibrary* library, const std::string& InterfaceName) {
    if (!library) return nullptr;
    return library->GetFunctionPointer<R, P...>(InterfaceName.c_str());
}

template<typename R, typename... P>
typename FunctorTraits<R, P...>::Type GetFunctionPointer(const std::weak_ptr<DynamicLibrary>& library, const std::string& InterfaceName) {
    auto known_library = library.lock();
    if (!known_library) return nullptr;
    return known_library->GetFunctionPointer<R, P...>(InterfaceName.c_str());
}

} // namespace utils

#endif  // !UTILS_DYNAMIC_LIBRARY_INCLUDE_H_