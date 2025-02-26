# 假设日志文件名为 'log.txt'

def count_commit_stat_pairs(logfile):
    with open(logfile, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    count = 0  # 计数器
    i = 0
    while i < len(lines) - 1:
        current_line = lines[i].strip()
        next_line = lines[i + 1].strip()

        # 检查当前行是否包含 'commit access'
        if 'commit access' in current_line:
            # 检查下一行是否以 'STAT:Table' 开头
            if next_line.split(':', maxsplit=2)[-1].strip().startswith('STAT:Table'):
                count += 1
                # 打印匹配的行信息
                print(f"匹配 #{count}:")
                print(f"  当前行: {current_line}")
                print(f"  下一行: {next_line}\n")
        i += 1

    print(f"'commit access' 行紧跟着 'STAT:Table' 行的次数共有 {count} 次。")

# 使用函数处理日志文件
count_commit_stat_pairs('/Data3/yutong.han/riscv/rxu-gem5/out/randomx/20241116_STAT/coremark_loop50_zicond_zba_zbb_testcase-riscv64-xs/debug.log')
