// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/utils-inl.h"

namespace v8 {
namespace internal {

static void CheckInvariantsOfAbortedPage(Page* page) {
  // Check invariants:
  // 1) Markbits are cleared
  // 2) The page is not marked as evacuation candidate anymore
  // 3) The page is not marked as aborted compaction anymore.
  CHECK(page->markbits()->IsClean());
  CHECK(!page->IsEvacuationCandidate());
  CHECK(!page->IsFlagSet(Page::COMPACTION_WAS_ABORTED));
}


HEAP_TEST(CompactionFullAbortedPage) {
  // Test the scenario where we reach OOM during compaction and the whole page
  // is aborted.

  // Disable concurrent sweeping to ensure memory is in an expected state, i.e.,
  // we can reach the state of a half aborted page.
  FLAG_concurrent_sweeping = false;
  FLAG_manual_evacuation_candidates_selection = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  {
    HandleScope scope1(isolate);
    PageIterator it(heap->old_space());
    while (it.has_next()) {
      it.next()->SetFlag(Page::NEVER_ALLOCATE_ON_PAGE);
    }

    {
      HandleScope scope2(isolate);
      CHECK(heap->old_space()->Expand());
      auto compaction_page_handles =
          CreatePadding(heap, Page::kAllocatableMemory, TENURED);
      Page* to_be_aborted_page =
          Page::FromAddress(compaction_page_handles.front()->address());
      to_be_aborted_page->SetFlag(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);

      heap->set_force_oom(true);
      heap->CollectAllGarbage();

      // Check that all handles still point to the same page, i.e., compaction
      // has been aborted on the page.
      for (Handle<FixedArray> object : compaction_page_handles) {
        CHECK_EQ(to_be_aborted_page, Page::FromAddress(object->address()));
      }
      CheckInvariantsOfAbortedPage(to_be_aborted_page);
    }
  }
}


HEAP_TEST(CompactionPartiallyAbortedPage) {
  // Test the scenario where we reach OOM during compaction and parts of the
  // page have already been migrated to a new one.

  // Disable concurrent sweeping to ensure memory is in an expected state, i.e.,
  // we can reach the state of a half aborted page.
  FLAG_concurrent_sweeping = false;
  FLAG_manual_evacuation_candidates_selection = true;

  const int object_size = 128 * KB;

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  {
    HandleScope scope1(isolate);
    PageIterator it(heap->old_space());
    while (it.has_next()) {
      it.next()->SetFlag(Page::NEVER_ALLOCATE_ON_PAGE);
    }

    {
      HandleScope scope2(isolate);
      // Fill another page with objects of size {object_size} (last one is
      // properly adjusted).
      CHECK(heap->old_space()->Expand());
      auto compaction_page_handles =
          CreatePadding(heap, Page::kAllocatableMemory, TENURED, object_size);
      Page* to_be_aborted_page =
          Page::FromAddress(compaction_page_handles.front()->address());
      to_be_aborted_page->SetFlag(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);

      {
        // Add another page that is filled with {num_objects} objects of size
        // {object_size}.
        HandleScope scope3(isolate);
        CHECK(heap->old_space()->Expand());
        const int num_objects = 3;
        std::vector<Handle<FixedArray>> page_to_fill_handles = CreatePadding(
            heap, object_size * num_objects, TENURED, object_size);
        Page* page_to_fill =
            Page::FromAddress(page_to_fill_handles.front()->address());

        heap->set_force_oom(true);
        heap->CollectAllGarbage();

        bool migration_aborted = false;
        for (Handle<FixedArray> object : compaction_page_handles) {
          // Once compaction has been aborted, all following objects still have
          // to be on the initial page.
          CHECK(!migration_aborted ||
                (Page::FromAddress(object->address()) == to_be_aborted_page));
          if (Page::FromAddress(object->address()) == to_be_aborted_page) {
            // This object has not been migrated.
            migration_aborted = true;
          } else {
            CHECK_EQ(Page::FromAddress(object->address()), page_to_fill);
          }
        }
        // Check that we actually created a scenario with a partially aborted
        // page.
        CHECK(migration_aborted);
        CheckInvariantsOfAbortedPage(to_be_aborted_page);
      }
    }
  }
}


HEAP_TEST(CompactionPartiallyAbortedPageIntraAbortedPointers) {
  // Test the scenario where we reach OOM during compaction and parts of the
  // page have already been migrated to a new one. Objects on the aborted page
  // are linked together. This test makes sure that intra-aborted page pointers
  // get properly updated.

  // Disable concurrent sweeping to ensure memory is in an expected state, i.e.,
  // we can reach the state of a half aborted page.
  FLAG_concurrent_sweeping = false;
  FLAG_manual_evacuation_candidates_selection = true;

  const int object_size = 128 * KB;

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  {
    HandleScope scope1(isolate);
    Handle<FixedArray> root_array =
        isolate->factory()->NewFixedArray(10, TENURED);

    PageIterator it(heap->old_space());
    while (it.has_next()) {
      it.next()->SetFlag(Page::NEVER_ALLOCATE_ON_PAGE);
    }

    Page* to_be_aborted_page = nullptr;
    {
      HandleScope temporary_scope(isolate);
      // Fill a fresh page with objects of size {object_size} (last one is
      // properly adjusted).
      CHECK(heap->old_space()->Expand());
      std::vector<Handle<FixedArray>> compaction_page_handles =
          CreatePadding(heap, Page::kAllocatableMemory, TENURED, object_size);
      to_be_aborted_page =
          Page::FromAddress(compaction_page_handles.front()->address());
      to_be_aborted_page->SetFlag(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
      for (size_t i = compaction_page_handles.size() - 1; i > 0; i--) {
        compaction_page_handles[i]->set(0, *compaction_page_handles[i - 1]);
      }
      root_array->set(0, *compaction_page_handles.back());
    }

    {
      // Add another page that is filled with {num_objects} objects of size
      // {object_size}.
      HandleScope scope3(isolate);
      CHECK(heap->old_space()->Expand());
      const int num_objects = 2;
      int used_memory = object_size * num_objects;
      std::vector<Handle<FixedArray>> page_to_fill_handles =
          CreatePadding(heap, used_memory, TENURED, object_size);
      Page* page_to_fill =
          Page::FromAddress(page_to_fill_handles.front()->address());

      heap->set_force_oom(true);
      heap->CollectAllGarbage();

      // The following check makes sure that we compacted "some" objects, while
      // leaving others in place.
      bool in_place = true;
      Handle<FixedArray> current = root_array;
      while (current->get(0) != heap->undefined_value()) {
        current = Handle<FixedArray>(FixedArray::cast(current->get(0)));
        CHECK(current->IsFixedArray());
        if (Page::FromAddress(current->address()) != to_be_aborted_page) {
          in_place = false;
        }
        bool on_aborted_page =
            Page::FromAddress(current->address()) == to_be_aborted_page;
        bool on_fill_page =
            Page::FromAddress(current->address()) == page_to_fill;
        CHECK((in_place && on_aborted_page) || (!in_place && on_fill_page));
      }
      // Check that we at least migrated one object, as otherwise the test would
      // not trigger.
      CHECK(!in_place);
      CheckInvariantsOfAbortedPage(to_be_aborted_page);
    }
  }
}


HEAP_TEST(CompactionPartiallyAbortedPageWithStoreBufferEntries) {
  // Test the scenario where we reach OOM during compaction and parts of the
  // page have already been migrated to a new one. Objects on the aborted page
  // are linked together and the very first object on the aborted page points
  // into new space. The test verifies that the store buffer entries are
  // properly cleared and rebuilt after aborting a page. Failing to do so can
  // result in other objects being allocated in the free space where their
  // payload looks like a valid new space pointer.

  // Disable concurrent sweeping to ensure memory is in an expected state, i.e.,
  // we can reach the state of a half aborted page.
  FLAG_concurrent_sweeping = false;
  FLAG_manual_evacuation_candidates_selection = true;

  const int object_size = 128 * KB;

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  {
    HandleScope scope1(isolate);
    Handle<FixedArray> root_array =
        isolate->factory()->NewFixedArray(10, TENURED);
    PageIterator it(heap->old_space());
    while (it.has_next()) {
      it.next()->SetFlag(Page::NEVER_ALLOCATE_ON_PAGE);
    }

    Page* to_be_aborted_page = nullptr;
    {
      HandleScope temporary_scope(isolate);
      // Fill another page with objects of size {object_size} (last one is
      // properly adjusted).
      CHECK(heap->old_space()->Expand());
      auto compaction_page_handles =
          CreatePadding(heap, Page::kAllocatableMemory, TENURED, object_size);
      // Sanity check that we have enough space for linking up arrays.
      CHECK_GE(compaction_page_handles.front()->length(), 2);
      to_be_aborted_page =
          Page::FromAddress(compaction_page_handles.front()->address());
      to_be_aborted_page->SetFlag(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);

      for (size_t i = compaction_page_handles.size() - 1; i > 0; i--) {
        compaction_page_handles[i]->set(0, *compaction_page_handles[i - 1]);
      }
      root_array->set(0, *compaction_page_handles.back());
      Handle<FixedArray> new_space_array =
          isolate->factory()->NewFixedArray(1, NOT_TENURED);
      CHECK(heap->InNewSpace(*new_space_array));
      compaction_page_handles.front()->set(1, *new_space_array);
    }

    {
      // Add another page that is filled with {num_objects} objects of size
      // {object_size}.
      HandleScope scope3(isolate);
      CHECK(heap->old_space()->Expand());
      const int num_objects = 2;
      int used_memory = object_size * num_objects;
      std::vector<Handle<FixedArray>> page_to_fill_handles =
          CreatePadding(heap, used_memory, TENURED, object_size);
      Page* page_to_fill =
          Page::FromAddress(page_to_fill_handles.front()->address());

      heap->set_force_oom(true);
      heap->CollectAllGarbage();

      // The following check makes sure that we compacted "some" objects, while
      // leaving others in place.
      bool in_place = true;
      Handle<FixedArray> current = root_array;
      while (current->get(0) != heap->undefined_value()) {
        current = Handle<FixedArray>(FixedArray::cast(current->get(0)));
        CHECK(!heap->InNewSpace(*current));
        CHECK(current->IsFixedArray());
        if (Page::FromAddress(current->address()) != to_be_aborted_page) {
          in_place = false;
        }
        bool on_aborted_page =
            Page::FromAddress(current->address()) == to_be_aborted_page;
        bool on_fill_page =
            Page::FromAddress(current->address()) == page_to_fill;
        CHECK((in_place && on_aborted_page) || (!in_place && on_fill_page));
      }
      // Check that we at least migrated one object, as otherwise the test would
      // not trigger.
      CHECK(!in_place);
      CheckInvariantsOfAbortedPage(to_be_aborted_page);

      // Allocate a new object in new space.
      Handle<FixedArray> holder =
          isolate->factory()->NewFixedArray(10, NOT_TENURED);
      // Create a broken address that looks like a tagged pointer to a new space
      // object.
      Address broken_address = holder->address() + 2 * kPointerSize + 1;
      // Convert it to a vector to create a string from it.
      Vector<const uint8_t> string_to_broken_addresss(
          reinterpret_cast<const uint8_t*>(&broken_address), 8);

      Handle<String> string;
      do {
        // We know that the interesting slot will be on the aborted page and
        // hence we allocate until we get our string on the aborted page.
        // We used slot 1 in the fixed size array which corresponds to the
        // the first word in the string. Since the first object definitely
        // migrated we can just allocate until we hit the aborted page.
        string = isolate->factory()
                     ->NewStringFromOneByte(string_to_broken_addresss, TENURED)
                     .ToHandleChecked();
      } while (Page::FromAddress(string->address()) != to_be_aborted_page);

      // If store buffer entries are not properly filtered/reset for aborted
      // pages we have now a broken address at an object slot in old space and
      // the following scavenge will crash.
      heap->CollectGarbage(NEW_SPACE);
    }
  }
}

}  // namespace internal
}  // namespace v8
