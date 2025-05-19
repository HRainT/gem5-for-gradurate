# 假设日志文件名为 'log.txt'
# 输出文件名为 'stst'

import re

def process_log(logfile, outputfile):
    with open(logfile, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    count = 0  # 满足条件的计数器
    output_lines = []  # 存储满足条件的关键信息

    events = []  # 存储已解析的事件
    separator = '------------------------------------'
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        # 跳过空行
        if not line:
            i += 1
            continue
        # 检查是否是分隔线
        if separator in line:
            events.append({'type': 'separator'})
            i += 1
            continue
        # 忽略前缀信息
        if ':' in line:
            message = line.split(':', maxsplit=2)[-1].strip()
        else:
            message = line

        # 检查是否是事件类型行
        if message == 'commit access' or message == 'lookup access':
            event_type = message.split()[0]  # 'commit' 或 'lookup'
            # 检查下一行是否是 STAT:Table 行
            if i + 1 < len(lines):
                next_line = lines[i + 1].strip()
                if ':' in next_line:
                    next_message = next_line.split(':', maxsplit=2)[-1].strip()
                else:
                    next_message = next_line
                if next_message.startswith('STAT:Table'):
                    # 提取表号和索引
                    match = re.search(r'STAT:Table\s+(\d+)\s+\[(\d+)\]', next_message)
                    if match:
                        table = match.group(1)
                        index = match.group(2)
                        # 将事件添加到列表
                        events.append({
                            'type': event_type,
                            'table': table,
                            'index': index,
                            'lines': [line.strip(), lines[i + 1].strip()],
                        })
                        i += 2
                        continue
            # 如果没有紧跟的 STAT:Table 行，或者解析失败，跳过该事件
            i += 1
            continue
        else:
            # 其他非事件行，跳过
            i += 1
            continue

    # 遍历事件列表，匹配满足条件的事件对
    i = 0
    while i < len(events):
        event = events[i]
        if event['type'] == 'separator':
            i += 1
            continue
        if event['type'] == 'commit':
            # 寻找后续的匹配的 lookup 事件
            j = i + 1
            while j < len(events):
                next_event = events[j]
                if next_event['type'] == 'separator':
                    # 遇到分隔线，停止寻找
                    break
                if next_event['type'] == 'lookup' and next_event['table'] == event['table'] and next_event['index'] == event['index']:
                    # 找到匹配的 lookup 事件
                    count += 1
                    # 将匹配的事件行添加到输出列表
                    output_lines.extend(event['lines'])
                    output_lines.extend(next_event['lines'])
                    break  # 停止寻找当前 commit 事件的匹配
                # 即使遇到新的 commit 事件，仍然继续寻找
                j += 1
        i += 1

    # 将满足条件的关键信息写入文件
    with open(outputfile, 'w', encoding='utf-8') as f_out:
        for line in output_lines:
            f_out.write(line + '\n')

    print(f"满足条件的数据共有 {count} 个。")

# 使用函数处理日志文件
process_log('/Data3/yutong.han/riscv/rxu-gem5/out/randomx/20241116_STAT/coremark_loop50_zicond_zba_zbb_testcase-riscv64-xs/debug.log', '/Data3/yutong.han/riscv/rxu-gem5/out/randomx/20241116_STAT/coremark_loop50_zicond_zba_zbb_testcase-riscv64-xs/stst.log')