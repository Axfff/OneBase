#include "onebase/execution/executors/seq_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  // TODO(student): Initialize the sequential scan
  // - Get the table from catalog using plan_->GetTableOid()
  // - Set up iterator to table_heap->Begin()
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // TODO(student): Return the next tuple from the table
  // - Advance iterator, skip tuples that don't match predicate
  // - Return false when no more tuples
  while (iter_ != end_) {
    Tuple current = *iter_;
    RID current_rid = iter_.GetRID();
    ++iter_;

    const auto &predicate = plan_->GetPredicate();
    if (predicate != nullptr) {
      Value predicate_value = predicate->Evaluate(&current, &table_info_->schema_);
      if (predicate_value.IsNull() || !predicate_value.GetAsBoolean()) {
        continue;
      }
    }

    std::vector<Value> values;
    values.reserve(table_info_->schema_.GetColumnCount());
    for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); ++i) {
      values.push_back(current.GetValue(&table_info_->schema_, i));
    }
    *tuple = Tuple(std::move(values));
    tuple->SetRID(current_rid);
    if (rid != nullptr) {
      *rid = current_rid;
    }
    return true;
  }
  return false;
}

}  // namespace onebase
