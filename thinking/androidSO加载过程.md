# 前言

在目前许多的Android应用加固中，都用到了so文件，并且通过针对so文件的section table进行混淆处理，以避免ida等逆向工具进行静态分析，因为在Android源码中，so的加载是完全不需要section信息的。

在此之前，很多文章都已经写到过关于Android so加载的流程，但很多都是基于Android4.x系统

虽然流程大同小异，但在Android5.0以后已经从Dalvik转换成ART，文件关系上已经对不上

因此，我针对Android7.1.2_r28的代码，对so加载过程进行分析

# so加载

在加载一个so的时候，必然要写一句

```java
System.loadLibrary("native-lib");
```

那么，我们就从这个函数看起

在aosp目录中，这个方法位于`./libcore/ojluni/src/main/java/java/lang/Runtime.java` 当中

(一些关于异常错误处理的代码已删除，方便只看加载思路)

```java
   // ./libcore/ojluni/src/main/java/java/lang/Runtime.java

	@CallerSensitive
    public void loadLibrary(String libname) {
        loadLibrary0(VMStack.getCallingClassLoader(), libname);
    }
    
    synchronized void loadLibrary0(ClassLoader loader, String libname) {
        
    ......
          
        String libraryName = libname;
        if (loader != null) {
            String filename = loader.findLibrary(libraryName);
            
            String error = doLoad(filename, loader);
            if (error != null) {
                throw new UnsatisfiedLinkError(error);
            }
            return;
        }
	......
      //下面删掉代码为根据平台加载对应的library，如x86、arm等
    }


	private String doLoad(String name, ClassLoader loader) {
        
  		String librarySearchPath = null;
        if (loader != null && loader instanceof BaseDexClassLoader) {
            BaseDexClassLoader dexClassLoader = (BaseDexClassLoader) loader;
            librarySearchPath = dexClassLoader.getLdLibraryPath();
        }
        // nativeLoad should be synchronized so there's only one LD_LIBRARY_PATH in use regardless
        // of how many ClassLoaders are in the system, but dalvik doesn't support synchronized
        // internal natives.
        synchronized (this) {
            return nativeLoad(name, loader, librarySearchPath);
        }
    }

// TODO: should be synchronized, but dalvik doesn't support synchronized internal natives.
    private static native String nativeLoad(String filename, ClassLoader loader,
                                            String librarySearchPath);

```

---

可以看到，到最后将会调用nativeLoad进入到native代码，接下来我们看一看native层的代码

nativeLoad代码位于`./libcore/ojluni/src/main/native/Runtime.c` 

在以前的安卓源码中，相关定义是放在Dalvik虚拟机下的，后来换了ART，路径也有所变化，为此我找了很久

```c
// ./libcore/ojluni/src/main/native/Runtime.c

JNIEXPORT jstring JNICALL
Runtime_nativeLoad(JNIEnv* env, jclass ignored, jstring javaFilename,
                   jobject javaLoader, jstring javaLibrarySearchPath)
{
    return JVM_NativeLoad(env, javaFilename, javaLoader, javaLibrarySearchPath);
}

//然后跳转到art目录下
// ./art/runtime/openjdkjvm/OpenjdkJvm.cc
JNIEXPORT jstring JVM_NativeLoad(JNIEnv* env,
                                 jstring javaFilename,
                                 jobject javaLoader,
                                 jstring javaLibrarySearchPath) {
  ScopedUtfChars filename(env, javaFilename);
  if (filename.c_str() == NULL) {
    return NULL;
  }

  std::string error_msg;
  {
    art::JavaVMExt* vm = art::Runtime::Current()->GetJavaVM();
    bool success = vm->LoadNativeLibrary(env,
                                         filename.c_str(),
                                         javaLoader,
                                         javaLibrarySearchPath,
                                         &error_msg);
    if (success) {
      return nullptr;
    }
  }
  
    // Don't let a pending exception from JNI_OnLoad cause a CheckJNI issue with NewStringUTF.
  env->ExceptionClear();
  return env->NewStringUTF(error_msg.c_str());
}
```

