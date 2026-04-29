## 项目简介
本组件提供基于 NetworkManager (nmcli) 的 WiFi 管理接口实现，封装了扫描、连接、断开、热点(AP)等常见功能，便于在 Linux 设备上统一使用 `wifi.h` 的 API。

## 功能特性
支持：
- STA 模式扫描、连接、断开、获取当前连接信息
- 列出已保存网络、删除保存网络
- 自动重连开关、按 SSID 自动连接
- AP 启动/关闭、获取 AP 配置缓存
- 获取/设置 MAC（设置后自动重连以生效）
- 可选注册消息回调（事件通知）

不支持或有限支持：
- 依赖 NetworkManager/nmcli，非 NetworkManager 环境不可用
- Linkd 相关协议接口未实现（返回不支持）
- AP 配置获取仅返回本组件内部缓存（非实时查询）

## 快速开始
### 环境准备
- Linux 系统已安装并运行 NetworkManager
- `nmcli` 命令可用

### 构建编译
仅使用本目录进行独立编译（脱离 SDK）：
```bash
mkdir -p build
cd build
cmake .. -DBUILD_TESTS=ON
make -j4
```

### 运行示例
```bash
./test_wifi_demo scan
./test_wifi_demo connect <ssid> [password]
./test_wifi_demo info
./test_wifi_demo disconnect
./test_wifi_demo list
./test_wifi_demo setmac wlan0 a8:a0:92:1f:c7:39
```
如需事件回调：
```bash
./test_wifi_demo cb scan
```

## 详细使用
保留，引用到后续的官方文档。

## 常见问题
- 扫描结果为 0：确认 `nmcli device wifi list` 是否有输出，NetworkManager 是否运行。
- `wifi_init` 失败：检查 `nmcli -t -f RUNNING general` 返回值。
- 设置 MAC 后未生效：需要自动重连，且硬件/驱动需支持克隆 MAC。

## 版本与发布
版本以本目录 `package.xml` 中的 `<version>` 为准。

| 版本   | 日期       | 说明 |
| ------ | ---------- | ---- |
| 0.1.0  | 2026-02-28 | 初始版本，支持wifi连接，开启ap等 |

## 贡献方式
欢迎参与贡献：提交 Issue 反馈问题，或通过 Pull Request 提交代码。

- **编码规范**：本组件 C 代码遵循 [Google C++ 风格指南](https://google.github.io/styleguide/cppguide.html)（C 相关部分），请按该规范编写与修改代码。
- **提交前检查**：请在提交前运行本仓库的 lint 脚本，确保通过风格检查：
  ```bash
  # 在仓库根目录执行（检查全仓库）
  bash scripts/lint/lint_cpp.sh

  # 仅检查本组件
  bash scripts/lint/lint_cpp.sh components/peripherals/wifi
  ```
  脚本路径：`scripts/lint/lint_cpp.sh`。若未安装 `cpplint`，可先执行：`pip install cpplint` 或 `pipx install cpplint`。
- **提交说明**：提交 Issue 或 PR 前请描述硬件型号、GPIO 编号与复现步骤。


## License
本组件源码文件头声明为 Apache-2.0，最终以本目录 `LICENSE` 文件为准。
