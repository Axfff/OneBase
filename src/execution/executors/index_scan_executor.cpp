#include "onebase/execution/executors/index_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  // TODO(student): Initialize index scan using the B+ tree index
  auto *catalog = GetExecutorContext()->GetCatalog();
  table_info_ = catalog->GetTable(plan_->GetTableOid());
  index_info_ = catalog->GetIndex(plan_->GetIndexOid());
  matching_rids_.clear();
  cursor_ = 0;

  if (table_info_ == nullptr || index_info_ == nullptr || !index_info_->SupportsPointLookup()) {
    return;
  }

  Tuple dummy;
  Value lookup_value = plan_->GetLookupKey()->Evaluate(&dummy, &table_info_->schema_);
  const auto *rids = index_info_->LookupInteger(lookup_value.GetAsInteger());
  if (rids != nullptr) {
    matching_rids_ = *rids;
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // TODO(student): Return next tuple from index scan
  while (cursor_ < matching_rids_.size()) {
    RID current_rid = matching_rids_[cursor_++];
    Tuple current = table_info_->table_->GetTuple(current_rid);

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
