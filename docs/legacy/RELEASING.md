# 发布与仓库结构

## 目录约定

```
D:\TaskPin\
├─ repo\        源码仓库（git，可直接推到 GitHub）
├─ build\       构建产物（不进 git）
└─ install\     发行包 zip（不进 git，作为 GitHub Release 附件上传）
```

## 源码 vs 发行文件

- **源码**：`repo/` 里只有代码、文档、脚本；`build/`、`install/` 均在 `.gitignore` 中。
- **发行文件**：`install\TaskPin-x.y.z-portable.zip`（7 MB，需 .NET 9 运行时）与
  `install\TaskPin-x.y.z-standalone.zip`（52 MB，自包含），通过 **GitHub Releases** 分发，
  不打进 git 历史（避免仓库体积膨胀）。

## 发布一次新版本

```powershell
# 1) 改版本号
#    src\TaskPin\TaskPin.csproj 里 <Version>/<FileVersion>/<AssemblyVersion>
# 2) 写 CHANGELOG.md 的对应小节
# 3) 构建 + 打包
cmd /c D:\TaskPin\repo\build.cmd
cmd /c D:\TaskPin\repo\install.cmd
#    便携包
$stage="$env:TEMP\taskpin-stage"; Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item D:\TaskPin\build\* $stage -Recurse -Force
Copy-Item D:\TaskPin\repo\installer $stage -Recurse -Force
Copy-Item D:\TaskPin\repo\install.cmd,D:\TaskPin\repo\README.md,D:\TaskPin\repo\LICENSE $stage -Force
Compress-Archive -Path "$stage\*" -DestinationPath D:\TaskPin\install\TaskPin-0.3.4-portable.zip -Force
#    自包含包
dotnet publish D:\TaskPin\repo\src\TaskPin\TaskPin.csproj -c Release -r win-x64 --self-contained true -p:DebugType=none -o $stage-sc
# 4) 提交 + 打标签
cd D:\TaskPin\repo
git add -A; git commit -m "release: v0.3.4"
git tag v0.3.4
git push origin main --tags
# 5) 在 GitHub 上建 Release，附上两个 zip
```

## 首次推送到 GitHub

仓库目标：`https://github.com/YChoux-Pan/TaskPin`

```powershell
cd D:\TaskPin\repo
git remote add origin https://github.com/YChoux-Pan/TaskPin.git
git branch -M main
git push -u origin main
```

> 需要先在该账号下创建空仓库（不要勾选 README/License，避免冲突），并完成一次认证
> （GitHub CLI `gh auth login`，或使用 Personal Access Token 作为密码）。