经过一系列的跳转，终于跑到关键的函数了

这个函数太长，不便于看思路，所以我只保留下了一些关键的代码

```cpp
// ./art/runtime/java_vm_ext.cc

bool JavaVMExt::LoadNativeLibrary(JNIEnv* env,
                                  const std::string& path,
                                  jobject class_loader,
                                  jstring library_path,
                                  std::string* error_msg) {
  
  //检查该NativeLibrary是否已加载
  ......

  // Open the shared library.  Because we're using a full path, the system
  // doesn't have to search through LD_LIBRARY_PATH.  (It may do so to
  // resolve this library's dependencies though.)

  // Failures here are expected when java.library.path has several entries
  // and we have to hunt for the lib.

  // Below we dlopen but there is no paired dlclose, this would be necessary if we supported
  // class unloading. Libraries will only be unloaded when the reference count (incremented by
  // dlopen) becomes zero from dlclose.

  Locks::mutator_lock_->AssertNotHeld(self);
  const char* path_str = path.empty() ? nullptr : path.c_str();
  void* handle = android::OpenNativeLibrary(env,
                                            runtime_->GetTargetSdkVersion(),
                                            path_str,
                                            class_loader,
                                            library_path);

  bool needs_native_bridge = false;
  if (handle == nullptr) {
    if (android::NativeBridgeIsSupported(path_str)) {
      handle = android::NativeBridgeLoadLibrary(path_str, RTLD_NOW);
      needs_native_bridge = true;
    }
  }

  VLOG(jni) << "[Call to dlopen(\"" << path << "\", RTLD_NOW) returned " << handle << "]";

  if (handle == nullptr) {
    *error_msg = dlerror();
    VLOG(jni) << "dlopen(\"" << path << "\", RTLD_NOW) failed: " << *error_msg;
    return false;
  }

 ......
  
  VLOG(jni) << "[Added shared library \"" << path << "\" for ClassLoader " << class_loader << "]";
 ......
  bool was_successful = false;
  void* sym;
  if (needs_native_bridge) {
    library->SetNeedsNativeBridge();
  }
  sym = library->FindSymbol("JNI_OnLoad", nullptr);
  if (sym == nullptr) {
    VLOG(jni) << "[No JNI_OnLoad found in \"" << path << "\"]";
    was_successful = true;
  } else {
    // Call JNI_OnLoad.  We have to override the current class
    // loader, which will always be "null" since the stuff at the
    // top of the stack is around Runtime.loadLibrary().  (See
    // the comments in the JNI FindClass function.)
    ScopedLocalRef<jobject> old_class_loader(env, env->NewLocalRef(self->GetClassLoaderOverride()));
    self->SetClassLoaderOverride(class_loader);

    VLOG(jni) << "[Calling JNI_OnLoad in \"" << path << "\"]";
    typedef int (*JNI_OnLoadFn)(JavaVM*, void*);
    JNI_OnLoadFn jni_on_load = reinterpret_cast<JNI_OnLoadFn>(sym);
    int version = (*jni_on_load)(this, nullptr);

    if (runtime_->GetTargetSdkVersion() != 0 && runtime_->GetTargetSdkVersion() <= 21) {
      fault_manager.EnsureArtActionInFrontOfSignalChain();
    }

    self->SetClassLoaderOverride(old_class_loader.get());

 ......
   //Version check
    VLOG(jni) << "[Returned " << (was_successful ? "successfully" : "failure")
              << " from JNI_OnLoad in \"" << path << "\"]";
  }

  library->SetResult(was_successful);
  return was_successful;
}
```

排除去一些版本检测，一些操作的语句，其实这个函数做的就是两个步骤

1. 调用`android::OpenNativeLibrary`，动态加载so到内存
2. 从so中找到`JNI_Onload`函数，并调用。`sym = library->FindSymbol("JNI_OnLoad", nullptr);`

