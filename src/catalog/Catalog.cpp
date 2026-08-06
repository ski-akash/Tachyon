#include "catalog/Catalog.h"

namespace quill {

void Catalog::setTableRowCount(const std::string& tableName, size_t rowCount) {
    table_row_counts_[tableName] = rowCount;
}

size_t Catalog::getTableRowCount(const std::string& tableName) const {
    auto it = table_row_counts_.find(tableName);
    if (it != table_row_counts_.end()) {
        return it->second;
    }
    return 10000; // Default fallback cost
}

} // namespace quill