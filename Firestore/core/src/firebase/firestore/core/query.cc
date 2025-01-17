/*
 * Copyright 2018 Google
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Firestore/core/src/firebase/firestore/core/query.h"

#include <algorithm>

#include "Firestore/core/src/firebase/firestore/core/field_filter.h"
#include "Firestore/core/src/firebase/firestore/model/document_key.h"
#include "Firestore/core/src/firebase/firestore/model/field_path.h"
#include "Firestore/core/src/firebase/firestore/model/resource_path.h"
#include "Firestore/core/src/firebase/firestore/util/equality.h"
#include "Firestore/core/src/firebase/firestore/util/hard_assert.h"
#include "absl/algorithm/container.h"

namespace firebase {
namespace firestore {
namespace core {
namespace {

using Operator = Filter::Operator;
using Type = Filter::Type;

using model::Document;
using model::DocumentKey;
using model::FieldPath;
using model::ResourcePath;

template <typename T>
std::vector<T> AppendingTo(const std::vector<T>& vector, T&& value) {
  std::vector<T> updated = vector;
  updated.push_back(std::forward<T>(value));
  return updated;
}

}  // namespace

Query::Query(ResourcePath path, std::string collection_group)
    : path_(std::move(path)),
      collection_group_(
          std::make_shared<const std::string>(std::move(collection_group))) {
}

// MARK: - Accessors

bool Query::IsDocumentQuery() const {
  return DocumentKey::IsDocumentKey(path_) && !collection_group_ &&
         filters_.empty();
}

const FieldPath* Query::InequalityFilterField() const {
  for (const auto& filter : filters_) {
    if (filter->IsInequality()) {
      return &filter->field();
    }
  }
  return nullptr;
}

bool Query::HasArrayContainsFilter() const {
  for (const auto& filter : filters_) {
    if (filter->IsAFieldFilter()) {
      const auto& relation_filter = static_cast<const FieldFilter&>(*filter);
      if (relation_filter.op() == Operator::ArrayContains) {
        return true;
      }
    }
  }
  return false;
}

const Query::OrderByList& Query::order_bys() const {
  if (memoized_order_bys_.empty()) {
    const FieldPath* inequality_field = InequalityFilterField();
    const FieldPath* first_order_by_field = FirstOrderByField();
    if (inequality_field && !first_order_by_field) {
      // In order to implicitly add key ordering, we must also add the
      // inequality filter field for it to be a valid query. Note that the
      // default inequality field and key ordering is ascending.
      if (inequality_field->IsKeyFieldPath()) {
        memoized_order_bys_.emplace_back(FieldPath::KeyFieldPath(),
                                         Direction::Ascending);
      } else {
        memoized_order_bys_.emplace_back(*inequality_field,
                                         Direction::Ascending);
        memoized_order_bys_.emplace_back(FieldPath::KeyFieldPath(),
                                         Direction::Ascending);
      }
    } else {
      HARD_ASSERT(
          !inequality_field || *inequality_field == *first_order_by_field,
          "First orderBy %s should match inequality field %s.",
          first_order_by_field->CanonicalString(),
          inequality_field->CanonicalString());

      bool found_key_order = false;

      Query::OrderByList result;
      for (const OrderBy& order_by : explicit_order_bys_) {
        result.push_back(order_by);
        if (order_by.field().IsKeyFieldPath()) {
          found_key_order = true;
        }
      }

      if (!found_key_order) {
        // The direction of the implicit key ordering always matches the
        // direction of the last explicit sort order
        Direction last_direction = explicit_order_bys_.empty()
                                       ? Direction::Ascending
                                       : explicit_order_bys_.back().direction();
        result.emplace_back(FieldPath::KeyFieldPath(), last_direction);
      }

      memoized_order_bys_ = std::move(result);
    }
  }
  return memoized_order_bys_;
}

const FieldPath* Query::FirstOrderByField() const {
  if (explicit_order_bys_.empty()) {
    return nullptr;
  }

  return &explicit_order_bys_.front().field();
}

// MARK: - Builder methods

Query Query::AddingFilter(std::shared_ptr<Filter> filter) const {
  HARD_ASSERT(!IsDocumentQuery(), "No filter is allowed for document query");

  const FieldPath* new_inequality_field = nullptr;
  if (filter->IsInequality()) {
    new_inequality_field = &filter->field();
  }
  const FieldPath* query_inequality_field = InequalityFilterField();
  HARD_ASSERT(!query_inequality_field || !new_inequality_field ||
                  *query_inequality_field == *new_inequality_field,
              "Query must only have one inequality field.");

  // TODO(rsgowman): ensure first orderby must match inequality field

  return Query(path_, collection_group_,
               AppendingTo(filters_, std::move(filter)), explicit_order_bys_,
               limit_, start_at_, end_at_);
}

Query Query::AddingOrderBy(OrderBy order_by) const {
  HARD_ASSERT(!IsDocumentQuery(), "No ordering is allowed for document query");

  if (explicit_order_bys_.empty()) {
    const FieldPath* inequality = InequalityFilterField();
    HARD_ASSERT(inequality == nullptr || *inequality == order_by.field(),
                "First OrderBy must match inequality field.");
  }

  return Query(path_, collection_group_, filters_,
               AppendingTo(explicit_order_bys_, std::move(order_by)), limit_,
               start_at_, end_at_);
}

Query Query::WithLimit(int32_t limit) const {
  return Query(path_, collection_group_, filters_, explicit_order_bys_, limit,
               start_at_, end_at_);
}

Query Query::StartingAt(Bound bound) const {
  return Query(path_, collection_group_, filters_, explicit_order_bys_, limit_,
               std::make_shared<Bound>(std::move(bound)), end_at_);
}

Query Query::EndingAt(Bound bound) const {
  return Query(path_, collection_group_, filters_, explicit_order_bys_, limit_,
               start_at_, std::make_shared<Bound>(std::move(bound)));
}

Query Query::AsCollectionQueryAtPath(ResourcePath path) const {
  return Query(path, /*collection_group=*/nullptr, filters_,
               explicit_order_bys_, limit_, start_at_, end_at_);
}