---

然后我们再找到`android::OpenNativeLibrary`的源码

```cpp
//./system/core/libnativeloader/include/nativeloader/native_loader.cpp

void* OpenNativeLibrary(JNIEnv* env,
                        int32_t target_sdk_version,
                        const char* path,
                        jobject class_loader,
                        jstring library_path) {
#if defined(__ANDROID__)
  UNUSED(target_sdk_version);
  if (class_loader == nullptr) {
    return dlopen(path, RTLD_NOW);
  }

  std::lock_guard<std::mutex> guard(g_namespaces_mutex);
  android_namespace_t* ns = g_namespaces->FindNamespaceByClassLoader(env, class_loader);

  if (ns == nullptr) {
    // This is the case where the classloader was not created by ApplicationLoaders
    // In this case we create an isolated not-shared namespace for it.
    ns = g_namespaces->Create(env, class_loader, false, library_path, nullptr);
    if (ns == nullptr) {
      return nullptr;
    }
  }

  android_dlextinfo extinfo;
  extinfo.flags = ANDROID_DLEXT_USE_NAMESPACE;
  extinfo.library_namespace = ns;

  return android_dlopen_ext(path, RTLD_NOW, &extinfo);
#else
  UNUSED(env, target_sdk_version, class_loader, library_path);
  return dlopen(path, RTLD_NOW);
#endif
}
```

思路也很简单，这里就是调用了`dlopen`

然后后面对应还有`dlclose` `dlsym` `dlerror`等对应 programming interface to dynamic linking loader

这些函数位于

`./bionic/linker/dlfcn.cpp`

具体实现位于

`./bionic/linker/linker.cpp`

```cpp
//./bionic/linker/linker.cpp
void* do_dlopen(const char* name, int flags, const android_dlextinfo* extinfo,
                  void* caller_addr) {
  soinfo* const caller = find_containing_library(caller_addr);

  if ((flags & ~(RTLD_NOW|RTLD_LAZY|RTLD_LOCAL|RTLD_GLOBAL|RTLD_NODELETE|RTLD_NOLOAD)) != 0) {
    DL_ERR("invalid flags to dlopen: %x", flags);
    return nullptr;
  }

  android_namespace_t* ns = get_caller_namespace(caller);

 //一些flag的设置和检查
  ......

  std::string asan_name_holder;

  const char* translated_name = name;
  if (g_is_asan) {
    if (file_is_in_dir(name, kSystemLibDir)) {
      asan_name_holder = std::string(kAsanSystemLibDir) + "/" + basename(name);
      if (file_exists(asan_name_holder.c_str())) {
        translated_name = asan_name_holder.c_str();
        PRINT("linker_asan dlopen translating \"%s\" -> \"%s\"", name, translated_name);
      }
    } else if (file_is_in_dir(name, kVendorLibDir)) {
      asan_name_holder = std::string(kAsanVendorLibDir) + "/" + basename(name);
      if (file_exists(asan_name_holder.c_str())) {
        translated_name = asan_name_holder.c_str();
        PRINT("linker_asan dlopen translating \"%s\" -> \"%s\"", name, translated_name);
      }
    }
  }

  ProtectedDataGuard guard;
  soinfo* si = find_library(ns, translated_name, flags, extinfo, caller);
  if (si != nullptr) {
    si->call_constructors();
    return si->to_handle();
  }

  return nullptr;
}
```

这里主要关注一个关键的结构体`soinfo`

这个结构体储存了so在加载后的信息

其定义在`./bionic/linker/linker.h`

