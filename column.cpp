#include "operator.h"
#include "types.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <ios>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

template <typename T> bool check_datatype(std::string_view source) {
  static_assert(std::is_arithmetic_v<T>, "numeric types only");
  if (source.empty())
    return false;

  const char *first = source.data();
  const char *last = first + source.size();
  T target{};

  if constexpr (std::is_integral_v<T>) {
    auto [ptr, ec] = std::from_chars(first, last, target);
    return ec == std::errc{} && ptr == last;
  } else {
    std::string buf(source);
    char *end = nullptr;
    errno = 0;
    if constexpr (std::is_same_v<T, double>)
      target = std::strtod(buf.c_str(), &end);
    else
      target = std::strtold(buf.c_str(), &end);
    return errno == 0 && end == buf.c_str() + buf.size();
  }
}

template <typename T> T convert_datatype(std::string_view source) {
  static_assert(std::is_arithmetic_v<T>, "numeric types only");
  const char *first = source.data();
  const char *last = first + source.size();
  T target{};

  if constexpr (std::is_integral_v<T>) {
    // std::cout << "Source: " << source << "\n";
    auto [ptr, ec] = std::from_chars(first, last, target);
    // std::cout << "Target: " << target << "\n";
    return target;
  } else {
    // std::cout << "Why you lying?\n";
    std::string buf(source);
    char *end = nullptr;
    errno = 0;
    if constexpr (std::is_same_v<T, double>)
      target = std::strtod(buf.c_str(), &end);
    else
      target = std::strtold(buf.c_str(), &end);
    return target;
  }
}

void parse_headers(std::string &line, Table &table, Schema &schema) {
  auto start = 0;
  auto end = line.find(',');
  while (end != std::string::npos) {
    std::string name = line.substr(start, end - start);
    Column col{};
    col.name = name;
    col.type = "string";
    table.push_back(col);
    schema[name] = table.size() - 1;
    start = end + 1;
    end = line.find(',', start);
  }

  std::string name = line.substr(start, end - start);
  while (!name.empty() && (name.back() == '\r' || name.back() == '\n')) {
    name.pop_back();
  }
  Column col{};
  col.name = name;
  table.push_back(col);
  schema[name] = table.size() - 1;
}

void parse_row(std::string &line, Table &table) {
  auto start = 0;
  auto end = line.find(',');
  auto count = 0;
  while (end != std::string::npos) {
    auto val = line.substr(start, end - start);
    auto b = val != "";
    auto &cd = table[count].data;
    auto *vec = std::get_if<std::vector<std::string>>(&cd);
    if (vec) {
      vec->push_back(val);
    } else {
      cd = std::vector<std::string>{val};
    }
    table[count].valid_mask.push_back(b);
    table[count].length += 1;
    start = end + 1;
    end = line.find(',', start);
    count += 1;
  }
  auto val = line.substr(start, end - start);
  if (!val.empty() && val.back() == '\r')
    val.pop_back();
  auto b = val != "";
  auto &cd = table[count].data;
  auto *vec = std::get_if<std::vector<std::string>>(&cd);
  if (vec) {
    vec->push_back(val);
  } else {
    cd = std::vector<std::string>{val};
  }
  table[count].valid_mask.push_back(b);
  table[count].length += 1;
}

