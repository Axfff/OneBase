#include "onebase/execution/executors/delete_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  // TODO(student): Initialize child executor
  child_executor_->Init();
  has_deleted_ = false;
}

auto DeleteExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // TODO(student): Delete tuples identified by child executor
  // - Get tuples from child, delete from table_heap
  // - Update any indexes
  // - Return count of deleted rows
  if (has_deleted_) {
    return false;
  }
  has_deleted_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  auto indexes = catalog->GetTableIndexes(table_info->name_);

  int32_t count = 0;
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    for (auto *index_info : indexes) {
      if (!index_info->SupportsPointLookup()) {
        continue;
      }
      int32_t key = child_tuple.GetValue(&table_info->schema_, index_info->GetLookupAttr()).GetAsInteger();
      index_info->RemoveEntry(key, child_rid);
    }
    table_info->table_->DeleteTuple(child_rid);
    count++;
  }

  *tuple = Tuple({Value(TypeId::INTEGER, count)});
  if (rid != nullptr) {
    *rid = RID{};
  }
  return true;
}

}  // namespace onebase
