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
  Ops pred;
  size_t index;
  double limit;

  FilterOp(std::unique_ptr<Operator> child, size_t index, Ops op, double limit)
      : child(std::move(child)), pred(op), index(index), limit(limit) {}
  bool next(Batch &out) override;
  void open() override;
  void close() override;
};

struct AggOp : Operator {
  std::unique_ptr<Operator> child;
  Func func;
  size_t index;
  AggState ag;
  bool already_emitted;

  AggOp(std::unique_ptr<Operator> child, Func func, size_t index)
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
  Func func;
  bool already_emitted{false};

  GroupsIndex running_state;

  GroupByOp(std::unique_ptr<Operator> child, size_t gid, size_t aid, Func func)
      : child(std::move(child)), group_id(gid), agg_id(aid), func(func) {}

  void open() override;
  void close() override;
  bool next(Batch &batch) override;
};