```cpp
struct soinfo {
 public:
  typedef LinkedList<soinfo, SoinfoListAllocator> soinfo_list_t;
  typedef LinkedList<android_namespace_t, NamespaceListAllocator> android_namespace_list_t;
#if defined(__work_around_b_24465209__)
 private:
  char old_name_[SOINFO_NAME_LEN];
#endif
 public:
  const ElfW(Phdr)* phdr;
  size_t phnum;
  ElfW(Addr) entry;
  ElfW(Addr) base;
  size_t size;

#if defined(__work_around_b_24465209__)
  uint32_t unused1;  // DO NOT USE, maintained for compatibility.
#endif

  ElfW(Dyn)* dynamic;

#if defined(__work_around_b_24465209__)
  uint32_t unused2; // DO NOT USE, maintained for compatibility
  uint32_t unused3; // DO NOT USE, maintained for compatibility
#endif

  soinfo* next;
 private:
  uint32_t flags_;

  const char* strtab_;
  ElfW(Sym)* symtab_;

  size_t nbucket_;
  size_t nchain_;
  uint32_t* bucket_;
  uint32_t* chain_;

#if defined(__mips__) || !defined(__LP64__)
  // This is only used by mips and mips64, but needs to be here for
  // all 32-bit architectures to preserve binary compatibility.
  ElfW(Addr)** plt_got_;
#endif

#if defined(USE_RELA)
  ElfW(Rela)* plt_rela_;
  size_t plt_rela_count_;

  ElfW(Rela)* rela_;
  size_t rela_count_;
#else
  ElfW(Rel)* plt_rel_;
  size_t plt_rel_count_;

  ElfW(Rel)* rel_;
  size_t rel_count_;
#endif

  linker_function_t* preinit_array_;
  size_t preinit_array_count_;

  linker_function_t* init_array_;
  size_t init_array_count_;
  linker_function_t* fini_array_;
  size_t fini_array_count_;

  linker_function_t init_func_;
  linker_function_t fini_func_;

#if defined(__arm__)
 public:
  // ARM EABI section used for stack unwinding.
  uint32_t* ARM_exidx;
  size_t ARM_exidx_count;
 private:
#elif defined(__mips__)
  uint32_t mips_symtabno_;
  uint32_t mips_local_gotno_;
  uint32_t mips_gotsym_;
  bool mips_relocate_got(const VersionTracker& version_tracker,
                         const soinfo_list_t& global_group,
                         const soinfo_list_t& local_group);
#if !defined(__LP64__)
  bool mips_check_and_adjust_fp_modes();
#endif
#endif
  size_t ref_count_;
 public:
  link_map link_map_head;

  bool constructors_called;

  // When you read a virtual address from the ELF file, add this
  // value to get the corresponding address in the process' address space.
  ElfW(Addr) load_bias;

#if !defined(__LP64__)
  bool has_text_relocations;
#endif
  bool has_DT_SYMBOLIC;

 public:
  soinfo(android_namespace_t* ns, const char* name, const struct stat* file_stat,
         off64_t file_offset, int rtld_flags);
  ~soinfo();

  void call_constructors();
  void call_destructors();
  void call_pre_init_constructors();
  bool prelink_image();
  bool link_image(const soinfo_list_t& global_group, const soinfo_list_t& local_group,
                  const android_dlextinfo* extinfo);
  bool protect_relro();

  void add_child(soinfo* child);
  void remove_all_links();

  ino_t get_st_ino() const;
  dev_t get_st_dev() const;
  off64_t get_file_offset() const;

  uint32_t get_rtld_flags() const;
  uint32_t get_dt_flags_1() const;
  void set_dt_flags_1(uint32_t dt_flags_1);

  soinfo_list_t& get_children();
  const soinfo_list_t& get_children() const;

  soinfo_list_t& get_parents();

  bool find_symbol_by_name(SymbolName& symbol_name,
                           const version_info* vi,
                           const ElfW(Sym)** symbol) const;

  ElfW(Sym)* find_symbol_by_address(const void* addr);
  ElfW(Addr) resolve_symbol_address(const ElfW(Sym)* s) const;

  const char* get_string(ElfW(Word) index) const;
  bool can_unload() const;
  bool is_gnu_hash() const;

  bool inline has_min_version(uint32_t min_version __unused) const {
#if defined(__work_around_b_24465209__)
    return (flags_ & FLAG_NEW_SOINFO) != 0 && version_ >= min_version;
#else
    return true;
#endif
  }

  bool is_linked() const;
  bool is_linker() const;
  bool is_main_executable() const;

  void set_linked();
  void set_linker_flag();
  void set_main_executable();
  void set_nodelete();

  void increment_ref_count();
  size_t decrement_ref_count();

  soinfo* get_local_group_root() const;

  void set_soname(const char* soname);
  const char* get_soname() const;
  const char* get_realpath() const;
  const ElfW(Versym)* get_versym(size_t n) const;
  ElfW(Addr) get_verneed_ptr() const;
  size_t get_verneed_cnt() const;
  ElfW(Addr) get_verdef_ptr() const;
  size_t get_verdef_cnt() const;

  bool find_verdef_version_index(const version_info* vi, ElfW(Versym)* versym) const;

  uint32_t get_target_sdk_version() const;

  void set_dt_runpath(const char *);
  const std::vector<std::string>& get_dt_runpath() const;
  android_namespace_t* get_primary_namespace();
  void add_secondary_namespace(android_namespace_t* secondary_ns);

  void set_mapped_by_caller(bool reserved_map);
  bool is_mapped_by_caller() const;

  uintptr_t get_handle() const;
  void generate_handle();
  void* to_handle();

 private:
  bool elf_lookup(SymbolName& symbol_name, const version_info* vi, uint32_t* symbol_index) const;
  ElfW(Sym)* elf_addr_lookup(const void* addr);
  bool gnu_lookup(SymbolName& symbol_name, const version_info* vi, uint32_t* symbol_index) const;
  ElfW(Sym)* gnu_addr_lookup(const void* addr);

  bool lookup_version_info(const VersionTracker& version_tracker, ElfW(Word) sym,
                           const char* sym_name, const version_info** vi);

  void call_array(const char* array_name, linker_function_t* functions, size_t count, bool reverse);
  void call_function(const char* function_name, linker_function_t function);
  template<typename ElfRelIteratorT>
  bool relocate(const VersionTracker& version_tracker, ElfRelIteratorT&& rel_iterator,
                const soinfo_list_t& global_group, const soinfo_list_t& local_group);

 private:
  // This part of the structure is only available
  // when FLAG_NEW_SOINFO is set in this->flags.
  uint32_t version_;

  // version >= 0
  dev_t st_dev_;
  ino_t st_ino_;

  // dependency graph
  soinfo_list_t children_;
  soinfo_list_t parents_;

  // version >= 1
  off64_t file_offset_;
  uint32_t rtld_flags_;
  uint32_t dt_flags_1_;
  size_t strtab_size_;

  // version >= 2

  size_t gnu_nbucket_;
  uint32_t* gnu_bucket_;
  uint32_t* gnu_chain_;
  uint32_t gnu_maskwords_;
  uint32_t gnu_shift2_;
  ElfW(Addr)* gnu_bloom_filter_;

  soinfo* local_group_root_;

  uint8_t* android_relocs_;
  size_t android_relocs_size_;

  const char* soname_;
  std::string realpath_;

  const ElfW(Versym)* versym_;

  ElfW(Addr) verdef_ptr_;
  size_t verdef_cnt_;

  ElfW(Addr) verneed_ptr_;
  size_t verneed_cnt_;

  uint32_t target_sdk_version_;

  // version >= 3
  std::vector<std::string> dt_runpath_;
  android_namespace_t* primary_namespace_;
  android_namespace_list_t secondary_namespaces_;
  uintptr_t handle_;

  friend soinfo* get_libdl_info();
};
```

