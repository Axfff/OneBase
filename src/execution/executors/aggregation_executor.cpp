#include "onebase/execution/executors/aggregation_executor.h"
#include "onebase/common/exception.h"
#include <string>

namespace onebase {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                          std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void AggregationExecutor::Init() {
  // TODO(student): Initialize child and build aggregation hash table
  // - Scan all tuples from child
  // - Group by group_by expressions
  // - Compute aggregates (COUNT, SUM, MIN, MAX) per group
  result_tuples_.clear();
  cursor_ = 0;
  child_executor_->Init();

  struct AggregateState {
    std::vector<Value> group_values;
    std::vector<Value> aggregate_values;
    std::vector<bool> has_value;
  };

  auto make_group_key = [](const std::vector<Value> &values) {
    std::string key;
    for (const auto &value : values) {
      auto type = static_cast<int>(value.GetTypeId());
      key.append(reinterpret_cast<const char *>(&type), sizeof(type));
      const char is_null = value.IsNull() ? 1 : 0;
      key.push_back(is_null);
      if (!value.IsNull()) {
        uint32_t size = value.GetSerializedSize();
        key.append(reinterpret_cast<const char *>(&size), sizeof(size));
        const size_t old_size = key.size();
        key.resize(old_size + size);
        value.SerializeTo(key.data() + old_size);
      }
    }
    return key;
  };

  auto make_initial_state = [&]() {
    AggregateState state;
    state.aggregate_values.reserve(plan_->GetAggregateTypes().size());
    state.has_value.assign(plan_->GetAggregateTypes().size(), false);
    for (auto agg_type : plan_->GetAggregateTypes()) {
      switch (agg_type) {
        case AggregationType::CountStarAggregate:
        case AggregationType::CountAggregate:
          state.aggregate_values.emplace_back(TypeId::INTEGER, 0);
          break;
        case AggregationType::SumAggregate:
        case AggregationType::MinAggregate:
        case AggregationType::MaxAggregate:
          state.aggregate_values.emplace_back(plan_->GetOutputSchema()
                                                  .GetColumn(plan_->GetGroupBys().size() +
                                                             state.aggregate_values.size())
                                                  .GetType());
          break;
      }
    }
    return state;
  };

  std::unordered_map<std::string, size_t> group_index;
  std::vector<AggregateState> states;

  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    std::vector<Value> group_values;
    group_values.reserve(plan_->GetGroupBys().size());
    for (const auto &expr : plan_->GetGroupBys()) {
      group_values.push_back(expr->Evaluate(&child_tuple, &child_executor_->GetOutputSchema()));
    }

    std::string key = make_group_key(group_values);
    auto it = group_index.find(key);
    if (it == group_index.end()) {
      group_index[key] = states.size();
      states.push_back(make_initial_state());
      states.back().group_values = std::move(group_values);
      it = group_index.find(key);
    }

    auto &state = states[it->second];
    for (size_t i = 0; i < plan_->GetAggregateTypes().size(); ++i) {
      auto agg_type = plan_->GetAggregateTypes()[i];
      Value input = plan_->GetAggregates()[i]->Evaluate(&child_tuple, &child_executor_->GetOutputSchema());
      switch (agg_type) {
        case AggregationType::CountStarAggregate:
          state.aggregate_values[i] =
              Value(TypeId::INTEGER, state.aggregate_values[i].GetAsInteger() + 1);
          state.has_value[i] = true;
          break;
        case AggregationType::CountAggregate:
          if (!input.IsNull()) {
            state.aggregate_values[i] =
                Value(TypeId::INTEGER, state.aggregate_values[i].GetAsInteger() + 1);
            state.has_value[i] = true;
          }
          break;
        case AggregationType::SumAggregate:
          if (!input.IsNull()) {
            state.aggregate_values[i] = state.has_value[i] ? state.aggregate_values[i].Add(input) : input;
            state.has_value[i] = true;
          }
          break;
        case AggregationType::MinAggregate:
          if (!input.IsNull() &&
              (!state.has_value[i] || input.CompareLessThan(state.aggregate_values[i]).GetAsBoolean())) {
            state.aggregate_values[i] = input;
            state.has_value[i] = true;
          }
          break;
        case AggregationType::MaxAggregate:
          if (!input.IsNull() &&
              (!state.has_value[i] || input.CompareGreaterThan(state.aggregate_values[i]).GetAsBoolean())) {
            state.aggregate_values[i] = input;
            state.has_value[i] = true;
          }
          break;
      }
    }
  }

  if (states.empty() && plan_->GetGroupBys().empty()) {
    states.push_back(make_initial_state());
  }

  for (const auto &state : states) {
    std::vector<Value> values;
    values.reserve(state.group_values.size() + state.aggregate_values.size());
    values.insert(values.end(), state.group_values.begin(), state.group_values.end());
    values.insert(values.end(), state.aggregate_values.begin(), state.aggregate_values.end());
    result_tuples_.emplace_back(std::move(values));
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // TODO(student): Return next aggregation result
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  if (rid != nullptr) {
    *rid = RID{};
  }
  return true;
}

}  // namespace onebase
