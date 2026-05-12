#include "onebase/execution/executors/insert_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  // TODO(student): Initialize child executor
  child_executor_->Init();
  has_inserted_ = false;
}

auto InsertExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // TODO(student): Insert tuples from child into the table
  // - Get tuples from child, insert into table_heap
  // - Update any indexes
  // - Return count of inserted rows as a single integer tuple
  if (has_inserted_) {
    return false;
  }
  has_inserted_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  auto indexes = catalog->GetTableIndexes(table_info->name_);

  int32_t count = 0;
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    auto inserted_rid = table_info->table_->InsertTuple(child_tuple);
    if (!inserted_rid.has_value()) {
      continue;
    }
    count++;

    for (auto *index_info : indexes) {
      if (!index_info->SupportsPointLookup()) {
        continue;
      }
      int32_t key = child_tuple.GetValue(&table_info->schema_, index_info->GetLookupAttr()).GetAsInteger();
      index_info->InsertEntry(key, inserted_rid.value());
    }
  }

  *tuple = Tuple({Value(TypeId::INTEGER, count)});
  if (rid != nullptr) {
    *rid = RID{};
  }
  return true;
}

}  // namespace onebase