关注一下可以发现，soinfo结构体里包含的只有`segment table`的信息，并不包含section的信息

---

注意发现，其获取soinfo的信息是通过一个叫`find_containing_library` 的方法，这个是检测so文件是否已经加载到内存中，如果已经加载到内存中，就可以直接return了。

如果尚未加载到内存中，则会通过`find_library` 函数获取soinfo信息

```cpp
// ./bioni/linker/linker.cpp

static soinfo* find_library(android_namespace_t* ns,
                            const char* name, int rtld_flags,
                            const android_dlextinfo* extinfo,
                            soinfo* needed_by) {
  soinfo* si;
  if (name == nullptr) {
    si = somain;
  } else if (!find_libraries(ns, needed_by, &name, 1, &si, nullptr, 0, rtld_flags,
                             extinfo, /* add_as_children */ false)) {
    return nullptr;
  }
  return si;
}

static bool find_libraries(android_namespace_t* ns,
                           soinfo* start_with,
                           const char* const library_names[],
                           size_t library_names_count, soinfo* soinfos[],
                           std::vector<soinfo*>* ld_preloads,
                           size_t ld_preloads_count, int rtld_flags,
                           const android_dlextinfo* extinfo,
                           bool add_as_children) {
  // Step 0: prepare.
  LoadTaskList load_tasks;
  std::unordered_map<const soinfo*, ElfReader> readers_map;

  for (size_t i = 0; i < library_names_count; ++i) {
    const char* name = library_names[i];
    load_tasks.push_back(LoadTask::create(name, start_with, &readers_map));
  }

  // Construct global_group.
  soinfo::soinfo_list_t global_group = make_global_group(ns);

  // If soinfos array is null allocate one on stack.
  // The array is needed in case of failure; for example
  // when library_names[] = {libone.so, libtwo.so} and libone.so
  // is loaded correctly but libtwo.so failed for some reason.
  // In this case libone.so should be unloaded on return.
  // See also implementation of failure_guard below.

  if (soinfos == nullptr) {
    size_t soinfos_size = sizeof(soinfo*)*library_names_count;
    soinfos = reinterpret_cast<soinfo**>(alloca(soinfos_size));
    memset(soinfos, 0, soinfos_size);
  }

  // list of libraries to link - see step 2.
  size_t soinfos_count = 0;

  auto scope_guard = make_scope_guard([&]() {
    for (LoadTask* t : load_tasks) {
      LoadTask::deleter(t);
    }
  });

  auto failure_guard = make_scope_guard([&]() {
    // Housekeeping
    soinfo_unload(soinfos, soinfos_count);
  });

  ZipArchiveCache zip_archive_cache;

  // Step 1: expand the list of load_tasks to include
  // all DT_NEEDED libraries (do not load them just yet)
  for (size_t i = 0; i<load_tasks.size(); ++i) {
    LoadTask* task = load_tasks[i];
    soinfo* needed_by = task->get_needed_by();

    bool is_dt_needed = needed_by != nullptr && (needed_by != start_with || add_as_children);
    task->set_extinfo(is_dt_needed ? nullptr : extinfo);
    task->set_dt_needed(is_dt_needed);

    if(!find_library_internal(ns, task, &zip_archive_cache, &load_tasks, rtld_flags)) {
      return false;
    }

    soinfo* si = task->get_soinfo();

    if (is_dt_needed) {
      needed_by->add_child(si);
    }

    if (si->is_linked()) {
      si->increment_ref_count();
    }

    // When ld_preloads is not null, the first
    // ld_preloads_count libs are in fact ld_preloads.
    if (ld_preloads != nullptr && soinfos_count < ld_preloads_count) {
      ld_preloads->push_back(si);
    }

    if (soinfos_count < library_names_count) {
      soinfos[soinfos_count++] = si;
    }
  }

  // Step 2: Load libraries in random order (see b/24047022)
  LoadTaskList load_list;
  for (auto&& task : load_tasks) {
    soinfo* si = task->get_soinfo();
    auto pred = [&](const LoadTask* t) {
      return t->get_soinfo() == si;
    };

    if (!si->is_linked() &&
        std::find_if(load_list.begin(), load_list.end(), pred) == load_list.end() ) {
      load_list.push_back(task);
    }
  }
  shuffle(&load_list);

  for (auto&& task : load_list) {
    if (!task->load()) {
      return false;
    }
  }

  // Step 3: pre-link all DT_NEEDED libraries in breadth first order.
  for (auto&& task : load_tasks) {
    soinfo* si = task->get_soinfo();
    if (!si->is_linked() && !si->prelink_image()) {
      return false;
    }
  }

  // Step 4: Add LD_PRELOADed libraries to the global group for
  // future runs. There is no need to explicitly add them to
  // the global group for this run because they are going to
  // appear in the local group in the correct order.
  if (ld_preloads != nullptr) {
    for (auto&& si : *ld_preloads) {
      si->set_dt_flags_1(si->get_dt_flags_1() | DF_1_GLOBAL);
    }
  }


  // Step 5: link libraries.
  soinfo::soinfo_list_t local_group;
  walk_dependencies_tree(
      (start_with != nullptr && add_as_children) ? &start_with : soinfos,
      (start_with != nullptr && add_as_children) ? 1 : soinfos_count,
      [&] (soinfo* si) {
    local_group.push_back(si);
    return true;
  });

  // We need to increment ref_count in case
  // the root of the local group was not linked.
  bool was_local_group_root_linked = local_group.front()->is_linked();

  bool linked = local_group.visit([&](soinfo* si) {
    if (!si->is_linked()) {
      if (!si->link_image(global_group, local_group, extinfo)) {
        return false;
      }
    }

    return true;
  });

  if (linked) {
    local_group.for_each([](soinfo* si) {
      if (!si->is_linked()) {
        si->set_linked();
      }
    });

    failure_guard.disable();
  }

  if (!was_local_group_root_linked) {
    local_group.front()->increment_ref_count();
  }

  return linked;
}

```

