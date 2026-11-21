# B 方案研究：recovery = iOS18，boot = Android

## 为什么必须动分区

一键包 loader 在 **darwin 分区偏移 0xC0000000（3 GiB）** 处读 `RAPHIOS1` 资源容器
（安装包的 `disk.py` 里 `RESOURCE_OFFSET=0xc0000000`，loader 内核里同一常量）。
iOS16 的系统卷只有 2.78 GiB，正好在资源区之前；我们的 iOS18 HFS 根卷从偏移 0 开始、
已用 16 GiB，把 3 GiB 处盖住了，所以 loader 读不到资源。

结论：**不改 loader 就必须重排 darwin**：
- darwin（前 4 GiB）：偏移 3 GiB 处放 RAPHIOS1
- 新分区 iosroot（20 GiB，紧随其后）：iOS18 的 HFS 根卷
- 原 16 GiB 根卷需要先搬走再写资源区（顺序不能反）

## 已验证的部分

`tools/raphael/mk-resources.py` 能生成 loader 认的容器，并用安装包的
`device_resources.header()` 校验通过（magic/版本/几何/CRC 全部通过）：

```
python3 tools/raphael/mk-resources.py \
  --kernel wifi-test/art-uc/kernel.development.sweet \
  --adt tools/raphael/raphael-wlan3.adt \
  --cmdline "$(cat /tmp/rs-cmdline.txt)" \
  --out resources.bin
```

- XNUP 条目：`kernel`（Mach-O）、`adt`（我们的 sidecar，给支持它的 loader）、`cmdline`（608 字节）
- 容器：4 KiB 头 + payload@4096 + resident@对齐偏移，CRC32 三处都写对
- 生成物 20,029,440 B，sha256 `357549d5…`

## 待验证 / 待做

1. **loader 是否认我们的 kernel/adt**：需要真机试；payload 里没有 ramdisk（我们的内核用
   `rd=ufsroot0 ufsroot=darwin` 直接挂根），若 loader 强制要 ramdisk 再补
2. **搬迁流程**（TWRP + adb root）：
   - `adb exec-out dd if=/dev/block/by-name/darwin bs=1M count=16384` 备份前 16 GiB 到主机
   - 用安装包 `disk.py` 的 GPT 写入逻辑：缩 darwin 到 4 GiB，紧跟创建 iosroot（APFS 类型）
   - 把备份写回 iosroot 起始处（`dd of=/dev/block/by-name/iosroot`）
   - 用 `mk-resources.py` 的产物写到 darwin 偏移 3 GiB
   - boot=Android（boot.emmc.win），recovery=一键包 ios-recovery.img
   - `cmdline` 里把 `ufsroot=darwin` 改成 `ufsroot=iosroot`
3. **风险**：GPT 编辑 + 32 GiB 数据往返；iOS 根卷有源镜像可恢复，Android userdata 不动
