#!/bin/bash
# DC AssignActivate 激活码批量扫描
# 依次对候选激活码调用 test_dc_scan, 汇总每个码的结论
#
# 用法: ./scan_dc_codes.sh
# 结果写入 dc_scan_results.txt

set -u
CODES=${1:-"0x0000 0x0100 0x0200 0x0300 0x0700 0x0301 0x0101 0x0331"}
CYCLE_NS=${2:-1000000}
DURATION_MS=${3:-3000}
RESULT_FILE=dc_scan_results.txt

cd "$(dirname "$0")/build" 2>/dev/null || true
DIR=$(dirname "$(readlink -f "$0")")
if [ -x "$DIR/build/test_dc_scan" ]; then
  cd "$DIR/build"
elif [ -x "$DIR/test_dc_scan" ]; then
  cd "$DIR"
else
  echo "找不到 test_dc_scan 可执行文件"
  exit 1
fi

: > "$RESULT_FILE"
echo "DC 激活码扫描开始: $(date)" | tee -a "$RESULT_FILE"
echo "候选码: $CODES" | tee -a "$RESULT_FILE"
echo "" | tee -a "$RESULT_FILE"

for code in $CODES; do
  echo "==================== 测试 $code ====================" | tee -a "$RESULT_FILE"
  # timeout 兜底: 即便程序异常也不会永久挂起 (duration + 10s 余量)
  timeout $((DURATION_MS/1000 + 15)) ./test_dc_scan "$code" "$CYCLE_NS" "$DURATION_MS" 2>&1 | tee -a "$RESULT_FILE"
  echo "" | tee -a "$RESULT_FILE"
  sleep 2  # 等待 master 完全释放
done

echo "==================== 汇总 ====================" | tee -a "$RESULT_FILE"
grep "结论:" "$RESULT_FILE" | tee -a "$RESULT_FILE"
echo "完整结果见 $RESULT_FILE"
