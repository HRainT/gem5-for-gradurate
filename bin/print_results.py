#!/usr/bin/env python3

from collections import defaultdict
from collections import namedtuple
from itertools import product
import operator
import os

import common
from common import PATHS, BENCHMARKS_INFO, ML_INPUT_PARTIONS

SUITE = [ # list of (benchmark, input name, weight, validation_br_name) tuples
  ('473.astar', 'rivers', 1.0, 'top100'),
  ('473.astar', 'BigLakes', 1.0, 'top100'),
]

CONFIGS = [ # list of (experiment name, model budget) tuples
  ('testrun5', 1024),
]

TAGE_CONFIG_NAME = 'tagescl64'

CSV = True
DUMP_PER_BR_STATS = True
PRODUCE_HARD_BRS = False
HARD_BRS_TAG = None
BUDGET = 2048
OUTPUT_CSV_PATH = "results/testrun5.csv"  # CSV输出路径

State = namedtuple('State', ['selected_brs_set', 'selected_brs_breakdown',
                             'total_size', 'total_mpki_reduction'])

# 新增函数：将CSV结果写入文件
def write_csv_to_file(csv_data, path):
    """将CSV数据写入指定路径的文件"""
    try:
        with open(path, 'w') as f:
            f.write(csv_data)
        print(f"CSV output saved to {os.path.abspath(path)}")
    except Exception as e:
        print(f"Error writing CSV file: {e}")


def compute_validation_mpki_reductions(benchmark, experiment_name):
    mpki_reductions = defaultdict(float)
    for inp in ML_INPUT_PARTIONS[benchmark]['validation_set']:
        tage_stats = common.read_tage_stats(TAGE_CONFIG_NAME, benchmark, inp)
        cnn_stats = common.read_cnn_stats(
            benchmark, experiment_name, inp, tage_stats)

        for br in cnn_stats:
          mpki_reductions[br] += (tage_stats[br].weighted_stats.mpki
                                    cnn_stats[br].weighted_stats.mpki)
    return mpki_reductions


def compute_test_stats(benchmark, experiment_name, hard_brs_name, inp, good_brs,
                       hard_brs_tag=None):
    tage_stats = common.read_tage_stats(TAGE_CONFIG_NAME, benchmark, inp)
    cnn_stats = common.read_cnn_stats(
        benchmark, experiment_name, inp, tage_stats)

    total_mpki_reduction = sum(
        tage_stats[br].weighted_stats.mpki - cnn_stats[br].weighted_stats.mpki
        for br in common.read_hard_brs(benchmark, hard_brs_name)
        if br in tage_stats and br in cnn_stats and br in good_brs)

    cnn_stats[-1].weighted_stats = (
        tage_stats[-1].weighted_stats.ApplyMPKIReduction(total_mpki_reduction))
    return tage_stats, cnn_stats, total_mpki_reduction



def get_highest_mpki_brs(mpki_reductions, max_brs):
    total_pos_mpki_reduction = sum(
        mpki for br, mpki in mpki_reductions.items() if mpki > 0 and br != -1)

    good_brs = []
    if total_pos_mpki_reduction > 0:
        num_good_brs = 0
        for br in sorted(mpki_reductions, key=mpki_reductions.get, reverse=True):
            if br == -1: continue

            if (mpki_reductions[br] / total_pos_mpki_reduction < 0.001
                or (max_brs is not None and num_good_brs == max_brs)
                or mpki_reductions[br] <= 0):
                break

            good_brs.append(br)
            num_good_brs += 1

    return good_brs


def get_mpki_reductions(benchmark, experiment, inp, hard_brs_name):
  good_brs = get_highest_mpki_brs(
      compute_validation_mpki_reductions(benchmark, experiment),
      None)
  tage_stats, cnn_stats, _ = compute_test_stats(
      benchmark, experiment, hard_brs_name, inp, good_brs,
      HARD_BRS_TAG if not PRODUCE_HARD_BRS else None)

  return {br: (tage_stats[br].weighted_stats.mpki
               - cnn_stats[br].weighted_stats.mpki)
          for br in good_brs if br in tage_stats and br in cnn_stats}


