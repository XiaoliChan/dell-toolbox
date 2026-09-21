# AWCC 逆向分析 — 00 组件清单

> 来源:官方公开分发的 `Alienware OC Controls Application 1.0.4.0`(driverid 33dp8,AWCC 5.x 世代的超频/热管理组件,G3 3590 适用)。
> 方法:InstallShield setup.exe 无法在 Linux 静态解包(ISSetupStream v3 表区混淆),改由 GitHub Actions Windows runner 在安装程序弹窗等待时收割 %TEMP% 解包产物,再对主 MSI 做 `msiexec /a` 管理镜像展开;托管程序集用 ilspycmd 8.2 反编译。
> 约束:只记录行为与参数,不复制代码;本目录产出仅文档。

## 外层结构

| 层 | 内容 | 格式 |
|---|---|---|
| AWCC Full Installer 5.10.2.0 (RK19X) | AWCCInstallationManager.exe + 1.16GB overlay | overlay 全域熵 8.00(加密),放弃静态解包 |
| OC Controls External zip | setup.exe (InstallShield 22) + setup.iss | ISSetupStream v3,表区混淆,不可静态解包 |
| OC Controls.msi(收割产物) | 18.5MB,管理镜像展开成功 | 标准 MSI |

## 组件清单(msiadmin 展开,非 Intel 部分在 `Program Files 64/Alienware/OCControls/`)

### 托管(.NET,已反编译到 AWCC/decomp/)
| 文件 | 职责 | 反编译 |
|---|---|---|
| OCControls.exe | WPF 主程序(Dominator 命名空间) | OK |
| OCControls.UI.dll | 视图/ViewModel(曲线编辑、档位 UI) | OK |
| OCControls.Domain.dll | 领域模型:OverclockingModel、BIOSSupportProvider、热档位 | OK |
| OCControls.ServiceModel.dll | 服务通信:XTUSDKLibrary、BIOSSupportAPIMap、cctk 调用 | OK |
| OCControls.AWCCPlugin.dll / CommandCenter_PlugIn.dll | AWCC 宿主插件接口 | OK |
| OCControlsWindowsService.exe | 常驻服务(XTU 服务监护、通知) | OK |
| IntelOverclockingSDK.dll | Intel XTU SDK 托管封装(1.4MB 源码) | OK |
| OCControls.Tools.dll | 日志/工具 | OK |
| ProfileHelperModel.dll | 档案模型 | FAIL(混淆/混合模式) |

### Native
| 文件 | 职责 |
|---|---|
| System32/System64/DomOCBiosSupportAPI.dll | BIOS OC 状态 API,导出 5 个函数(见下) |
| Intel XTU 运行时(XtuCoreServer.dll、HardwareAccess.dll、ICCLib.dll、XtuService.exe + 驱动) | CPU 超频/功耗墙真实执行层 |

### DomOCBiosSupportAPI.dll 导出表(与 tcc-g15 WMI 文档方法号对应)
| 导出 | 对应 AWCC WMI 方法 | 语义(来自反编译调用方) |
|---|---|---|
| Initialize | — | COM/WMI 初始化,返回 BIOSInitializationStates |
| ReturnOverclockingReport | 13 | 返回位域:int byte0=CPU OC 使能;byte1=OC UI BIOS 控制(0=不支持);byte2=OC Failsafe 标志 |
| SetOCUIBIOSControl(bool) | 14 | 请求 BIOS 放开 OC 控制权 |
| ClearOCFailSafeFlag | 15 | 清除 Failsafe 状态;Dell 应用在读到标志置位时会自动调用 |
| Release | — | 释放 |

## 关键架构结论(初步)

1. **AWCC 5.x 的 OC = Intel XTU SDK**:CPU PL1/PL2/倍频/电压偏移全部经 XtuService(本地 COM 服务)执行,不是 WMI;WMI(经 DomOCBiosSupportAPI)只管 BIOS 侧 OC 使能状态与 Failsafe。
2. OC 状态机:Dell 应用轮询 `ReturnOverclockingReport`,发现 Failsafe 置位→自动 ClearOCFailSafeFlag→UI 提示。
3. 档案存 SQL Server Compact(SSCE)v3.5 本地库;首次联网"下载超频档案"即按机型从 Dell 服务器拉 OTA 数据(OTAOCDataSetupTool.exe)。
4. 热管理(模式切换/风扇曲线)不在 OC Controls 内 —— 在 AWCC 主程序(加密 bundle);其 WMI 协议以 tcc-g15 `WMI-AWCC-doc.md` 为准,调度行为对照本目录 01 文档。

后续:01-thermal-scheduling.md、02-oc-semantics.md。