void schema(const Table &table, std::string &output);

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Error: command not provided\n";
    return 1;
  } else if (argc == 2) {
    std::cerr << "Error: file required\n";
    return 1;
  }

  std::string filename = argv[2];
  std::ifstream file(filename);
  if (!file) {
    std::cerr << "Unable to open file!\n";
    return 1;
  }

  std::string line;
  if (!std::getline(file, line)) {
    std::cerr << "Unable to find column line\n";
    return 1;
  }

  Table table;
  Schema table_schema;
  parse_headers(line, table, table_schema);

  while (std::getline(file, line)) {
    parse_row(line, table);
  }

  for (auto &col : table) {
    auto &cd = col.data;
    auto *vec = std::get_if<std::vector<std::string>>(&cd);
    if (!vec) {
      std::cerr << "Error in datatype detection and conversion\n";
      return 1;
    }
    if (std::all_of(vec->begin(), vec->end(), check_datatype<int64_t>)) {
      // std::cout << "Reached here\n";
      std::vector<int64_t> next;
      next.reserve(vec->size());
      std::transform(vec->begin(), vec->end(), std::back_inserter(next),
                     convert_datatype<int64_t>);
      // std::cout << "Reached here1\n";
      cd = next;
      col.type = "int64";
      continue;
    }

    if (std::all_of(vec->begin(), vec->end(), check_datatype<double>)) {
      // std::cout << "Reached here2\n";
      std::vector<double> next;
      next.reserve(vec->size());
      std::transform(vec->begin(), vec->end(), std::back_inserter(next),
                     convert_datatype<double>);
      cd = next;
      col.type = "double";
      // std::cout << "Reached here2\n";
      continue;
    }
    col.type = "string";
  }

  std::string output{};
  auto begin = std::chrono::high_resolution_clock::now();

  // SCHEMA
  if (std::string_view(argv[1]) == "schema") {
    schema(table, output);
  }

  // SELECT
  else if (std::string_view(argv[1]) == "select") {
    if (4 >= argc) {
      std::cerr << "Error: incomplete command\n";
      return 1;
    }

    if (!check_datatype<int64_t>(std::string_view(argv[argc - 1]))) {
      std::cerr << "Error: last argument needs to be an integer\n";
      return 1;
    }

    std::string cols{argv[3]};
    std::vector<size_t> cols_index;
    auto start = 0;
    auto end = cols.find(',', start);
    while (end != std::string::npos) {
      auto col = cols.substr(start, end - start);
      auto index = table_schema[col];
      cols_index.push_back(index);
      start = end + 1;
      end = cols.find(',', start);
    }
    auto col = cols.substr(start, end - start);
    auto index = table_schema[col];
    cols_index.push_back(index);
    auto scan = std::make_unique<ScanOp>(table);
    auto project = std::make_unique<ProjectionOp>(
        std::move(scan), cols_index.begin(), cols_index.end());
    int limit = std::stoi(std::string(argv[argc - 1]));
    auto limiter = std::make_unique<LimitOp>(std::move(project), limit);
    limiter->open();
    std::vector<Batch> batches;
    Batch in;
    while (limiter->next(in)) {
      batches.push_back(std::move(in));
    }
    for (auto &b : batches) {
      for (auto i = 0; i < b.row_mask.size(); i++) {
        if (!b.row_mask[i]) {
          continue;
        }
        output += "\n";
        for (auto &cp : b.columns) {
          std::visit(
              [&](auto &c) {
                using T = typename std::decay_t<decltype(*c)>;
                if constexpr (std::is_same_v<T, std::string>) {
                  output += *(c + i) + ",";
                } else {
                  output += std::to_string(*(c + i)) + ",";
                }
              },
              cp);
        }
      }
      output.pop_back();
    }

    limiter->close();
  }

  // FILTER
  else if (std::string_view(argv[1]) == "filter") {
    if (5 >= argc) {
      std::cerr << "Error: incomplete command\n";
      return 1;
    }

    if (!check_datatype<double>(std::string_view(argv[argc - 1]))) {
      std::cerr << "Error: last argument needs to be an integer\n";
      return 1;
    }

    std::string colname = std::string(argv[3]);
    auto index = table_schema[colname];
    auto op_string = std::string(argv[4]);
    Ops op;

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
      return 1;
    }
    auto limit = std::stod(std::string(argv[5]));

    auto scan = std::make_unique<ScanOp>(table);
    auto filter = std::make_unique<FilterOp>(std::move(scan), index, op, limit);
    filter->open();
    auto count{0};
    Batch in;
    while (filter->next(in)) {
      count += std::accumulate(in.row_mask.begin(), in.row_mask.end(), 0);
    }

    output = "count:" + std::to_string(count) + "\n";
    filter->close();
  }

  // AGG
  else if (std::string_view(argv[1]) == "agg") {
    if (4 >= argc) {
      std::cerr << "Error: incomplete command\n";
      return 1;
    }

    std::string colname{argv[4]};
    std::string func{argv[3]};
    Func efunc;
    if (func == "sum") {
      efunc = Func::SUM;
    } else if (func == "count") {
      efunc = Func::COUNT;
    } else if (func == "avg") {
      efunc = Func::AVG;
    } else if (func == "max") {
      efunc = Func::MAX;
    } else if (func == "min") {
      efunc = Func::MIN;
    }
    auto scan = std::make_unique<ScanOp>(table);
    auto agg =
        std::make_unique<AggOp>(std::move(scan), efunc, table_schema[colname]);

    agg->open();
    std::vector<Batch> batches;
    Batch in;
    while (agg->next(in)) {
      batches.push_back(std::move(in));
    }

    if (batches.size() > 1) {
      std::cerr << "Error: multiple agg outputs\n";
      return 1;
    }

    double d;
    switch (efunc) {
    case Func::SUM:
      d = agg->ag.sum;
      break;
    case Func::COUNT:
      d = agg->ag.count;
      break;
    case Func::AVG:
      d = agg->ag.sum / agg->ag.count;
      break;
    case Func::MAX:
      d = agg->ag.max;
      break;
    case Func::MIN:
      d = agg->ag.min;
      break;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(10) << d;
    output = oss.str();
    agg->close();
  }

  //  GROUPBY
  else if (std::string_view(argv[1]) == "groupby") {
    if (5 >= argc) {
      std::cerr << "Error: incomplete command\n";
      return 1;
    }
    auto groupcol = std::string(argv[3]);
    auto group_id = table_schema[groupcol];
    auto aggcol = std::string(argv[5]);
    auto agg_id = table_schema[aggcol];
    auto func = std::string(argv[4]);
    auto scan = std::make_unique<ScanOp>(table);
    Func efunc;
    if (func == "sum") {
      efunc = Func::SUM;
    } else if (func == "count") {
      efunc = Func::COUNT;
    } else if (func == "avg") {
      efunc = Func::AVG;
    } else if (func == "max") {
      efunc = Func::MAX;
    } else if (func == "min") {
      efunc = Func::MIN;
    }
    auto groupby =
        std::make_unique<GroupByOp>(std::move(scan), group_id, agg_id, efunc);
    groupby->open();
    std::vector<Batch> batches;
    Batch in;
    while (groupby->next(in)) {
      batches.push_back(std::move(in));
    }

    if (batches.size() > 1) {
      std::cerr << "Error: multiple groupby outputs\n";
      return 1;
    }

    std::function<double(AggState *)> getValue;
    if (func == "count") {
      getValue = [](AggState *agg) { return agg->count; };
    } else if (func == "sum") {
      getValue = [](AggState *agg) { return agg->sum; };
    } else if (func == "avg") {
      getValue = [](AggState *agg) { return agg->sum / agg->count; };
    } else if (func == "min") {
      getValue = [](AggState *agg) { return agg->min; };
    } else if (func == "max") {
      getValue = [](AggState *agg) { return agg->max; };
    } else {
      std::cerr << "Error: Invalid func\n";
    }

    std::visit(
        [&](auto &map) {
          if (map.size() != 0) {
            using KeyType = typename std::decay_t<decltype(map)>::key_type;
            for (auto &[g, as] : map) {
              if constexpr (std::is_same_v<KeyType, std::string>) {
                output += g;
              } else {
                output += std::to_string(g);
              }
              std::ostringstream oss;
              oss << std::fixed << std::setprecision(10) << getValue(&as);
              output += " " + oss.str();
              output += "\n";
            }
          }
        },
        groupby->running_state);

    groupby->close();
  }

  else if (std::string_view(argv[1]) == "pipeline") {
    if (argc < 2) {
      std::cerr << "Error: Incomplete pipeline command\n";
      return 1;
    } else if (argc <= 2) {
      std::cout << "count: " << table[0].length << "\n";
      return 1;
    }
    std::unique_ptr<Operator> root;
    root = std::make_unique<ScanOp>(table);
    bool terminal{false};
    int i{3};
    while (i < argc) {
      if (std::string_view(argv[i]) == "filter") {
        if (i + 3 >= argc) {
          std::cerr << "Error: incomplete command\n";
          return 1;
        }

        if (!check_datatype<double>(std::string_view(argv[i + 3]))) {
          std::cerr << "Error: last argument needs to be numeric\n";
          return 1;
        }

        std::string colname = std::string(argv[i + 1]);
        auto index = table_schema[colname];
        auto op_string = std::string(argv[i + 2]);
        Ops op;

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
          return 1;
        }
        auto limit = std::stod(std::string(argv[i + 3]));

        root = std::make_unique<FilterOp>(std::move(root), index, op, limit);
        i += 4;
      } else if (std::string_view(argv[i]) == "agg") {
        if (i + 2 >= argc) {
          std::cerr << "Error: incomplete command\n";
          return 1;
        }

        std::string colname{argv[i + 2]};
        std::string func = argv[i + 1];
        Func efunc;
        if (func == "sum") {
          efunc = Func::SUM;
        } else if (func == "count") {
          efunc = Func::COUNT;
        } else if (func == "avg") {
          efunc = Func::AVG;
        } else if (func == "max") {
          efunc = Func::MAX;
        } else if (func == "min") {
          efunc = Func::MIN;
        }
        auto agg = std::make_unique<AggOp>(std::move(root), efunc,
                                           table_schema[colname]);
        std::vector<Batch> batches;
        Batch in;
        agg->open();
        while (agg->next(in)) {
          batches.push_back(std::move(in));
        }
        if (batches.size() > 1) {
          std::cerr << "Error: multiple agg outputs\n";
          return 1;
        }

        if (!agg->ag.first) {
          double d;
          switch (efunc) {
          case Func::SUM:
            d = agg->ag.sum;
            break;
          case Func::COUNT:
            d = agg->ag.count;
            break;
          case Func::AVG:
            d = agg->ag.sum / agg->ag.count;
            break;
          case Func::MAX:
            d = agg->ag.max;
            break;
          case Func::MIN:
            d = agg->ag.min;
            break;
          }
          std::ostringstream oss;
          oss << std::fixed << std::setprecision(10) << d;
          output = oss.str();
        } else if (func == "count") {
          double d;
          d = agg->ag.count;
          std::ostringstream oss;
          oss << std::fixed << std::setprecision(10) << d;
          output = oss.str();
        }

        agg->close();
        i += 3;
        terminal = true;
        break;
      } else if (std::string_view(argv[i]) == "select") {
        if (i + 2 >= argc) {
          std::cerr << "Error: incomplete command\n";
          return 1;
        }

        if (!check_datatype<int64_t>(std::string_view(argv[i + 2]))) {
          std::cerr << "Error: last argument needs to be an integer\n";
          return 1;
        }
        std::string cols{argv[i + 1]};
        std::vector<size_t> cols_index;
        auto start = 0;
        auto end = cols.find(',', start);
        while (end != std::string::npos) {
          auto col = cols.substr(start, end - start);
          auto index = table_schema[col];
          cols_index.push_back(index);
          start = end + 1;
          end = cols.find(',', start);
        }
        auto col = cols.substr(start, end - start);
        auto index = table_schema[col];
        cols_index.push_back(index);
        root = std::make_unique<ProjectionOp>(
            std::move(root), cols_index.begin(), cols_index.end());
        int limit = std::stoi(std::string(argv[i + 2]));
        auto limiter = std::make_unique<LimitOp>(std::move(root), limit);
        limiter->open();
        std::vector<Batch> batches;
        Batch in;
        while (limiter->next(in)) {
          batches.push_back(std::move(in));
        }
        for (auto &b : batches) {
          for (auto i = 0; i < b.row_mask.size(); i++) {
            if (!b.row_mask[i]) {
              continue;
            }
            output += "\n";
            for (auto &cp : b.columns) {
              std::visit(
                  [&](auto &c) {
                    using T = typename std::decay_t<decltype(*c)>;
                    if constexpr (std::is_same_v<T, std::string>) {
                      output += *(c + i) + ",";
                    } else {
                      output += std::to_string(*(c + i)) + ",";
                    }
                  },
                  cp);
            }
            output.pop_back();
          }
        }

        limiter->close();
        terminal = true;
        i += 3;
        break;
      } else if (std::string_view(argv[i]) == "groupby") {
        if (i + 3 >= argc) {
          std::cerr << "Error: incomplete command\n";
          return 1;
        }

        auto groupcol = std::string(argv[i + 1]);
        auto group_id = table_schema[groupcol];
        auto aggcol = std::string(argv[i + 3]);
        auto agg_id = table_schema[aggcol];
        auto func = std::string(argv[i + 2]);
        Func efunc;
        if (func == "sum") {
          efunc = Func::SUM;
        } else if (func == "count") {
          efunc = Func::COUNT;
        } else if (func == "avg") {
          efunc = Func::AVG;
        } else if (func == "max") {
          efunc = Func::MAX;
        } else if (func == "min") {
          efunc = Func::MIN;
        }
        auto groupby = std::make_unique<GroupByOp>(std::move(root), group_id,
                                                   agg_id, efunc);
        groupby->open();
        std::vector<Batch> batches;
        Batch in;
        while (groupby->next(in)) {
          batches.push_back(std::move(in));
        }

        if (batches.size() > 1) {
          std::cerr << "Error: multiple groupby outputs\n";
          return 1;
        }

        std::function<double(AggState *)> getValue;
        if (func == "count") {
          getValue = [](AggState *agg) { return agg->count; };
        } else if (func == "sum") {
          getValue = [](AggState *agg) { return agg->sum; };
        } else if (func == "avg") {
          getValue = [](AggState *agg) { return agg->sum / agg->count; };
        } else if (func == "min") {
          getValue = [](AggState *agg) { return agg->min; };
        } else if (func == "max") {
          getValue = [](AggState *agg) { return agg->max; };
        } else {
          std::cerr << "Error: Invalid func\n";
        }

        std::visit(
            [&](auto &map) {
              if (map.size() != 0) {
                using KeyType = typename std::decay_t<decltype(map)>::key_type;
                for (auto &[g, as] : map) {
                  if constexpr (std::is_same_v<KeyType, std::string>) {
                    output += g;
                  } else {
                    output += std::to_string(g);
                  }
                  std::ostringstream oss;
                  oss << std::fixed << std::setprecision(10) << getValue(&as);
                  output += " " + oss.str();
                  output += "\n";
                }
              }
            },
            groupby->running_state);
        groupby->close();
        i += 4;
        terminal = true;
        break;
      } else {
        std::cerr << "Error: Invalid pipeline command/stage\n";
        return 1;
      }
    }

    if (i < argc) {
      std::cerr << "Error: invalid pipeline command\n";
      return 1;
    }

    if (!terminal) {
      root->open();
      auto count{0};
      Batch in;
      while (root->next(in)) {
        count += std::accumulate(in.row_mask.begin(), in.row_mask.end(), 0);
      }

      output = "count:" + std::to_string(count) + "\n";
      root->close();
    }
  }

  else {
    std::cerr << "Error: invalid command\n";
    return 1;
  }

  auto finish = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = finish - begin;
  std::cerr << "QUERY_MS: " << duration.count() << "\n";
  if (output != "") {
    std::cout << output;
  }
  return 0;
}

void schema(const Table &table, std::string &output) {
  int64_t length{0};
  for (auto col : table) {
    output += col.name + ":" + col.type + "\n";
    std::visit([&](auto &c) { length = std::max<int64_t>(length, c.size()); },
               col.data);
  }
  output = "rows:" + std::to_string(length) + "\n" + output;
}