def select_next_br(mpki_reductions, selected_brs):
  try:
    br, br_mpki_reduction = max(
        ((br, mpki_reduction) for br, mpki_reduction in mpki_reductions.items()
         if br not in selected_brs),
        key=operator.itemgetter(1)
    )
  except ValueError:
    br, br_mpki_reduction = None, 0.0
  return br, br_mpki_reduction


def create_new_state(old_state, new_br, new_br_mpki_reduction, new_br_experiment,
                     new_br_config_size):
  if new_br is None:
    return State(
      selected_brs_set=old_state.selected_brs_set,
      selected_brs_breakdown=old_state.selected_brs_breakdown,
      total_mpki_reduction=old_state.total_mpki_reduction,
      total_size=old_state.total_size + new_br_config_size
    )

  new_br_list_for_experiment = (
      old_state.selected_brs_breakdown[new_br_experiment].copy())
  new_br_list_for_experiment.append((new_br, new_br_mpki_reduction))
  new_brs_breakdown = old_state.selected_brs_breakdown.copy()
  new_brs_breakdown[new_br_experiment] = new_br_list_for_experiment
  return State(
      selected_brs_set=old_state.selected_brs_set | set([new_br]),
      selected_brs_breakdown=new_brs_breakdown,
      total_mpki_reduction=(old_state.total_mpki_reduction
                            + new_br_mpki_reduction),
      total_size=old_state.total_size + new_br_config_size,
  )


