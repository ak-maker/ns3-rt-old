#!/usr/bin/env bash

# 这个脚本会在当前目录里寻找 scene.xml，
# 然后把所有 'mesh-car_' 改成 'car_'，
# 主要针对 id="mesh-car_X" 与 name="mesh-car_X" 的情况。

SCENE_FILE="scene.xml"

# 如果有别的文件名，你可以自行改
if [ ! -f "$SCENE_FILE" ]; then
  echo "Error: $SCENE_FILE not found!"
  exit 1
fi

# 备份原文件
cp "$SCENE_FILE" "${SCENE_FILE}.bak"

# 使用 sed 在原文件内就地替换:
#   1) 把 id="mesh-car_  改成 id="car_
#   2) 把 name="mesh-car_ 改成 name="car_
# 这样 scene.xml 里 <shape id="mesh-car_4" ...> 会变成 <shape id="car_4" ...>
# 并且 name="mesh-car_4" 也会变成 name="car_4"

sed -i 's/id="mesh-car_/id="car_/g; s/name="mesh-car_/name="car_/g' "$SCENE_FILE"

echo "Done! All 'mesh-car_X' replaced with 'car_X' in $SCENE_FILE."
echo "Backup of original file -> ${SCENE_FILE}.bak"



