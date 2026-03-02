## Single Core FASE Environment

这个文件夹下存放的是单核 FASE 环境(附带ila)

### 目录结构

`gateware`是litex生成的一个单核rocket的工程文件

`NulCPUCtrlMPWithUart.v` 是hardware/chisel文件下使用sbt生成的单核921600波特率的控制器

`add_nulctrl_in_examplerocketsystem.py` 用于将上述控制器实例化到rocket中

`patchv.py` 用于将控制器的一些调试信号加入到设计中

### 使用方法

```bash
Make verilog
Make vivado
```


