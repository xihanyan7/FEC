# Packet FEC Library

这是一个使用 C++11 实现、通过 C ABI 对外提供服务的媒体无关包级 FEC 库。DSP 或其他调用方提交的数据只被视为不透明字节串。

当前实现包括：

- `NONE` 透传 profile；
- 普通 XOR_DX：连续 X 个源包生成 1 个 repair，X 可配置为 1..32；
- 一维 XOR 交织：D8/L4、D5/L4、D4/L4、D3/L4、D4/L8；
- 系统型 RS(8,6) 和 RS(10,8)，GF(2^8) 使用 RFC 5510 的 `0x11d` 多项式和系统型 Vandermonde 生成矩阵；
- 乱序、重复包、repair-first、活动块上限、超时和迟到包抑制；
- 多接收端最差链路聚合、快速增强和缓慢降级的动态 profile 控制器；
- FEC 窗口统计以及统计到动态控制指标的转换。

## Directory

```text
include/fec/fec.h                 C ABI 公共头文件
src/fec.cpp                       C ABI 句柄和薄适配层
src/internal/fec_encoder.*        块级编码流程
src/internal/fec_decoder.*        块级解码、恢复和统计
src/internal/fec_controller.*     动态 profile 控制器
src/internal/fec_frame.*          线格式、序列化和 CRC
src/internal/fec_symbol.*         受保护逻辑符号
src/internal/fec_profile.*        profile 参数目录
src/internal/xor_codec.*          XOR 与交织恢复算法
src/internal/reed_solomon_codec.* GF(256) 和 RS 算法
tests/test_fec.cpp                编解码、故障注入和控制器测试
tests/c_api_compile.c             纯 C 编译及 ABI 调用测试
CMakeLists.txt                    CMake 构建入口
```

`src/internal` 是私有实现目录，不属于安装接口。算法模块不依赖编码器、
解码器或用户回调；`Encoder`/`Decoder` 负责块状态和工作区，`src/fec.cpp`
只负责把稳定的 C ABI 转发到 C++11 对象。

## Build

```sh
cmake -S FEC/code -B FEC/build
cmake --build FEC/build
ctest --test-dir FEC/build --output-on-failure
```

也可以直接编译进固件。库实现不依赖 RTTI 和异常，所有 `src/*.cpp` 和
`src/internal/*.cpp` 均已在下面的约束下验证：

```sh
g++ -std=c++11 -O2 -fno-exceptions -fno-rtti \
    -Iinclude -Isrc -c src/fec.cpp src/internal/*.cpp
```

## C API lifecycle

发送端基本流程：

```c
fec_config_t config;
fec_encoder_t *encoder = 0;

memset(&config, 0, sizeof(config));
config.profile = FEC_PROFILE_XOR_I_D4_L4;
config.stream_id = 1;
config.session_epoch = 1;
config.max_packet_size = 256;

fec_encoder_create(&config, on_frame, user, &encoder);
fec_encoder_push(encoder, dsp_packet, dsp_packet_size);
fec_encoder_destroy(encoder);
```

普通 XOR_DX 示例：

```c
memset(&config, 0, sizeof(config));
config.profile = FEC_PROFILE_XOR_DX;
config.xor_group_size = 4; /* 连续4个源包生成1个repair。 */
config.max_packet_size = 256;

fec_encoder_create(&config, on_frame, user, &encoder);
```

编码顺序为 `S0,S1,S2,S3,P0`，其中
`P0=S0 XOR S1 XOR S2 XOR S3`。X 的合法范围是 1..32，冗余率为
`1/X`。`fec_profile_get_info` 只适用于参数固定的 profile；XOR_DX 应调用
`fec_config_get_profile_info` 查询完整参数。

接收端基本流程：

```c
config.profile = FEC_PROFILE_AUTO;
fec_decoder_create(&config, on_packet, on_event, user, &decoder);
fec_decoder_ingest(decoder, rf_frame, rf_frame_size, now_ms);
fec_decoder_expire(decoder, now_ms);
fec_decoder_flush(decoder); /* 流结束时关闭仍在等待的块 */
fec_decoder_destroy(decoder);
```

编码器每次 `fec_encoder_push` 同步输出一个 SOURCE 帧；源块完成时还会连续输出 REPAIR 帧。因此一次 push 最多触发 `1 + repair_count` 次 frame callback。回调不支持背压，回调返回前必须完成发送或复制。

正常 SOURCE 数据会由解码器立即回调；恢复包可能稍后回调。回调携带 `source_seq`，严格按序交付由 DSP 或独立重排层负责。

所有 callback 指针只在回调期间有效，回调返回后库可以立即复用缓冲区。禁止从 callback 重入同一个 encoder/decoder 上下文。

## Profile switching

调用 `fec_encoder_request_profile` 只记录待切换 profile：

