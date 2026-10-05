#pragma once
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using ColumnData = std::variant<std::vector<int64_t>, std::vector<double>,
                                std::vector<std::string>>;
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

struct Batch {
  std::vector<ColumnData> columns;
  size_t size{0};
};
