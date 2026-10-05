#pragma once
#include "types.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <variant>

struct Operator {
  virtual void open() = 0;
  virtual bool next(Batch &batch) = 0;
  virtual void close() = 0;
  virtual ~Operator() = default;
};

struct ScanOp : Operator {
  const Table &t;
  size_t pos;

  ScanOp(Table &table) : t(table), pos(0) {}
  bool next(Batch &batch) override;

  void open() override;
  void close() override;
};

struct FilterOp : Operator {
  std::unique_ptr<Operator> child;
  std::function<bool(double)> pred;
  size_t index;

  FilterOp(std::unique_ptr<Operator> child, size_t index, std::string op_string,
           double limit)
      : child(std::move(child)), index(index) {
    std::function<bool(double, double)> op;
    if (op_string == "==") {
      op = std::equal_to<>{};
    } else if (op_string == "!=") {
      op = std::not_equal_to<>{};
    } else if (op_string == "<") {
      op = std::less<>{};
    } else if (op_string == "<=") {
      op = std::less_equal<>{};
    } else if (op_string == ">") {
      op = std::greater<>{};
    } else if (op_string == ">=") {
      op = std::greater_equal<>{};
    } else {
      std::cerr << "Error: Invalid operation\n";
      return;
    }
    pred = [=](double d) { return op(d, limit); };
  }
  bool next(Batch &out) override;
  void open() override;
  void close() override;
};

struct AggOp : Operator {
  std::unique_ptr<Operator> child;
  std::string func;
  size_t index;
  bool already_emitted;

  AggOp(std::unique_ptr<Operator> child, std::string func, size_t index)
      : child(std::move(child)), func(func), index(index),
        already_emitted(false) {}

  bool next(Batch &out) override;

  void open() override;
  void close() override;
};

struct ProjectionOp : Operator {
  std::unique_ptr<Operator> child;
  std::vector<size_t> col_indices;

  template <typename InputIt>
  ProjectionOp(std::unique_ptr<Operator> child, InputIt first, InputIt last)
      : child(std::move(child)), col_indices(first, last) {}

  template <typename InputIt>
  ProjectionOp(std::unique_ptr<Operator> child) : child(std::move(child)) {}

  bool next(Batch &batch) override;
  void open() override;
  void close() override;
};

struct LimitOp : Operator {
  std::unique_ptr<Operator> child;
  int limit;

  LimitOp(std::unique_ptr<Operator> child, double limit)
      : child(std::move(child)), limit(limit) {}

  bool next(Batch &batch) override;
  void open() override;
  void close() override;
};

struct GroupByOp : Operator {
  std::unique_ptr<Operator> child;
  size_t group_id;
  size_t agg_id;
  std::string func;
  bool already_emitted{false};
  using ScalarVariant = std::variant<double, int64_t, std::string>;

  struct AggState {
    double sum, count, max, min;
    bool first{true};
    void add_row();
    void update(double value);
  };

  std::unordered_map<ScalarVariant, AggState> running_state;

  GroupByOp(std::unique_ptr<Operator> child, size_t gid, size_t aid,
            std::string func)
      : child(std::move(child)), group_id(gid), agg_id(aid), func(func) {}

  void open() override;
  void close() override;
  bool next(Batch &batch) override;
};