注意到，27行有一个`ELFReader` ，找到他的定义处

```cpp
// ./bionic/linker/linker_phdr.h

class ElfReader {
 public:
  ElfReader();

  bool Read(const char* name, int fd, off64_t file_offset, off64_t file_size);
  bool Load(const android_dlextinfo* extinfo);

  const char* name() const { return name_.c_str(); }
  size_t phdr_count() const { return phdr_num_; }
  ElfW(Addr) load_start() const { return reinterpret_cast<ElfW(Addr)>(load_start_); }
  size_t load_size() const { return load_size_; }
  ElfW(Addr) load_bias() const { return load_bias_; }
  const ElfW(Phdr)* loaded_phdr() const { return loaded_phdr_; }
  const ElfW(Dyn)* dynamic() const { return dynamic_; }
  const char* get_string(ElfW(Word) index) const;
  bool is_mapped_by_caller() const { return mapped_by_caller_; }

 private:
  bool ReadElfHeader();
  bool VerifyElfHeader();
  bool ReadProgramHeaders();
  bool ReadSectionHeaders();
  bool ReadDynamicSection();
  bool ReserveAddressSpace(const android_dlextinfo* extinfo);
  bool LoadSegments();
  bool FindPhdr();
  bool CheckPhdr(ElfW(Addr));
  bool CheckFileRange(ElfW(Addr) offset, size_t size, size_t alignment);

  bool did_read_;
  bool did_load_;
  std::string name_;
  int fd_;
  off64_t file_offset_;
  off64_t file_size_;

  ElfW(Ehdr) header_;
  size_t phdr_num_;

  MappedFileFragment phdr_fragment_;
  const ElfW(Phdr)* phdr_table_;

  MappedFileFragment shdr_fragment_;
  const ElfW(Shdr)* shdr_table_;
  size_t shdr_num_;

  MappedFileFragment dynamic_fragment_;
  const ElfW(Dyn)* dynamic_;

  MappedFileFragment strtab_fragment_;
  const char* strtab_;
  size_t strtab_size_;

  // First page of reserved address space.
  void* load_start_;
  // Size in bytes of reserved address space.
  size_t load_size_;
  // Load bias.
  ElfW(Addr) load_bias_;

  // Loaded phdr.
  const ElfW(Phdr)* loaded_phdr_;

  // Is map owned by the caller
  bool mapped_by_caller_;
};
```

