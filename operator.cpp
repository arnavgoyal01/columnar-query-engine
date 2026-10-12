#include "operator.h"
#include "types.h"
#include <algorithm>
#include <cstddef>
#include <execution>
#include <functional>
#include <grp.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

void AggState::add_row(int i) { count += i; }
void AggState::updateSum(double value) { sum += value; }
void AggState::updateMin(double value) { min = std::min(value, min); }
void AggState::updateMax(double value) { max = std::max(value, max); }
void AggState::reset() {
  sum = 0;
  count = 0;
  first = true;
}

bool ScanOp::next(Batch &batch) {
  if (pos >= t[0].length) {
    return false;
  }

  auto n = std::min<size_t>(1024, t[0].length - pos);

  batch.columns.clear();
  for (auto &c : t) {
    std::visit(
        [&](const auto &full) {
          using V = typename std::decay_t<decltype(full)>::value_type;
          const V *slice = &full[pos];
          batch.columns.push_back(slice);
        },
        c.data);
  }
  batch.row_mask = std::vector<uint8_t>(n, 1);
  pos += n;
  return true;
}

void ScanOp::open() { pos = 0; }

void ScanOp::close() {}

bool FilterOp::next(Batch &out) {
  Batch in;
  while (child->next(in)) {
    out.row_mask.clear();
    auto &cd = in.columns[index];
    std::visit(
        [&](const auto c) {
          using T = typename std::decay_t<decltype(*c)>;
          if constexpr (std::is_same_v<T, std::string>) {
            std::cerr << "Error: string column\n";
            return;
          } else {
            out.row_mask = in.row_mask;
            std::visit(
                [&](auto &op) {
                  const auto d = c;
                  const size_t bound = in.row_mask.size();
                  auto opt = out.row_mask.data();
                  auto ipt = in.row_mask.data();
                  for (int i = 0; i < bound; i++) {
                    *(opt + i) = op(*(d + i), limit) * *(ipt + i);
                  }
                },
                pred);
          }
        },
        cd);

    out.columns = in.columns;
    return true;
  }
  return false;
}

void FilterOp::open() { child->open(); }

void FilterOp::close() { child->close(); }

bool AggOp::next(Batch &out) {
  if (already_emitted)
    return false;
  Batch in;
  while (child->next(in)) {
    auto &cd = in.columns[index];
    std::visit(
        [&](const auto &v) {
          using T = typename std::decay_t<decltype(*v)>;
          if constexpr (!std::is_arithmetic_v<T>) {
            std::cerr << "Error: string column not applicable for agg\n";
            return;
          } else {
            const size_t bound = in.row_mask.size();
            const auto ptr = in.row_mask.data();
            std::vector<double> sums;
            sums.reserve(bound);
            const auto sptr = sums.data();
            switch (func) {
            case Func::SUM:
              for (int i = 0; i < bound; i++) {
                *(sptr + i) = *(v + i) * *(ptr + i);
              }
              ag.updateSum(
                  std::reduce(std::execution::unseq, sptr, sptr + bound, 0.0));
              break;
            case Func::COUNT:
              for (int i = 0; i < bound; i++) {
                ag.add_row(in.row_mask[i]);
              }
              break;
            case Func::AVG:
              for (int i = 0; i < bound; i++) {
                ag.add_row(in.row_mask[i]);
                *(sptr + i) = *(v + i) * *(ptr + i);
              }
              ag.updateSum(
                  std::reduce(std::execution::unseq, sptr, sptr + bound, 0.0));
              break;
            case Func::MAX:
              for (int i = 0; i < bound; i++) {
                auto value =
                    *(ptr + i) ? *(v + i) : -pos_inf_double::infinity();
                ag.updateMax(value);
              }
              break;
            case Func::MIN:
              for (int i = 0; i < bound; i++) {
                auto value = *(ptr + i) ? *(v + i) : pos_inf_double::infinity();
                ag.updateMin(value);
              }
              break;
            }
            auto b =
                std::reduce(std::execution::unseq, ptr, ptr + bound, 0) != 0;
            ag.first = b ? false : ag.first;
          }
        },
        cd);
  }

  already_emitted = true;
  return true;
}

