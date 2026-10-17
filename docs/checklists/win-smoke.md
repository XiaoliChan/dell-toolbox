# Windows 真机冒烟清单(G3 3590)

每次发版前在真机走一遍;任何一步失败即阻断发版。
本清单为待验证项；Linux/offscreen 单元测试不代表 Windows 真机验证通过。

## 安装与启动
- [ ] 解压 portable zip 到任意目录,双击 `dell-toolbox.exe`,UAC 通过
- [ ] 冷启动到主窗口出现 < 1s(对比 Dell Power Manager 数秒)
- [ ] 无 `dell-toolbox.log` 中的 error 行(`--verbose` 检查)

## doctor
- [ ] `dell-toolbox.exe --doctor`:thermal PASS 且 fans: 2;charge 若 FAIL 有明确指引
- [ ] 无管理员权限运行 → UAC 或明确失败信息(不应静默)

## 热控制
- [ ] 逐项切换 Quiet / Cool / Balanced / Performance / G-Mode / Custom:托盘图标温度刷新,风扇行为变化可闻
- [ ] Custom 模式拖曲线点 → 松手后 ≤2s 风扇转速变化(2% 死区内不抖动)
- [ ] 曲线保留至少 2 点，重复温度不生成；重置恢复默认曲线
- [ ] 重启应用后曲线/模式/场景参数保留(dell-toolbox.ini)

## 场景检测
- [ ] scene 参数加 `game.exe`,运行任一游戏 → 10s 内 active 策略变 scene、模式切 G-Mode
- [ ] 关游戏 → 30s 后回到 baseline/Balanced
- [ ] GPU 60%+ 压力测试(gfurmark/3DMark)同样触发;55–60% 区间不反复切换

## 性能页与 UI
- [ ] 下拉框无黑色原生边框，100% / 150% / 200% DPI 均可读
- [ ] 打开电源方案下拉并用键盘高亮第二项，等待 5 秒仍保持高亮
- [ ] 选择电源方案后回读确认实际方案；powercfg 失败不显示伪成功
- [ ] 新建/编辑/保存功耗草稿明确显示未应用，不改变 CPU/GPU 功耗、不切换 active=manual
- [ ] 场景确认时间 120 秒切为 2 分钟再切回，配置仍为 120 秒
- [ ] Restore defaults 后开关、场景 Enabled、单位范围与配置一致
- [ ] AWCC monitor-only 时电源方案、充电模式不写硬件
- [ ] 本版本没有应用层 Failsafe 策略或 Resume auto 按钮，不按旧版本清单验证

## 电池页
- [ ] 充电模式切换后 `cctk --PrimaryBattChargeCfg` 输出一致
- [ ] Custom 起止阈值差 <5 被强制拉开;写入后 cctk 回读正确

## 退出
- [ ] 托盘 Exit → 模式恢复 Balanced,进程退出,无残留高转速
- [ ] 关闭窗口 → 藏到托盘,提示一次

## 自启
- [ ] Settings 开自启 → `schtasks /query /tn DellToolbox` 存在
- [ ] 注销重登 → 进程以 --minimized 启动且托盘可见

## 启动失败排查(side-by-side / 打不开)
1. **必须整目录删除后重新解压**,不要在旧目录里覆盖解压(旧 DLL/清单残留会复现旧错误)
2. 若仍报 "side-by-side configuration is incorrect":
   - 打开 事件查看器 → Windows 日志 → 应用程序,找来源为 **SideBySide** 的最新条目,截图发回(内含确切解析错误行号)
   - 管理员 cmd 跑 `sfc /scannow` 修复组件存储后再试
   - 或装一次 [VC++ 2015-2022 x64 运行库](https://aka.ms/vs/17/release/vc_redist.x64.exe)(包内已自带,此步只为排除系统存储损坏)
3. 报 "找不到 VCRUNTIME140.dll":说明解压时 DLL 丢失/被杀软隔离,检查 zip 完整性与杀软白名单
