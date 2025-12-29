```
# 从 upstream 获取 cow 分支代码
git fetch upstream cow
# 创建本地 cow 分支并以 upstream/cow 为基准
git checkout -b cow upstream/cow
# 将本地 cow 分支与 origin/cow 关联，用于后续推送
git push -u origin cow
# 查看本地分支及其关联的远程分支
git branch -vv
```