void AggOp::open() {
  ag.reset();
  child->open();
  already_emitted = false;
}

void AggOp::close() { child->close(); }

bool ProjectionOp::next(Batch &out) {
  Batch in;
  out.columns.clear();
  out.columns.reserve(col_indices.size());
  while (child->next(in)) {
    for (auto &i : col_indices) {
      out.columns.push_back(in.columns[i]);
    }
    if (!out.columns.size())
      continue;

    out.row_mask = in.row_mask;
    return true;
  }

  return false;
}

void ProjectionOp::open() { child->open(); }

void ProjectionOp::close() { child->close(); }

bool LimitOp::next(Batch &out) {
  Batch in;
  out.columns.clear();
  while (child->next(in)) {
    if (limit <= 0) {
      return false;
    }

    int d = std::accumulate(in.row_mask.begin(), in.row_mask.end(), 0);
    if (d <= limit) {
      limit -= d;
      out = in;
    } else {
      double sum{0};
      int i{0};
      while (sum < limit) {
        sum += in.row_mask[i];
        i++;
      }
      out.columns = in.columns;
      std::vector<uint8_t> vec(in.row_mask.begin(), in.row_mask.begin() + i);
      out.row_mask = vec;
      limit -= sum;
    }
    return true;
  }
  return false;
}

void LimitOp::open() { child->open(); }

void LimitOp::close() { child->close(); }

void GroupByOp::open() {
  std::visit([](auto &map) { map.clear(); }, running_state);
  already_emitted = false;
  child->open();
}

void GroupByOp::close() { child->close(); }
// template <typename T> struct DumpType;
bool GroupByOp::next(Batch &out) {
  if (already_emitted)
    return false;
  Batch in;
  out.columns.clear();
  while (child->next(in)) {
    auto &gc = in.columns[group_id];
    auto &ac = in.columns[agg_id];

    if (out.columns.empty()) {
      out = in;
    }

    std::visit(
        [&](const auto &keys, const auto &vals) {
          using K = typename std::decay_t<decltype(*keys)>;
          using V = typename std::decay_t<decltype(*vals)>;

          if (!std::holds_alternative<std::unordered_map<K, AggState>>(
                  running_state)) {
            running_state.emplace<std::unordered_map<K, AggState>>();
          }

          std::visit(
              [&](auto &map) {
                using KeyType = typename std::decay_t<decltype(map)>::key_type;
                if constexpr (std::is_same_v<K, KeyType>) {
                  if constexpr (!std::is_arithmetic_v<V>) {
                    if (func != Func::COUNT) {
                      std::cerr << "Error: Invalid func for string agg col\n";
                      return;
                    } else {
                      for (auto i = 0; i < in.row_mask.size(); i++) {
                        if (!in.row_mask[i])
                          continue;
                        map[*(keys + i)].add_row(in.row_mask[i]);
                      }
                    }
                  } else {
                    for (auto i = 0; i < in.row_mask.size(); i++) {
                      if (!in.row_mask[i])
                        continue;
                      auto ky = *(keys + i);
                      auto &ag = map[ky];
                      switch (func) {
                      case Func::SUM:
                        ag.updateSum(vals[i] * in.row_mask[i]);
                        break;
                      case Func::COUNT:
                        ag.add_row(in.row_mask[i]);
                        break;
                      case Func::AVG:
                        ag.add_row(in.row_mask[i]);
                        ag.updateSum(vals[i] * in.row_mask[i]);
                        break;
                      case Func::MAX:
                        ag.updateMax(vals[i]);
                        break;
                      case Func::MIN:
                        ag.updateMin(vals[i]);
                        break;
                      }
                      // ag.first = std::accumulate(in.row_mask.begin(),
                      //                            in.row_mask.end(), 0) == 0;
                    }
                  }
                } else {
                  std::cerr << "Error: KeyType and ColumnType do not match\n";
                  return;
                }
                // DumpType<K> dump;
              },
              running_state);
        },
        gc, ac);
  }

  already_emitted = true;
  return true;
}