从其private的方法就能看出，有一系列读取ELF信息的操作。

这里要说明一下，这里显然有`ReadSectionHeaders()` 和 `ReadDynamicSection()` 两个函数，这在4.x的安卓源码中是没有的，别的没看过，并不清楚。

在ELFReader中也对应多了一个`Read`的函数，这是用来读取一个ELF的信息，但是真正运行的时候只需要其加载到内存中，也即是`Load` 函数。而`Load`函数中则只需要segment的信息

```cpp
bool ElfReader::Read(const char* name, int fd, off64_t file_offset, off64_t file_size) {
  CHECK(!did_read_);
  CHECK(!did_load_);
  name_ = name;
  fd_ = fd;
  file_offset_ = file_offset;
  file_size_ = file_size;

  if (ReadElfHeader() &&
      VerifyElfHeader() &&
      ReadProgramHeaders() &&
      ReadSectionHeaders() &&
      ReadDynamicSection()) {
    did_read_ = true;
  }

  return did_read_;
}

bool ElfReader::Load(const android_dlextinfo* extinfo) {
  CHECK(did_read_);
  CHECK(!did_load_);
  if (ReserveAddressSpace(extinfo) &&
      LoadSegments() &&
      FindPhdr()) {
    did_load_ = true;
  }

  return did_load_;
}
```



