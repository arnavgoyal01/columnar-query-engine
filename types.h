#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using pos_inf_double = std::numeric_limits<double>;
using ColumnData = std::variant<std::vector<int64_t>, std::vector<double>,
                                std::vector<std::string>>;
using ColumnPointer =
    std::variant<const int64_t *, const double *, const std::string *>;
enum class Func { SUM, COUNT, AVG, MIN, MAX };
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
  double sum{0}, count{0}, max{-pos_inf_double::infinity()},
      min{pos_inf_double::infinity()};
  bool first{true};
  void add_row(int i);
  void updateSum(double value);
  void updateMin(double value);
  void updateMax(double value);
  void reset();
};

using GroupsIndex = std::variant<std::unordered_map<int64_t, AggState>,
                                 std::unordered_map<double, AggState>,
                                 std::unordered_map<std::string, AggState>>;

struct Batch {
  std::vector<ColumnPointer> columns;
  std::vector<uint8_t> row_mask;
};

using Ops =
    std::variant<std::equal_to<>, std::not_equal_to<>, std::less<>,
                 std::less_equal<>, std::greater<>, std::greater_equal<>>;