// MARK: - Matching

bool Query::Matches(const Document& doc) const {
  return MatchesPathAndCollectionGroup(doc) && MatchesOrderBy(doc) &&
         MatchesFilters(doc) && MatchesBounds(doc);
}

bool Query::MatchesPathAndCollectionGroup(const Document& doc) const {
  const ResourcePath& doc_path = doc.key().path();
  if (collection_group_) {
    // NOTE: path_ is currently always empty since we don't expose Collection
    // Group queries rooted at a document path yet.
    return doc.key().HasCollectionId(*collection_group_) &&
           path_.IsPrefixOf(doc_path);
  } else if (DocumentKey::IsDocumentKey(path_)) {
    // Exact match for document queries.
    return path_ == doc_path;
  } else {
    // Shallow ancestor queries by default.
    return path_.IsImmediateParentOf(doc_path);
  }
}

bool Query::MatchesFilters(const Document& doc) const {
  for (const auto& filter : filters_) {
    if (!filter->Matches(doc)) return false;
  }
  return true;
}

bool Query::MatchesOrderBy(const Document& doc) const {
  for (const OrderBy& order_by : explicit_order_bys_) {
    const FieldPath& field_path = order_by.field();
    // order by key always matches
    if (field_path != FieldPath::KeyFieldPath() &&
        doc.field(field_path) == absl::nullopt) {
      return false;
    }
  }
  return true;
}

bool Query::MatchesBounds(const Document& doc) const {
  const OrderByList& ordering = order_bys();
  if (start_at_ && !start_at_->SortsBeforeDocument(ordering, doc)) {
    return false;
  }
  if (end_at_ && end_at_->SortsBeforeDocument(ordering, doc)) {
    return false;
  }
  return true;
}

bool operator==(const Query& lhs, const Query& rhs) {
  return lhs.path() == rhs.path() &&
         util::Equals(lhs.collection_group(), rhs.collection_group()) &&
         absl::c_equal(lhs.filters(), rhs.filters(),
                       util::Equals<std::shared_ptr<const Filter>>) &&
         lhs.order_bys() == rhs.order_bys() && lhs.limit() == rhs.limit() &&
         util::Equals(lhs.start_at(), rhs.start_at()) &&
         util::Equals(lhs.end_at(), rhs.end_at());
}

}  // namespace core
}  // namespace firestore
}  // namespace firebase
