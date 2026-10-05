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
      for (auto i = 0; i < b.size; i++) {
        output += "\n";
        for (auto &cd : b.columns) {
          std::visit(
              [&](auto &c) {
                using T = typename std::decay_t<decltype(c)>::value_type;
                if constexpr (std::is_same_v<T, std::string>) {
                  output += c[i] + ",";
                } else {
                  output += std::to_string(c[i]) + ",";
                }
              },
              cd);
        }
      }
      output.pop_back();
    }
  }

  // FILTER
  else if (std::string_view(argv[1]) == "filter") {
    if (!check_datatype<double>(std::string_view(argv[argc - 1]))) {
      std::cerr << "Error: last argument needs to be an integer\n";
      return 1;
    }

    std::string colname = std::string(argv[3]);
    auto index = table_schema[colname];
    auto op_string = std::string(argv[4]);
    auto limit = std::stod(std::string(argv[5]));

    auto scan = std::make_unique<ScanOp>(table);
    auto filter =
        std::make_unique<FilterOp>(std::move(scan), index, op_string, limit);
    filter->open();
    auto count{0};
    Batch in;
    while (filter->next(in)) {
      count += in.size;
    }

    output = "count:" + std::to_string(count) + "\n";
  }

  // AGG
  else if (std::string_view(argv[1]) == "agg") {
    std::string colname{argv[4]};
    std::string func{argv[3]};

    auto scan = std::make_unique<ScanOp>(table);
    auto agg =
        std::make_unique<AggOp>(std::move(scan), func, table_schema[colname]);

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

    auto d = std::get_if<std::vector<double>>(&batches[0].columns[0]);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(10) << d->at(0);
    output = oss.str();
  }

  //  GROUPBY
  else if (std::string_view(argv[1]) == "groupby") {
    auto groupcol = std::string(argv[3]);
    auto group_id = table_schema[groupcol];
    auto aggcol = std::string(argv[5]);
    auto agg_id = table_schema[aggcol];
    auto func = std::string(argv[4]);
    auto scan = std::make_unique<ScanOp>(table);
    auto groupby =
        std::make_unique<GroupByOp>(std::move(scan), group_id, agg_id, func);
    groupby->open();
    std::vector<Batch> batches;
    Batch in;
    while (groupby->next(in)) {
      batches.push_back(std::move(in));
    }

    if (batches.size() > 1) {
      std::cerr << "Error: multiple groupby outputs\n";
      return 1;
    } else if (batches[0].columns.size() > 2) {
      std::cerr << "Error: too many columns in groupby output\n";
      return 1;
    }

    auto out = batches[0];

    std::visit(
        [&](auto &groups, auto &ans) {
          using G = typename std::decay_t<decltype(groups)>::value_type;
          using A = typename std::decay_t<decltype(ans)>::value_type;

          if constexpr (!std::is_same_v<A, double>) {
            std::cerr
                << "Error: recieved non-double column for groupby ans column\n";
            // return 1;
          } else {
            for (auto i = 0; i < out.size; i++) {
              if constexpr (std::is_same_v<G, std::string>) {
                output += groups[i];
              } else {
                output += std::to_string(groups[i]);
              }

              std::ostringstream oss;
              oss << std::fixed << std::setprecision(10) << ans[i];
              output += oss.str();
              output += "\n";
            }
          }
        },
        out.columns[0], out.columns[1]);

    // groupby(table, table_schema, argv, argc);
  }

  else if (std::string_view(argv[1]) == "pipeline") {
    std::unique_ptr<Operator> root;
    root = std::make_unique<ScanOp>(table);
    std::string terminal{};
    int i{2};
    while (i < argc) {
      if (std::string_view(argv[i]) == "filter") {
        if (!check_datatype<double>(std::string_view(argv[i + 4]))) {
          std::cerr << "Error: last argument needs to be an integer\n";
          return 1;
        }

        std::string colname = std::string(argv[i + 2]);
        auto index = table_schema[colname];
        auto op_string = std::string(argv[i + 3]);
        auto limit = std::stod(std::string(argv[i + 4]));
        root = std::make_unique<FilterOp>(std::move(root), index, op_string,
                                          limit);
        i += 5;
      } else if (std::string_view(argv[i]) == "agg") {
        std::string colname{argv[i + 3]};
        std::string func{argv[i + 2]};

        root = std::make_unique<AggOp>(std::move(root), func,
                                       table_schema[colname]);
        terminal = "agg";
        i += 4;
        break;
      } else if (std::string_view(argv[i]) == "select") {
        if (!check_datatype<int64_t>(std::string_view(argv[i + 3]))) {
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
      } else if (std::string_view(argv[i]) == "groupby") {

      } else {
        std::cerr << "Error: Invalid pipeline command/stage\n";
        return 1;
      }
    }
  }

  else {
    std::cerr << "Error: invalid command\n";
    return 1;
  }
  auto finish = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = finish - begin;
  std::cerr << "QUERY_MS: " << duration.count();
  std::cout << output;
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

// void select(const Table &table, Schema &schema, char **args_ptr, int argc) {
//   int limit = std::stoi(std::string(args_ptr[argc - 1]));
//
//   std::string cols{args_ptr[3]};
//   std::vector<int> cols_index;
//   auto start = 0;
//   auto end = cols.find(',', start);
//   while (end != std::string::npos) {
//     auto col = cols.substr(start, end - start);
//     auto index = schema[col];
//     cols_index.push_back(index);
//     start = end + 1;
//     end = cols.find(',', start);
//   }
//   auto col = cols.substr(start, end - start);
//   auto index = schema[col];
//   cols_index.push_back(index);
//   std::string output{};
//   for (int i = 0; i < limit; i++) {
//     output += '\n';
//     for (auto id : cols_index) {
//       auto &c = table[id];
//       if (auto *v = std::get_if<std::vector<int64_t>>(&c.data)) {
//         output += std::to_string((*v)[i]) + ",";
//       } else if (auto *v = std::get_if<std::vector<double>>(&c.data)) {
//         output += std::to_string((*v)[i]) + ",";
//       } else if (auto *v = std::get_if<std::vector<std::string>>(&c.data)) {
//         output += (*v)[i] + ",";
//       } else {
//         std::cerr << "Error: column datatype not detected\n";
//       }
//     }
//     if (output.back() == ',')
//       output.pop_back();
//   }
//   std::cout << output;
// }
//
// void filter(const Table &table, Schema &schema, char **args_ptr, int argc) {
//   std::string colname = std::string(args_ptr[3]);
//   auto index = schema[colname];
//   auto &c = table[index];
//   std::function<bool(double, double)> op;
//   auto op_string = std::string(args_ptr[4]);
//   if (op_string == "==") {
//     op = std::equal_to<>{};
//   } else if (op_string == "!=") {
//     op = std::not_equal_to<>{};
//   } else if (op_string == "<") {
//     op = std::less<>{};
//   } else if (op_string == "<=") {
//     op = std::less_equal<>{};
//   } else if (op_string == ">") {
//     op = std::greater<>{};
//   } else if (op_string == ">=") {
//     op = std::greater_equal<>{};
//   } else {
//     std::cerr << "Error: Invalid operation\n";
//     return;
//   }
//
//   auto limit = std::stod(std::string(args_ptr[5]));
//   auto count{0};
//   auto pred = [&limit, &count, &op](double d) { return count += op(d, limit);
//   };
//
//   std::visit(
//       [&](auto &v) {
//         using T = typename std::decay_t<decltype(v)>::value_type;
//         if constexpr (std::is_same_v<T, std::string>) {
//           std::cerr << "Error: string column datatype not supported\n";
//         } else {
//           std::for_each(v.begin(), v.end(), pred);
//         }
//       },
//       c.data);
//   std::cout << "count:" << count << "\n";
// }
//
// void agg(const Table &table, Schema &schema, char **args_ptr, int argc) {
//   std::string colname{args_ptr[4]};
//   std::string func{args_ptr[3]};
//   const auto &c = table[schema[colname]];
//
//   if (func == "count") {
//     std::visit([](const auto &v) { std::cout << v.size() << "\n"; }, c.data);
//     return;
//   }
//   std::visit(
//       [&](const auto &v) {
//         using T = typename std::decay_t<decltype(v)>::value_type;
//
//         if constexpr (!std::is_arithmetic_v<T>) {
//           std::cerr << "Error: invalid func for string datatype\n";
//         } else {
//           if (v.empty()) {
//             std::cerr << "Error: column is empty\n";
//             return;
//           }
//           double ans;
//           if (func == "sum") {
//             ans = std::accumulate(v.begin(), v.end(), 0.0);
//           } else if (func == "avg") {
//             ans = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
//           } else if (func == "max") {
//             ans = *std::max_element(v.begin(), v.end());
//           } else if (func == "min") {
//             ans = *std::min_element(v.begin(), v.end());
//           } else {
//             std::cerr << "Error: invalid func\n";
//             return;
//           }
//           // Force fixed-point notation AND adjust decimal precision
//           std::cout << std::fixed << std::setprecision(10) << ans << "\n";
//         }
//       },
//       c.data);
// }
//
// void groupby(const Table &table, Schema &schema, char **args_ptr, int argc) {
//   auto groupcol = std::string(args_ptr[3]);
//   auto &gc = table[schema[groupcol]];
//   auto aggcol = std::string(args_ptr[5]);
//   auto &ac = table[schema[aggcol]];
//   auto func = std::string(args_ptr[4]);
//
//   std::visit(
//       [&](const auto &keys, const auto &vals) {
//         using K = typename std::decay_t<decltype(keys)>::value_type;
//         using V = typename std::decay_t<decltype(vals)>::value_type;
//
//         std::unordered_map<K, std::vector<size_t>> group_cols;
//         for (auto i = 0; i < keys.size(); i++) {
//           group_cols[keys[i]].push_back(i);
//         }
//
//         for (auto &[key, idx] : group_cols) {
//           if (func == "count") {
//             std::cout << key << idx.size() << "\n";
//           } else if constexpr (!std::is_arithmetic_v<V>) {
//             std::cerr << "Error: Invalid func for string agg col\n";
//             return;
//           } else {
//             std::vector<V> subset;
//             subset.reserve(idx.size());
//             for (auto id : idx) {
//               if (id < vals.size()) {
//                 subset.push_back(vals[id]);
//               }
//             }
//
//             double ans{};
//             if (func == "sum") {
//               ans = std::accumulate(subset.begin(), subset.end(), 0.0);
//             } else if (func == "avg") {
//               ans = std::accumulate(subset.begin(), subset.end(), 0.0) /
//                     idx.size();
//             } else if (func == "max") {
//               ans = *std::max_element(subset.begin(), subset.end());
//             } else if (func == "min") {
//               ans = *std::min_element(subset.begin(), subset.end());
//             } else {
//               std::cerr << "Error: invalid func\n";
//               return;
//             }
//
//             std::cout << key << std::fixed << std::setprecision(10) << ans
//                       << "\n";
//           }
//         }
//       },
//       gc.data, ac.data);
// }
