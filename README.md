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
├── build.ps1         # PowerShell 构建脚本
├── build.bat         # 批处理构建包装
└── F4Menu.ini        # 运行时配置（自动生成）
```

## 兼容性

- Windows XP ~ Windows 11
- 32 位 / 64 位
- 静态链接运行时（`/MT`），无需安装 VC++ 运行库