总的来说，在Android运行时，对于一个so，只需要用到它的segment信息，由此可以对section部分进行删除，而ida等工具静态分析都是通过section的信息进行分析的，所以由此可以有效地防止so被静态分析逆向。

---

注意到下面还有调用`si->call_constructors()`

```cpp
void soinfo::call_constructors() {
  if (constructors_called) {
    return;
  }

  // We set constructors_called before actually calling the constructors, otherwise it doesn't
  // protect against recursive constructor calls. One simple example of constructor recursion
  // is the libc debug malloc, which is implemented in libc_malloc_debug_leak.so:
  // 1. The program depends on libc, so libc's constructor is called here.
  // 2. The libc constructor calls dlopen() to load libc_malloc_debug_leak.so.
  // 3. dlopen() calls the constructors on the newly created
  //    soinfo for libc_malloc_debug_leak.so.
  // 4. The debug .so depends on libc, so CallConstructors is
  //    called again with the libc soinfo. If it doesn't trigger the early-
  //    out above, the libc constructor will be called again (recursively!).
  constructors_called = true;

  if (!is_main_executable() && preinit_array_ != nullptr) {
    // The GNU dynamic linker silently ignores these, but we warn the developer.
    PRINT("\"%s\": ignoring DT_PREINIT_ARRAY in shared library!", get_realpath());
  }

  get_children().for_each([] (soinfo* si) {
    si->call_constructors();
  });

  TRACE("\"%s\": calling constructors", get_realpath());

  // DT_INIT should be called before DT_INIT_ARRAY if both are present.
  call_function("DT_INIT", init_func_);
  call_array("DT_INIT_ARRAY", init_array_, init_array_count_, false);
}
```

这个函数通过执行`.init(_array)`定义的内容，从而完成so的初始化

---

`dlopen`的内容就不继续探究了，这里主要是有关Linux动态链接库方面的内容了。

总结一下

Android对so的加载

1. 通过`System.loadLibrary` 传递so的路径、classLoader等信息，然后进入native代码
2. 进入native代码后，通过调用`android::OpenNativeLibrary` 函数，跳转到dlopen，加载so到内存
3. 在dlopen中，会通过执行`.init(array)` 的内容进行so的一些动态链接信息初始化
4. `android::OpenNativeLibrary` 执行完后，会调用so中`JNI_Onload`函数，从而完成native函数的注册


---

###### Android 7.1.1的代码相比4.x的源码复杂了好多……