- 当前块仍使用原 profile；
- 当前块全部修复帧输出后，下一块才启用新 profile；
- 每个 FEC 帧都携带 profile；
- 自适应接收端应配置 `FEC_PROFILE_AUTO`，允许旧块和新块同时处于活动状态。

XOR_DX 的 X 来自创建编码器时的 `xor_group_size`，并写入每个帧的头部；
AUTO 解码器可以直接按帧中携带的 X 解码。当前自适应控制器不会主动选择
XOR_DX，但编码器可以在已经配置有效 X 的前提下请求切入该 profile。

动态控制流程：

1. 接收端通过 `fec_decoder_get_stats` 获取一个统计窗口。
2. 使用 `fec_link_metrics_from_stats` 转成 ppm 丢包指标。
3. 多设备指标一起传给 `fec_controller_update`，控制器按最差设备决策。
4. `changed != 0` 时，发送端调用 `fec_encoder_request_profile`。

控制器按三个维度处理波动：

- 通过 D 调节 XOR 冗余：D 越小，冗余越高；
- 长突发切换到 D4/L8；
- 同列多丢包或 FEC 后仍有残余丢包时切换到 RS(8,6)。

增强受 `min_hold_ms` 限制，但不需要累计稳定窗口；降级必须连续满足 `stable_windows_to_degrade` 个窗口。默认不会自动降到 `NONE`，避免在干净窗口后完全撤掉保护。

## Wire format

所有整数按 little-endian 手工序列化，不直接发送 C/C++ 结构体。固定头部为 32 字节：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | magic `0xC1FE` |
| 2 | 1 | version |
| 3 | 1 | profile |
| 4 | 1 | frame type |
| 5 | 1 | XOR_DX group size X；其他 profile 为 0 |
| 6 | 4 | stream id |
| 10 | 4 | session epoch |
| 14 | 4 | block id |
| 18 | 4 | block base source sequence |
| 22 | 2 | source/repair index |
| 24 | 2 | logical symbol size |
| 26 | 2 | payload size |
| 28 | 4 | header CRC32 |

头部后依次是 payload 和 4 字节 payload CRC32。SOURCE 只发送实际 DSP 数据，不发送逻辑补零；REPAIR 发送完整 `symbol_size`。

内部受保护符号为：

```text
[uint16 original_length | uint32 original_crc32 | payload | implicit_zero_padding]
```

底层 RF/PHY 仍应先完成自己的 CRC。库内 CRC 用于保护 FEC 帧控制字段，并验证恢复出的原始数据，不是密码学认证。

## Memory and performance

`create` 阶段通过 `new (nothrow)` 分配所有工作区，热路径 `push/ingest/expire` 不进行堆分配。没有可变全局状态；GF 查表只在程序初始化时生成一次。

忽略上下文结构体和 retired key 时，工作区近似为：

```text
encoder ≈ (1 + 8 + 10) × symbol_size + frame overhead
decoder ≈ max_active_blocks × 17 × symbol_size
```

编码器同时预留 XOR 和 RS 工作区，以支持块边界动态切换。解码器每个活动块预留 8 个 XOR 累加器、8 个 repair 缓冲和 1 个临时缓冲。可以通过降低 `max_active_blocks` 控制 RAM，但会更早淘汰乱序块。

XOR 按 32 位自然字长处理，CRC32 使用 16 项半字节查表，RS 乘法使用 GF 对数/指数表。最终 cycles/byte、栈峰值和功耗仍必须在目标 MCU、量产优化选项及真实中断负载下测量。

## Decoder statistics

统计包括：

- 有效帧、源帧、修复帧、重复/迟到帧；
- 原始缺包、成功恢复和不可恢复包；
- 完成、超时和被活动块上限淘汰的块；
- CRC、格式、profile 和恢复符号校验错误；
- 块内最大连续缺包和同一 XOR 列多丢包次数。

`max_missing_burst` 当前按单个 FEC 块统计，不跨块拼接；控制面需要跨块突发时，应结合 RF 序号轨迹补充计算。

## Limitations

- 当前不是线程安全实现；每条流应使用独立上下文并由单一线程/任务调用。
- `fec_encoder_flush` 丢弃未完成编码块的 repair 状态，但已经回调出去的系统型 SOURCE 不会撤回。
- 解码端若把 `block_timeout_ms` 配置为 0，流结束时必须调用 `fec_decoder_flush`，否则最后的未完成块不会产生不可恢复事件和统计。
- 不支持 callback 背压；RF 发送队列必须按 `1 + repair_count` 峰值准备容量。
- create 阶段仍依赖可用堆；完全无堆平台后续应增加 caller-provided workspace/in-place API。
- FEC 负责包擦除恢复，不替代 PHY 纠错、重传、时钟同步或业务层排序。
