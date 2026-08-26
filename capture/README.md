# TurboUtils Capture

`TurboUtils::Capture` 是可选的原生音频、摄像头与屏幕采集组件。它保留
`turbo_capture.h` 的 C 接口、枚举值、结构布局和符号名称；播放、编解码、录制、
RTSP、WebRTC 以及移动端 Java/Swift 包装不属于该组件。

## 构建与链接

组件默认关闭。通过 CMake 启用时，同时选择 vcpkg 的 `capture` feature：

```sh
cmake -S . -B build/capture \
  -DTURBO_ENABLE_CAPTURE=ON \
  -DVCPKG_MANIFEST_FEATURES=capture
cmake --build build/capture
```

仓库提供 `win-capture-{dev,release}-user` 与
`linux-capture-{dev,release}-user` configure/build/test/install presets。安装后，
消费端只依赖导出的组件：

```cmake
find_package(TurboUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE TurboUtils::Capture)
```

Windows 使用 Media Foundation、DirectShow、D3D11 与 DXGI；Linux 配置时要求
PipeWire 0.3、X11 和 Xext；macOS/iOS 使用系统 Framework；Android 使用 NDK
Camera2、Media、OpenSL ES 和 NativeWindow。miniaudio 与 libyuv 是实现依赖，其类型
不会进入公开头文件。

## 所有权、线程与关闭

- capture 实例拥有原生句柄和格式转换缓冲区。
- 音频块或视频帧是只读 borrowed view，只在同步 callback 返回前有效。需要保留数据
  的调用方必须在 callback 内复制。
- view 不得跨 callback 返回、`stop`、`destroy` 或协程挂起保存。
- 每个实例只有一个原生 producer 路径；组件不增加队列、fan-out 或无界缓存。慢
  callback 可能增加延迟或使原生后端丢帧。
- create/start/stop/destroy、callback setter 和设备控制属于 control plane；同一实例
  需要由调用方串行化，不能从该实例自己的 callback 中调用 stop/destroy。
- 正常关闭顺序是先 `turbo_capture_stop()`，等待后端停止投递，再调用
  `turbo_capture_destroy()`。启动失败进入 `TURBO_CAPTURE_STATE_ERROR`，不会报告
  `RUNNING`。

设备不存在时，枚举返回零项。参数、权限、格式协商、原生资源或模式身份无效时接口会
明确失败，不会静默切换为另一种设备语义。Android 屏幕采集的 native 生命周期可以先
启动，但应用层仍必须完成 Java MediaProjection 授权与 ImageReader surface 交接后才会
产生视频帧。

## 下游迁移

TurboMedia 应在单独的改动中将 `TurboMedia::Device` 调用方切换到已安装的
`TurboUtils::Capture`，运行其设备与打包测试后，再删除 TurboMedia 中重复的 capture
源码。该顺序使回滚只需恢复链接目标，不需要恢复已删除的实现文件。

Windows 和 Android arm64 可由本仓库 presets 在当前环境编译验证。Linux、macOS 与 iOS
仍应在对应原生 runner 上完成 configure、link、生命周期和有硬件/权限条件的设备测试。
