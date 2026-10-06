#include "operator.h"
#include <algorithm>
#include <cstddef>
#include <grp.h>
#include <iomanip>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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
          std::vector<V> slice(full.begin() + pos, full.begin() + pos + n);
          batch.columns.push_back(std::move(slice));
        },
        c.data);
  }
  batch.size = n;
  pos += n;
  return true;
}

void ScanOp::open() { pos = 0; }

void ScanOp::close() {}

bool FilterOp::next(Batch &out) {
  Batch in;
  out.columns.clear();
  out.size = 0;
  while (child->next(in)) {
    auto &cd = in.columns[index];
    std::vector<size_t> selected;
    std::visit(
        [&](const auto &c) {
          using T = typename std::decay_t<decltype(c)>::value_type;
          if constexpr (std::is_same_v<T, std::string>) {
            std::cerr << "Error: string column\n";
            return;
          } else {
            selected.reserve(c.size());
            for (size_t i = 0; i < in.size; i++) {
              if (pred(c[i])) {
                selected.push_back(i);
              }
            }
          }
        },
        cd);

    if (!selected.size())
      continue;

    for (auto &cd : in.columns) {
      std::visit(
          [&](const auto &c) {
            using C = typename std::decay_t<decltype(c)>::value_type;
            std::vector<C> slice;
            slice.reserve(selected.size());
            for (auto i : selected) {
              slice.push_back(c[i]);
            }
            out.columns.push_back(std::move(slice));
          },
          cd);
    }

    out.size = selected.size();
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
  std::vector<double> ans;
  double count{};
  out.columns.clear();
  out.size = 0;
  while (child->next(in)) {
    auto &cd = in.columns[index];
    std::visit(
        [&](const auto &v) {
          using T = typename std::decay_t<decltype(v)>::value_type;
          if constexpr (std::is_same_v<T, std::string>) {
            std::cerr << "Error: string column not applicable for agg\n";
            return;
          } else {
            if (v.empty()) {
              std::cerr << "Error: column is empty\n";
              return;
            }
            double a{};
            count += v.size();
            if (func == "sum" || func == "avg" || func == "count") {
              a = std::accumulate(v.begin(), v.end(), 0.0);
            } else if (func == "max") {
              a = *std::max_element(v.begin(), v.end());
            } else if (func == "min") {
              a = *std::min_element(v.begin(), v.end());
            } else {
              std::cerr << "Error: invalid func\n";
              return;
            }
            ans.push_back(a);
          }
        },
        cd);
  }

  if (ans.size() == 0) {
    if (func == "count") {
      std::vector<double> vec{0};
      out.columns.push_back(std::move(vec));
      out.size = 1;
    }
    already_emitted = true;
    return true;
  }

  double result{};
  if (func == "sum") {
    result = std::accumulate(ans.begin(), ans.end(), 0.0);
  } else if (func == "count") {
    result = count;
  } else if (func == "avg") {
    result = std::accumulate(ans.begin(), ans.end(), 0.0);
    result /= count;
  } else if (func == "max") {
    result = *std::max_element(ans.begin(), ans.end());
  } else if (func == "min") {
    result = *std::min_element(ans.begin(), ans.end());
  }
  // Force fixed-point notation AND adjust decimal precision
  std::vector<double> vec{result};
  out.columns.push_back(std::move(vec));
  out.size = 1;
  already_emitted = true;
  return true;
}

void AggOp::open() {
  child->open();
  already_emitted = false;
}

void AggOp::close() { child->close(); }

bool ProjectionOp::next(Batch &out) {
  out.columns.clear();
  out.size = 0;
  std::vector<size_t> sizes;
  Batch in;
  while (child->next(in)) {
    for (auto &i : col_indices) {
      std::visit(
          [&](auto &c) {
            out.columns.push_back(c);
            sizes.push_back(c.size());
          },
          in.columns[i]);
    }
    if (!out.columns.size())
      continue;

    out.size = *std::max_element(sizes.begin(), sizes.end());
    return true;
  }

  return false;
}

void ProjectionOp::open() { child->open(); }

void ProjectionOp::close() { child->close(); }

bool LimitOp::next(Batch &out) {
  Batch in;
  out.columns.clear();
  out.size = 0;
  while (child->next(in)) {
    if (limit <= 0) {
      return false;
    }
    auto size = std::min<double>(limit, in.size);
    for (auto &cd : in.columns) {
      std::visit(
          [&](auto &c) {
            using T = typename std::decay_t<decltype(c)>::value_type;
            std::vector<T> slice(c.begin(), c.begin() + size);
            out.columns.push_back(slice);
          },
          cd);
    }
    out.size = size;
    limit -= size;
    return true;
  }
  return false;
}

void LimitOp::open() { child->open(); }

void LimitOp::close() { child->close(); }

void GroupByOp::open() {
  already_emitted = false;
  child->open();
}

void GroupByOp::close() { child->close(); }

void GroupByOp::AggState::add_row() { count += 1; }

void GroupByOp::AggState::update(double value) {
  sum += value;
  min = first ? value : std::min(value, min);
  max = first ? value : std::max(value, max);
  first = false;
}

bool GroupByOp::next(Batch &out) {
  if (already_emitted)
    return false;
  Batch in;
  out.columns.clear();
  out.size = 0;

  while (child->next(in)) {
    auto gc = in.columns[group_id];
    auto ac = in.columns[agg_id];
    std::visit(
        [&](const auto &keys, const auto &vals) {
          using K = typename std::decay_t<decltype(keys)>::value_type;
          using V = typename std::decay_t<decltype(vals)>::value_type;
          if (out.columns.size() == 0) {
            out.columns.push_back(std::vector<K>());
          }

          if constexpr (!std::is_arithmetic_v<V>) {
            if (func != "count") {
              std::cerr << "Error: Invalid func for string agg col\n";
              return;
            } else {
              for (auto i = 0; i < keys.size(); i++) {
                running_state[keys[i]].add_row();
              }
            }
          } else {
            for (auto i = 0; i < keys.size(); i++) {
              running_state[keys[i]].add_row();
              running_state[keys[i]].update(vals[i]);
            }
          }
        },
        gc, ac);
  }

  if (running_state.size() == 0) {
    out.columns.clear();
    out.size = 0;
    already_emitted = true;
    return true;
  }

  std::vector<double> answers;
  std::visit(
      [&](auto &groups) {
        using K = typename std::decay_t<decltype(groups)>::value_type;
        out.size = running_state.size();
        for (auto &[g, as] : running_state) {
          groups.push_back(std::get<K>(g));
          if (func == "count") {
            answers.push_back(as.count);
          } else if (func == "sum") {
            answers.push_back(as.sum);
          } else if (func == "avg") {
            answers.push_back(as.sum / as.count);
          } else if (func == "min") {
            answers.push_back(as.min);
          } else if (func == "max") {
            answers.push_back(as.max);
          } else {
            std::cerr << "Error: Invalid func\n";
          }
        }
      },
      out.columns[0]);

  out.columns.push_back(answers);
  already_emitted = true;
  return true;
}
