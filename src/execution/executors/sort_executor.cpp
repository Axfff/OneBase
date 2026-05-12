#include "onebase/execution/executors/sort_executor.h"
#include <algorithm>
#include "onebase/common/exception.h"

namespace onebase {

SortExecutor::SortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void SortExecutor::Init() {
  // TODO(student): Materialize all tuples from child, then sort
  // - Scan all child tuples into sorted_tuples_
  // - Sort using order_by expressions and directions
  // - Reset cursor_ to 0
  child_executor_->Init();
  sorted_tuples_.clear();
  cursor_ = 0;

  Tuple tuple;
  RID rid;
  while (child_executor_->Next(&tuple, &rid)) {
    sorted_tuples_.push_back(tuple);
  }

  const auto &schema = child_executor_->GetOutputSchema();
  std::sort(sorted_tuples_.begin(), sorted_tuples_.end(),
            [&](const Tuple &lhs, const Tuple &rhs) {
              for (const auto &[is_ascending, expr] : plan_->GetOrderBys()) {
                Value left = expr->Evaluate(&lhs, &schema);
                Value right = expr->Evaluate(&rhs, &schema);
                if (left.CompareEquals(right).GetAsBoolean()) {
                  continue;
                }
                bool less = left.CompareLessThan(right).GetAsBoolean();
                return is_ascending ? less : !less;
              }
              return false;
            });
}

auto SortExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // TODO(student): Return next sorted tuple
  if (cursor_ >= sorted_tuples_.size()) {
    return false;
  }
  *tuple = sorted_tuples_[cursor_++];
  if (rid != nullptr) {
    *rid = tuple->GetRID();
  }
  return true;
}

}  // namespace onebase
