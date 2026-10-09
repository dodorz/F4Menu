# F4Menu

Windows 文件启动器与配置工具。纯 C + Win32 API 编写，无第三方依赖。

## 功能

- **配置模式**：双击运行，打开 GUI 管理程序关联列表（添加/编辑/删除）
- **启动模式**：通过命令行传入文件路径，弹出右键菜单选择用哪个程序打开

## 使用方法

### 配置模式

直接双击 `F4Menu.exe`，在界面中管理程序列表：

| 字段 | 说明 |
|------|------|
| 名称 | 程序显示名称 |
| 路径 | 可执行文件路径（支持 `%ENV%` 环境变量） |
| 参数 | 启动参数，`%1` 代表文件路径 |
| 起始目录 | 工作目录 |
| 图标 | 图标文件路径 |
| 类型 | 匹配的文件扩展名，逗号分隔（如 `txt,log,md`） |
| 模式 | 独立（每个文件单独启动）/ 合并（多个文件传给同一实例） |
| 窗口 | 常规 / 最大化 / 最小化 |

### 配置文件格式

`F4Menu.ini`（UTF-8）里**一个段就是一个程序**：

- **段名 = 程序显示名称**，段在文件中的先后顺序 = 界面列表顺序（段名里有空格没问题）
- **带 `Path` 的段才会被读取**；`[General]` 存放默认值；其余段、注释和空行在保存时原样保留，可以随意放自己的备注或别的工具的配置
- 增加一个程序 = 贴一段，删除一个程序 = 删一段，不需要维护任何计数

```ini
[General]
DefaultMode=separate        ; 所有未单独指定 Mode 的程序都用这个
DefaultWindow=normal

[记事本]
Path=%SystemRoot%\System32\notepad.exe
Icon=%SystemRoot%\System32\notepad.exe,0
Mode=merge                  ; merge = 多个文件交给同一个实例
Window=min                  ; min = 最小化
Type=txt,ini,log,md
```

| 键 | 说明 |
|----|------|
| `Path` | 可执行文件路径（支持 `%ENV%` 环境变量） |
| `Param` | 启动参数，`%1` 代表文件路径 |
| `Start` | 起始目录 |
| `Icon` | 图标，格式为 `图标文件,图标序号` |
| `Type` | 匹配的文件扩展名，逗号分隔 |
| `Mode` | `separate`（每个文件单独启动）/ `merge`（多个文件传给同一实例） |
| `Window` | `normal` / `max` / `min` |

- `Mode` 和 `Window` 与 `[General]` 默认值相同的段**不会写这两行**；改了 `DefaultMode` / `DefaultWindow`，所有没有单独指定的程序一起变
- 段名在比较时忽略大小写，因此名字重复的程序会自动加 `#2`、`#3` 后缀
- 段内 `[段名]` 之后的注释以及本程序不认识的键会在保存时保留
- 想让某项注释跟着某个程序走，把注释写在它的 `[段名]` 行之后（段名之前的注释属于文件顶部）
- 由旧版本（`[General] Count=N` + `[ProgramN]` 编号段）写出的配置文件可以直接读取，
  在程序里保存一次之后就变成新格式，旧计数和 `Main` 字段自动丢弃

### 启动模式

```bash
F4Menu.exe <file1> [file2] ...
```

程序根据文件扩展名匹配已配置的程序，弹出菜单供选择。

### 配置关联

将以下命令注册为 `.txt` 等文件类型的打开方式：

```
F4Menu.exe "%1"
```

或在右键菜单中调用：

```
F4Menu.exe "%1" "%2" "%3"
```

## 构建

### 前置条件

- [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) 或完整 Visual Studio

### 构建命令

```powershell
.\build.ps1                    # Release x64（默认）
.\build.ps1 -Platform Win32    # Release Win32
.\build.ps1 -Clean             # 清理构建产物
.\build.ps1 -Verbose           # 显示详细编译输出
```

也可以使用 `.bat` 包装：

```bat
build.bat                  &rem x64
build.bat Win32            &rem Win32
build.bat x64 clean        &rem 清理
```

## 项目结构

```
F4Menu/
├── F4Menu.c          # 源代码
├── F4Menu.rc         # 资源脚本（图标）
├── F4Menu.vcxproj    # MSBuild 项目文件
├── version.h         # 版本号（发布时由 CI 按 tag 覆盖，本地构建用它）
├── build.ps1         # PowerShell 构建脚本
├── build.bat         # 批处理构建包装
└── F4Menu.ini        # 运行时配置（界面里管理，也可直接编辑）
```

## 版本号

版本以 git tag 为准（`0.1.0`、`0.1.0-rc1` 这样的形式触发发布）：

- `version.h` 供**本地构建**使用（“关于”对话框和 exe 文件属性），改版本时同步更新它
- 发布流水线会用 tag 覆盖 `version.h`，所以 CI 产物里的版本一定等于 tag
- 配置文件格式发生不兼容变化时按次版本号递增（`0.0.x` → `0.1.0`）

## 兼容性

- Windows XP ~ Windows 11
- 32 位 / 64 位
- 静态链接运行时（`/MT`），无需安装 VC++ 运行库
