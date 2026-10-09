#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using ColumnData = std::variant<std::vector<int64_t>, std::vector<double>,
                                std::vector<std::string>>;
using ColumnPointer =
    std::variant<const int64_t *, const double *, const std::string *>;
using ColumnType = std::string;
struct Column {
  std::string name;
  ColumnData data;
  ColumnType type;
  int length{0};
  std::vector<bool> valid_mask;
};

using Table = std::vector<Column>;
using Schema = std::unordered_map<std::string, std::size_t>;
using ScalarVariant = std::variant<double, int64_t, std::string>;

struct AggState {
  double sum{0}, count{0}, max, min;
  bool first{true};
  void add_row();
  void update(double value);
};

struct Batch {
  std::vector<ColumnPointer> columns;
  std::vector<uint8_t> row_mask;
};