def get_max_models_per_experiment():
  return {experiment: int(BUDGET // size) for experiment, size in CONFIGS}


def read_benchmark_stats(benchmark, inp, hard_brs_name):
  return {experiment: get_mpki_reductions(benchmark, experiment, inp, hard_brs_name)
          for experiment, _ in CONFIGS}


def init_dynamic_state():
  dynamic_states = {}
  dynamic_states[tuple([0] * len(CONFIGS))] = State(
      selected_brs_set=set(),
      selected_brs_breakdown={experiment: [] for experiment, _ in CONFIGS},
      total_mpki_reduction=0.0,
      total_size=0.0,
  )
  return dynamic_states


def get_last_state_idx(curr_state_idx, experiment_idx):
  idx_as_list = list(curr_state_idx)
  idx_as_list[experiment_idx] -= 1
  if (idx_as_list[experiment_idx]) < 0:
    return None
  else:
    return tuple(idx_as_list)


def get_state_size(state_idx):
  return sum(x * y for x, y in zip(
    state_idx,
    (config_size for _, config_size in CONFIGS),
  ))


def all_state_indices(max_models):
  iteration_ranges = [
    range(0, max_models[experiment] + 1) for experiment, _ in CONFIGS
  ]
  return product(*iteration_ranges)


def fill_states(dynamic_states, mpki_reductions, max_models):
  for state_idx in all_state_indices(max_models):
    state_size = get_state_size(state_idx)
    if state_size == 0 or state_size > BUDGET: continue

    choices = []
    for experiment_idx, (experiment, model_size) in enumerate(CONFIGS):
      prev_state_idx = get_last_state_idx(state_idx, experiment_idx)
      if prev_state_idx is None:
        continue
      prev_state = dynamic_states[prev_state_idx]
      next_br, next_br_mpki_reduction = select_next_br(
          mpki_reductions[experiment], prev_state.selected_brs_set)
      next_total_mpki_rediction = (prev_state.total_mpki_reduction
                                   + next_br_mpki_reduction)
      choices.append((next_total_mpki_rediction, prev_state, experiment,
                      model_size, next_br, next_br_mpki_reduction,))

    best_choice = max(choices, key=operator.itemgetter(0))
    (_, chosen_prev_state, chosen_experiment, choconfigodel_size, chosen_next_br,
     chosen_next_br_mpki_reduction, ) = best_choice

    dynamic_states[state_idx] = create_new_state(
        chosen_prev_state, chosen_next_br, chosen_next_br_mpki_reduction,
        chosen_experiment, choconfigodel_size)
    assert dynamic_states[state_idx].total_size == state_size


def get_best_assignments(benchmarks_dynamic_states, max_models):
  choices = defaultdict(list)
  states = benchmarks_dynamic_states
  for state_idx in all_state_indices(max_models):
    state_size = get_state_size(state_idx)
    if state_size == 0 or state_size > BUDGET: continue
    weighted_mpki_reduction = (sum(
        weight * states[benchmark_idx][state_idx].total_mpki_reduction
        for benchmark_idx, (_, _, weight, _) in enumerate(SUITE))
        / sum(weight for _, _, weight, _ in SUITE))
    choices[state_size].append((weighted_mpki_reduction, state_idx))

  best_assignments = {}
  for size, choices_for_size in choices.items():
    best_assignments[size] = max(choices_for_size, key=operator.itemgetter(0))[1]
  return best_assignments


def print_results_csv(benchmarks_dynamic_states, best_assignments, size):
  state_idx = best_assignments[size]

  name = []
  mpki = []
  for benchmark_idx, (benchmark, inp, _, _) in enumerate(SUITE):
    state = benchmarks_dynamic_states[benchmark_idx][state_idx]
    tage_stats = common.read_tage_stats(
        TAGE_CONFIG_NAME, benchmark, inp,
        HARD_BRS_TAG if not PRODUCE_HARD_BRS else None)
    cnn_mpki = tage_stats[-1].weighted_stats.mpki - state.total_mpki_reduction
    name.append(benchmark + '_' + inp)
    mpki.append(cnn_mpki)
  
  header = ','.join(name)
  data = ','.join(map(str, mpki))
  
  print(header)
  print(data)
  
  # 返回CSV数据以便写入文件
  return f"{header}\n{data}\n"


def print_results_verbose(benchmarks_dynamic_states, best_assignments, size):
  state_idx = best_assignments[size]
  sum_weights = sum(weight for _, _, weight, _ in SUITE)
  total_mpki_reduction = sum(
      weight * benchmarks_dynamic_states[benchmark_idx][state_idx].total_mpki_reduction
      for benchmark_idx, (_, _, weight, _) in enumerate(SUITE)) / sum_weights
  tag = HARD_BRS_TAG if not PRODUCE_HARD_BRS else None
  total_tage_mpki = sum(
      weight * common.read_tage_stats(
        TAGE_CONFIG_NAME, benchmark, inp, tag)[-1].weighted_stats.mpki
      for benchmark, inp, weight, hard_brs_name in SUITE) / sum_weights
  
  output_lines = [
      f"========= Size: {size}KB ============",
      f"Total TAGE MPKI: {total_tage_mpki}",
      f"Total CNN MPKI: {total_tage_mpki - total_mpki_reduction}",
      f"Total MPKI Reduction: {total_mpki_reduction}"
  ]
  
  for benchmark_idx, (benchmark, inp, _, _) in enumerate(SUITE):
    state = benchmarks_dynamic_states[benchmark_idx][state_idx]
    tage_stats = common.read_tage_stats(
        TAGE_CONFIG_NAME, benchmark, inp,
        HARD_BRS_TAG if not PRODUCE_HARD_BRS else None)
    tage_mpki = tage_stats[-1].weighted_stats.mpki
    output_lines.extend([
        '--------------',
        f"Benchmark: {benchmark}_{inp}",
        f"TAGE MPKI: {tage_mpki}",
        f"CNN MPKI: {tage_mpki - state.total_mpki_reduction}",
        f"MPKI Reduction: {state.total_mpki_reduction}"
    ])
    
    for experiment, _ in CONFIGS:
      selected_brs = state.selected_brs_breakdown[experiment]
      output_lines.append(f"Config {experiment} ---> {len(selected_brs)} models: "
                         f"{', '.join([hex(br) for br, _ in selected_brs])}")
  
  output = "\n".join(output_lines)
  print(output)
  return output


def dump_per_br_stats(benchmarks_dynamic_states, best_assignments, size):
  assert len(CONFIGS) == 1, ('Per sranch stats only works for '
                             'single-model results')

  experiment_name = CONFIGS[0][0]
  state_idx = best_assignments[size]
  for benchmark_idx, (benchmark, inp, _, _) in enumerate(SUITE):
    state = benchmarks_dynamic_states[benchmark_idx][state_idx]
    tage_stats = common.read_tage_stats(
        TAGE_CONFIG_NAME, benchmark, inp,
        HARD_BRS_TAG if not PRODUCE_HARD_BRS else None)
    cnn_stats = common.read_cnn_stats(benchmark, experiment_name, inp,
                                      tage_stats) 

    stat_file_defs = [
      ('PC', lambda br, tage, cnn: hex(br)),
      ('Total', lambda br, tage, cnn: tage.total),
      ('Tage Correct', lambda br, tage, cnn: tage.correct),
      ('Tage Inorrect', lambda br, tage, cnn: tage.incorrect),
      ('Tage Accuracy', lambda br, tage, cnn: tage.accuracy),
      ('Tage MPKI', lambda br, tage, cnn: tage.mpki),
      ('CNN Correct', lambda br, tage, cnn: cnn.correct),
      ('CNN Inorrect', lambda br, tage, cnn: cnn.incorrect),
      ('CNN Accuracy', lambda br, tage, cnn: cnn.accuracy),
      ('CNN MPKI', lambda br, tage, cnn: cnn.mpki),
      ('MPKI Reduction', lambda br, tage, cnn: tage.mpki - cnn.mpki),
      ('Selected Branch', (lambda br, tage, cnn:
                           'Yes' if br in state.selected_brs_set else 'No')),
    ]
      
    header = ','.join(header for header, _ in stat_file_defs)
    print(header)
    rows = []
    for br in cnn_stats:
        row = ','.join(
            str(f(br, tage_stats[br].weighted_stats,
                  cnn_stats[br].weighted_stats))
            for _, f in stat_file_defs)
        print(row)
        rows.append(row)
    
    return f"{header}\n" + "\n".join(rows) + "\n"


def print_results(benchmarks_dynamic_states, best_assignments, size):
  output = ""
  if CSV:
      csv_output = print_results_csv(benchmarks_dynamic_states, best_assignments, size)
      output += csv_output
  else:
      verbose_output = print_results_verbose(benchmarks_dynamic_states, best_assignments, size)
      output += verbose_output
      
  if DUMP_PER_BR_STATS:
      per_br_output = dump_per_br_stats(benchmarks_dynamic_states, best_assignments, size)
      output += per_br_output if per_br_output else ""
  
  return output


def produce_hard_br_files(benchmarks_dynamic_states, best_assignments, size, tag):
  assert tag
  state_idx = best_assignments[size]

  for benchmark_idx, (benchmark, inp, _) in enumerate(SUITE):
    state = benchmarks_dynamic_states[benchmark_idx][state_idx]
    output_dir = (script_utils.TAGE_TRACES_PATH + '/'
                  + script_utils.translate_bench_name[benchmark]
                  + '/noalloc_for_hard_brs_' + tag)
    os.makedirs(output_dir, exist_ok=True)

    filename = 'hard_brs_' + inp
    with open(output_dir + '/' + filename, 'w') as f:
      for br in state.selected_brs_set:
        f.write('{:x}\n'.format(br))

    filename = 'hard_brs_breakdown_' + inp
    with open(output_dir + '/' + filename, 'w') as f:
      for experiment, br_list in state.selected_brs_breakdown.items():
        f.write('{}: {}\n'.format(experiment, ', '.join(map(hex, (br for br, _ in br_list)))))


# 修复函数名错误 - 添加了缺失的"_assignments"部分
def evaluate_all_assignments(max_models):
  benchmarks_dynamic_states = {}
  for benchmark_idx, (benchmark, inp, _, hard_brs_name) in enumerate(SUITE):
    print('Evaluating all possible assignments for', benchmark, inp)
    mpki_reductions = read_benchmark_stats(benchmark, inp, hard_brs_name)
    dynamic_states = init_dynamic_state()
    fill_states(dynamic_states, mpki_reductions, max_models)
    benchmarks_dynamic_states[benchmark_idx] = dynamic_states
  return benchmarks_dynamic_states


def main():
  max_models = get_max_models_per_experiment()
  benchmarks_dynamic_states = evaluate_all_assignments(max_models)
  print('Identifying best assignments')
  best_assignments = get_best_assignments(benchmarks_dynamic_states, max_models)
  
  # 获取输出结果并写入CSV文件
  output = print_results(benchmarks_dynamic_states, best_assignments, BUDGET)
  write_csv_to_file(output, OUTPUT_CSV_PATH)
  
  if PRODUCE_HARD_BRS:
    produce_hard_br_files(benchmarks_dynamic_states, best_assignments,
                          BUDGET, HARD_BRS_TAG)


if __name__ == '__main__':
  main()
