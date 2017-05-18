/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDOMMutationObserver.h"

#include "mozilla/AnimationTarget.h"
#include "mozilla/Maybe.h"
#include "mozilla/OwningNonNull.h"

#include "mozilla/dom/Animation.h"
#include "mozilla/dom/KeyframeEffectReadOnly.h"

#include "nsContentUtils.h"
#include "nsCSSPseudoElements.h"
#include "nsError.h"
#include "nsIDOMMutationEvent.h"
#include "nsIScriptGlobalObject.h"
#include "nsServiceManagerUtils.h"
#include "nsTextFragment.h"
#include "nsThreadUtils.h"

using mozilla::Maybe;
using mozilla::Move;
using mozilla::NonOwningAnimationTarget;
using mozilla::dom::TreeOrderComparator;
using mozilla::dom::Animation;
using mozilla::dom::Element;

AutoTArray<RefPtr<nsDOMMutationObserver>, 4>*
  nsDOMMutationObserver::sScheduledMutationObservers = nullptr;

nsDOMMutationObserver* nsDOMMutationObserver::sCurrentObserver = nullptr;

uint32_t nsDOMMutationObserver::sMutationLevel = 0;
uint64_t nsDOMMutationObserver::sCount = 0;

AutoTArray<AutoTArray<RefPtr<nsDOMMutationObserver>, 4>, 4>*
nsDOMMutationObserver::sCurrentlyHandlingObservers = nullptr;

nsINodeList*
nsDOMMutationRecord::AddedNodes()
{
  if (!mAddedNodes) {
    mAddedNodes = new nsSimpleContentList(mTarget);
  }
  return mAddedNodes;
}

nsINodeList*
nsDOMMutationRecord::RemovedNodes()
{
  if (!mRemovedNodes) {
    mRemovedNodes = new nsSimpleContentList(mTarget);
  }
  return mRemovedNodes;
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDOMMutationRecord)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMMutationRecord)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMMutationRecord)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(nsDOMMutationRecord,
                                      mTarget,
                                      mPreviousSibling, mNextSibling,
                                      mAddedNodes, mRemovedNodes,
                                      mAddedAnimations, mRemovedAnimations,
                                      mChangedAnimations,
                                      mNext, mOwner)

// Observer

bool
nsMutationReceiverBase::IsObservable(nsIContent* aContent)
{
  return !aContent->ChromeOnlyAccess() &&
    (Observer()->IsChrome() || !aContent->IsInAnonymousSubtree());
}

NS_IMPL_ADDREF(nsMutationReceiver)
NS_IMPL_RELEASE(nsMutationReceiver)

NS_INTERFACE_MAP_BEGIN(nsMutationReceiver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
NS_INTERFACE_MAP_END

nsMutationReceiver::nsMutationReceiver(nsINode* aTarget,
                                       nsDOMMutationObserver* aObserver)
: nsMutationReceiverBase(aTarget, aObserver)
{
  mTarget->BindObject(aObserver);
}

void
nsMutationReceiver::Disconnect(bool aRemoveFromObserver)
{
  if (mRegisterTarget) {
    mRegisterTarget->RemoveMutationObserver(this);
    mRegisterTarget = nullptr;
  }

  mParent = nullptr;
  nsINode* target = mTarget;
  mTarget = nullptr;
  nsDOMMutationObserver* observer = mObserver;
  mObserver = nullptr;
  RemoveClones();

  if (target && observer) {
    if (aRemoveFromObserver) {
      static_cast<nsDOMMutationObserver*>(observer)->RemoveReceiver(this);
    }
    // UnbindObject may delete 'this'!
    target->UnbindObject(observer);
  }
}

void
nsMutationReceiver::NativeAnonymousChildListChange(nsIDocument* aDocument,
                                                   nsIContent* aContent,
                                                   bool aIsRemove) {
  if (!NativeAnonymousChildList()) {
    return;
  }

  nsINode* parent = aContent->GetParentNode();
  if (!parent ||
      (!Subtree() && Target() != parent) ||
      (Subtree() && RegisterTarget()->SubtreeRoot() != parent->SubtreeRoot())) {
    return;
  }

  nsDOMMutationRecord* m =
    Observer()->CurrentRecord(nsGkAtoms::nativeAnonymousChildList);

  if (m->mTarget) {
    return;
  }
  m->mTarget = parent;

  if (aIsRemove) {
    m->mRemovedNodes = new nsSimpleContentList(parent);
    m->mRemovedNodes->AppendElement(aContent);
  } else {
    m->mAddedNodes = new nsSimpleContentList(parent);
    m->mAddedNodes->AppendElement(aContent);
  }
}

void
nsMutationReceiver::AttributeWillChange(nsIDocument* aDocument,
                                        mozilla::dom::Element* aElement,
                                        int32_t aNameSpaceID,
                                        nsIAtom* aAttribute,
                                        int32_t aModType,
                                        const nsAttrValue* aNewValue)
{
  if (nsAutoMutationBatch::IsBatching() ||
      !ObservesAttr(RegisterTarget(), aElement, aNameSpaceID, aAttribute)) {
    return;
  }

  nsDOMMutationRecord* m =
    Observer()->CurrentRecord(nsGkAtoms::attributes);

  NS_ASSERTION(!m->mTarget || m->mTarget == aElement,
               "Wrong target!");
  NS_ASSERTION(!m->mAttrName || m->mAttrName == aAttribute,
               "Wrong attribute!");
  if (!m->mTarget) {
    m->mTarget = aElement;
    m->mAttrName = aAttribute;
    if (aNameSpaceID == kNameSpaceID_None) {
      m->mAttrNamespace.SetIsVoid(true);
    } else {
      nsContentUtils::NameSpaceManager()->GetNameSpaceURI(aNameSpaceID,
                                                          m->mAttrNamespace);
    }
  }

  if (AttributeOldValue() && m->mPrevValue.IsVoid()) {
    if (!aElement->GetAttr(aNameSpaceID, aAttribute, m->mPrevValue)) {
      m->mPrevValue.SetIsVoid(true);
    }
  }
}

void
nsMutationReceiver::CharacterDataWillChange(nsIDocument *aDocument,
                                            nsIContent* aContent,
                                            CharacterDataChangeInfo* aInfo)
{
  if (nsAutoMutationBatch::IsBatching() ||
      !CharacterData() ||
      (!Subtree() && aContent != Target()) ||
      (Subtree() && RegisterTarget()->SubtreeRoot() != aContent->SubtreeRoot()) ||
      !IsObservable(aContent)) {
    return;
  }

  nsDOMMutationRecord* m =
    Observer()->CurrentRecord(nsGkAtoms::characterData);

  NS_ASSERTION(!m->mTarget || m->mTarget == aContent,
               "Wrong target!");

  if (!m->mTarget) {
    m->mTarget = aContent;
  }
  if (CharacterDataOldValue() && m->mPrevValue.IsVoid()) { 
    aContent->GetText()->AppendTo(m->mPrevValue);
  }
}

void
nsMutationReceiver::ContentAppended(nsIDocument* aDocument,
                                    nsIContent* aContainer,
                                    nsIContent* aFirstNewContent,
                                    int32_t aNewIndexInContainer)
{
  nsINode* parent = NODE_FROM(aContainer, aDocument);
  bool wantsChildList =
    ChildList() &&
    ((Subtree() && RegisterTarget()->SubtreeRoot() == parent->SubtreeRoot()) ||
     parent == Target());
  if (!wantsChildList || !IsObservable(aFirstNewContent)) {
    return;
  }

  if (nsAutoMutationBatch::IsBatching()) {
    if (parent == nsAutoMutationBatch::GetBatchTarget()) {
      nsAutoMutationBatch::UpdateObserver(Observer(), wantsChildList);
    }
    return;
  }

  nsDOMMutationRecord* m =
    Observer()->CurrentRecord(nsGkAtoms::childList);
  NS_ASSERTION(!m->mTarget || m->mTarget == parent,
               "Wrong target!");
  if (m->mTarget) {
    // Already handled case.
    return;
  }
  m->mTarget = parent;
  m->mAddedNodes = new nsSimpleContentList(parent);

  nsINode* n = aFirstNewContent;
  while (n) {
    m->mAddedNodes->AppendElement(static_cast<nsIContent*>(n));
    n = n->GetNextSibling();
  }
  m->mPreviousSibling = aFirstNewContent->GetPreviousSibling();
}

void
nsMutationReceiver::ContentInserted(nsIDocument* aDocument,
                                    nsIContent* aContainer,
                                    nsIContent* aChild,
                                    int32_t aIndexInContainer)
{
  nsINode* parent = NODE_FROM(aContainer, aDocument);
  bool wantsChildList =
    ChildList() &&
    ((Subtree() && RegisterTarget()->SubtreeRoot() == parent->SubtreeRoot()) ||
     parent == Target());
  if (!wantsChildList || !IsObservable(aChild)) {
    return;
  }

  if (nsAutoMutationBatch::IsBatching()) {
    if (parent == nsAutoMutationBatch::GetBatchTarget()) {
      nsAutoMutationBatch::UpdateObserver(Observer(), wantsChildList);
    }
    return;
  }

  nsDOMMutationRecord* m =
    Observer()->CurrentRecord(nsGkAtoms::childList);
  if (m->mTarget) {
    // Already handled case.
    return;  
  }
  m->mTarget = parent;
  m->mAddedNodes = new nsSimpleContentList(parent);
  m->mAddedNodes->AppendElement(aChild);
  m->mPreviousSibling = aChild->GetPreviousSibling();
  m->mNextSibling = aChild->GetNextSibling();
}

void
nsMutationReceiver::ContentRemoved(nsIDocument* aDocument,
                                   nsIContent* aContainer,
                                   nsIContent* aChild,
                                   int32_t aIndexInContainer,
                                   nsIContent* aPreviousSibling)
{
  if (!IsObservable(aChild)) {
    return;
  }

  nsINode* parent = NODE_FROM(aContainer, aDocument);
  if (Subtree() && parent->SubtreeRoot() != RegisterTarget()->SubtreeRoot()) {
    return;
  }
  if (nsAutoMutationBatch::IsBatching()) {
    if (nsAutoMutationBatch::IsRemovalDone()) {
      // This can happen for example if HTML parser parses to
      // context node, but needs to move elements around.
      return;
    }
    if (nsAutoMutationBatch::GetBatchTarget() != parent) {
      return;
    }

    bool wantsChildList = ChildList() && (Subtree() || parent == Target());
    if (wantsChildList || Subtree()) {
      nsAutoMutationBatch::NodeRemoved(aChild);
      nsAutoMutationBatch::UpdateObserver(Observer(), wantsChildList);
    }

    return;
  }

  if (Subtree()) {
    // Try to avoid creating transient observer if the node
    // already has an observer observing the same set of nodes.
    nsMutationReceiver* orig = GetParent() ? GetParent() : this;
    if (Observer()->GetReceiverFor(aChild, false, false) != orig) {
      bool transientExists = false;
      nsCOMArray<nsMutationReceiver>* transientReceivers = nullptr;
      Observer()->mTransientReceivers.Get(aChild, &transientReceivers);
      if (!transientReceivers) {
        transientReceivers = new nsCOMArray<nsMutationReceiver>();
        Observer()->mTransientReceivers.Put(aChild, transientReceivers);
      } else {
        for (int32_t i = 0; i < transientReceivers->Count(); ++i) {
          nsMutationReceiver* r = transientReceivers->ObjectAt(i);
          if (r->GetParent() == orig) {
            transientExists = true;
          }
        }
      }
      if (!transientExists) {
        // Make sure the elements which are removed from the
        // subtree are kept in the same observation set.
        nsMutationReceiver* tr;
        if (orig->Animations()) {
          tr = nsAnimationReceiver::Create(aChild, orig);
        } else {
          tr = nsMutationReceiver::Create(aChild, orig);
        }
        transientReceivers->AppendObject(tr);
      }
    }
  }

  if (ChildList() && (Subtree() || parent == Target())) {
    nsDOMMutationRecord* m =
      Observer()->CurrentRecord(nsGkAtoms::childList);
    if (m->mTarget) {
      // Already handled case.
      return;
    }
    m->mTarget = parent;
    m->mRemovedNodes = new nsSimpleContentList(parent);
    m->mRemovedNodes->AppendElement(aChild);
    m->mPreviousSibling = aPreviousSibling;
    m->mNextSibling = parent->GetChildAt(aIndexInContainer);
  }
  // We need to schedule always, so that after microtask mTransientReceivers
  // can be cleared correctly.
  Observer()->ScheduleForRun();
}

void nsMutationReceiver::NodeWillBeDestroyed(const nsINode *aNode)
{
  NS_ASSERTION(!mParent, "Shouldn't have mParent here!");
  Disconnect(true);
}

void
nsAnimationReceiver::RecordAnimationMutation(Animation* aAnimation,
                                             AnimationMutation aMutationType)
{
  mozilla::dom::AnimationEffectReadOnly* effect = aAnimation->GetEffect();
  if (!effect) {
    return;
  }

  mozilla::dom::KeyframeEffectReadOnly* keyframeEffect =
    effect->AsKeyframeEffect();
  if (!keyframeEffect) {
    return;
  }

  Maybe<NonOwningAnimationTarget> animationTarget = keyframeEffect->GetTarget();
  if (!animationTarget) {
    return;
  }

  Element* elem = animationTarget->mElement;
  if (!Animations() || !(Subtree() || elem == Target()) ||
      elem->ChromeOnlyAccess()) {
    return;
  }

  // Record animations targeting to a pseudo element only when subtree is true.
  if (animationTarget->mPseudoType != mozilla::CSSPseudoElementType::NotPseudo &&
      !Subtree()) {
    return;
  }

  if (nsAutoAnimationMutationBatch::IsBatching()) {
    switch (aMutationType) {
      case eAnimationMutation_Added:
        nsAutoAnimationMutationBatch::AnimationAdded(aAnimation, elem);
        break;
      case eAnimationMutation_Changed:
        nsAutoAnimationMutationBatch::AnimationChanged(aAnimation, elem);
        break;
      case eAnimationMutation_Removed:
        nsAutoAnimationMutationBatch::AnimationRemoved(aAnimation, elem);
        break;
    }

    nsAutoAnimationMutationBatch::AddObserver(Observer());
    return;
  }

  nsDOMMutationRecord* m =
    Observer()->CurrentRecord(nsGkAtoms::animations);

  NS_ASSERTION(!m->mTarget, "Wrong target!");

  m->mTarget = elem;

  switch (aMutationType) {
    case eAnimationMutation_Added:
      m->mAddedAnimations.AppendElement(aAnimation);
      break;
    case eAnimationMutation_Changed:
      m->mChangedAnimations.AppendElement(aAnimation);
      break;
    case eAnimationMutation_Removed:
      m->mRemovedAnimations.AppendElement(aAnimation);
      break;
  }
}

void
nsAnimationReceiver::AnimationAdded(Animation* aAnimation)
{
  RecordAnimationMutation(aAnimation, eAnimationMutation_Added);
}

void
nsAnimationReceiver::AnimationChanged(Animation* aAnimation)
{
  RecordAnimationMutation(aAnimation, eAnimationMutation_Changed);
}

void
nsAnimationReceiver::AnimationRemoved(Animation* aAnimation)
{
  RecordAnimationMutation(aAnimation, eAnimationMutation_Removed);
}

NS_IMPL_ISUPPORTS_INHERITED(nsAnimationReceiver, nsMutationReceiver,
                            nsIAnimationObserver)

// Observer

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDOMMutationObserver)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsDOMMutationObserver)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMMutationObserver)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMMutationObserver)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDOMMutationObserver)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsDOMMutationObserver)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsDOMMutationObserver)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner)
  for (int32_t i = 0; i < tmp->mReceivers.Count(); ++i) {
    tmp->mReceivers[i]->Disconnect(false);
  }
  tmp->mReceivers.Clear();
  tmp->ClearPendingRecords();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCallback)
  // No need to handle mTransientReceivers
  NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsDOMMutationObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReceivers)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFirstPendingMutation)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCallback)
  // No need to handle mTransientReceivers
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

nsMutationReceiver*
nsDOMMutationObserver::GetReceiverFor(nsINode* aNode, bool aMayCreate,
                                      bool aWantsAnimations)
{
  MOZ_ASSERT(aMayCreate || !aWantsAnimations,
             "the value of aWantsAnimations doesn't matter when aMayCreate is "
             "false, so just pass in false for it");

  if (!aMayCreate && !aNode->MayHaveDOMMutationObserver()) {
    return nullptr;
  }

  for (int32_t i = 0; i < mReceivers.Count(); ++i) {
    if (mReceivers[i]->Target() == aNode) {
      return mReceivers[i];
    }
  }
  if (!aMayCreate) {
    return nullptr;
  }

  nsMutationReceiver* r;
  if (aWantsAnimations) {
    r = nsAnimationReceiver::Create(aNode, this);
  } else {
    r = nsMutationReceiver::Create(aNode, this);
  }
  mReceivers.AppendObject(r);
  return r;
}

void
nsDOMMutationObserver::RemoveReceiver(nsMutationReceiver* aReceiver)
{
  mReceivers.RemoveObject(aReceiver);
}

void
nsDOMMutationObserver::GetAllSubtreeObserversFor(nsINode* aNode,
                                                 nsTArray<nsMutationReceiver*>&
                                                   aReceivers)
{
  nsINode* n = aNode;
  while (n) {
    if (n->MayHaveDOMMutationObserver()) {
      nsMutationReceiver* r = GetReceiverFor(n, false, false);
      if (r && r->Subtree() && !aReceivers.Contains(r)) {
        aReceivers.AppendElement(r);
        // If we've found all the receivers the observer has,
        // no need to search for more.
        if (mReceivers.Count() == int32_t(aReceivers.Length())) {
          return;
        }
      }                                            
      nsCOMArray<nsMutationReceiver>* transientReceivers = nullptr;
      if (mTransientReceivers.Get(n, &transientReceivers) && transientReceivers) {
        for (int32_t i = 0; i < transientReceivers->Count(); ++i) {
          nsMutationReceiver* r = transientReceivers->ObjectAt(i);
          nsMutationReceiver* parent = r->GetParent();
          if (r->Subtree() && parent && !aReceivers.Contains(parent)) {
            aReceivers.AppendElement(parent);
          }
        }
        if (mReceivers.Count() == int32_t(aReceivers.Length())) {
          return;
        }
      }
    }
    n = n->GetParentNode();
  }
}

void
nsDOMMutationObserver::ScheduleForRun()
{
  nsDOMMutationObserver::AddCurrentlyHandlingObserver(this, sMutationLevel);

  if (mWaitingForRun) {
    return;
  }
  mWaitingForRun = true;
  RescheduleForRun();
}

void
nsDOMMutationObserver::RescheduleForRun()
{
  if (!sScheduledMutationObservers) {
    sScheduledMutationObservers = new AutoTArray<RefPtr<nsDOMMutationObserver>, 4>;
  }

  bool didInsert = false;
  for (uint32_t i = 0; i < sScheduledMutationObservers->Length(); ++i) {
    if (static_cast<nsDOMMutationObserver*>((*sScheduledMutationObservers)[i])
          ->mId > mId) {
      sScheduledMutationObservers->InsertElementAt(i, this);
      didInsert = true;
      break;
    }
  }
  if (!didInsert) {
    sScheduledMutationObservers->AppendElement(this);
  }
}

void
nsDOMMutationObserver::Observe(nsINode& aTarget,
                               const mozilla::dom::MutationObserverInit& aOptions,
                               mozilla::ErrorResult& aRv)
{

  bool childList = aOptions.mChildList;
  bool attributes =
    aOptions.mAttributes.WasPassed() &&
    aOptions.mAttributes.Value();
  bool characterData =
    aOptions.mCharacterData.WasPassed() &&
    aOptions.mCharacterData.Value();
  bool subtree = aOptions.mSubtree;
  bool attributeOldValue =
    aOptions.mAttributeOldValue.WasPassed() &&
    aOptions.mAttributeOldValue.Value();
  bool nativeAnonymousChildList = aOptions.mNativeAnonymousChildList;
  bool characterDataOldValue =
    aOptions.mCharacterDataOldValue.WasPassed() &&
    aOptions.mCharacterDataOldValue.Value();
  bool animations = aOptions.mAnimations;

  if (!aOptions.mAttributes.WasPassed() &&
      (aOptions.mAttributeOldValue.WasPassed() ||
       aOptions.mAttributeFilter.WasPassed())) {
    attributes = true;
  }

  if (!aOptions.mCharacterData.WasPassed() &&
      aOptions.mCharacterDataOldValue.WasPassed()) {
    characterData = true;
  }

  if (!(childList || attributes || characterData || animations ||
        nativeAnonymousChildList)) {
    aRv.Throw(NS_ERROR_DOM_TYPE_ERR);
    return;
  }

  if (aOptions.mAttributeOldValue.WasPassed() &&
      aOptions.mAttributeOldValue.Value() &&
      aOptions.mAttributes.WasPassed() &&
      !aOptions.mAttributes.Value()) {
    aRv.Throw(NS_ERROR_DOM_TYPE_ERR);
    return;
  }

  if (aOptions.mAttributeFilter.WasPassed() &&
      aOptions.mAttributes.WasPassed() &&
      !aOptions.mAttributes.Value()) {
    aRv.Throw(NS_ERROR_DOM_TYPE_ERR);
    return;
  }

  if (aOptions.mCharacterDataOldValue.WasPassed() &&
      aOptions.mCharacterDataOldValue.Value() &&
      aOptions.mCharacterData.WasPassed() &&
      !aOptions.mCharacterData.Value()) {
    aRv.Throw(NS_ERROR_DOM_TYPE_ERR);
    return;
  }

  nsCOMArray<nsIAtom> filters;
  bool allAttrs = true;
  if (aOptions.mAttributeFilter.WasPassed()) {
    allAttrs = false;
    const mozilla::dom::Sequence<nsString>& filtersAsString =
      aOptions.mAttributeFilter.Value();
    uint32_t len = filtersAsString.Length();
    filters.SetCapacity(len);

    for (uint32_t i = 0; i < len; ++i) {
      filters.AppendElement(NS_Atomize(filtersAsString[i]));
    }
  }

  nsMutationReceiver* r = GetReceiverFor(&aTarget, true, animations);
  r->SetChildList(childList);
  r->SetAttributes(attributes);
  r->SetCharacterData(characterData);
  r->SetSubtree(subtree);
  r->SetAttributeOldValue(attributeOldValue);
  r->SetCharacterDataOldValue(characterDataOldValue);
  r->SetNativeAnonymousChildList(nativeAnonymousChildList);
  r->SetAttributeFilter(Move(filters));
  r->SetAllAttributes(allAttrs);
  r->SetAnimations(animations);
  r->RemoveClones();

#ifdef DEBUG
  for (int32_t i = 0; i < mReceivers.Count(); ++i) {
    NS_WARNING_ASSERTION(mReceivers[i]->Target(),
                         "All the receivers should have a target!");
  }
#endif
}

void
nsDOMMutationObserver::Disconnect()
{
  for (int32_t i = 0; i < mReceivers.Count(); ++i) {
    mReceivers[i]->Disconnect(false);
  }
  mReceivers.Clear();
  mCurrentMutations.Clear();
  ClearPendingRecords();
}

void
nsDOMMutationObserver::TakeRecords(
                         nsTArray<RefPtr<nsDOMMutationRecord> >& aRetVal)
{
  aRetVal.Clear();
  aRetVal.SetCapacity(mPendingMutationCount);
  RefPtr<nsDOMMutationRecord> current;
  current.swap(mFirstPendingMutation);
  for (uint32_t i = 0; i < mPendingMutationCount; ++i) {
    RefPtr<nsDOMMutationRecord> next;
    current->mNext.swap(next);
    if (!mMergeAttributeRecords ||
        !MergeableAttributeRecord(aRetVal.SafeLastElement(nullptr),
                                  current)) {
      *aRetVal.AppendElement() = current.forget();
    }
    current.swap(next);
  }
  ClearPendingRecords();
}

void
nsDOMMutationObserver::GetObservingInfo(
                         nsTArray<Nullable<MutationObservingInfo>>& aResult,
                         mozilla::ErrorResult& aRv)
{
  aResult.SetCapacity(mReceivers.Count());
  for (int32_t i = 0; i < mReceivers.Count(); ++i) {
    MutationObservingInfo& info = aResult.AppendElement()->SetValue();
    nsMutationReceiver* mr = mReceivers[i];
    info.mChildList = mr->ChildList();
    info.mAttributes.Construct(mr->Attributes());
    info.mCharacterData.Construct(mr->CharacterData());
    info.mSubtree = mr->Subtree();
    info.mAttributeOldValue.Construct(mr->AttributeOldValue());
    info.mCharacterDataOldValue.Construct(mr->CharacterDataOldValue());
    info.mNativeAnonymousChildList = mr->NativeAnonymousChildList();
    info.mAnimations = mr->Animations();
    nsCOMArray<nsIAtom>& filters = mr->AttributeFilter();
    if (filters.Count()) {
      info.mAttributeFilter.Construct();
      mozilla::dom::Sequence<nsString>& filtersAsStrings =
        info.mAttributeFilter.Value();
      nsString* strings = filtersAsStrings.AppendElements(filters.Count(),
                                                          mozilla::fallible);
      if (!strings) {
        aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
      for (int32_t j = 0; j < filters.Count(); ++j) {
        filters[j]->ToString(strings[j]);
      }
    }
    info.mObservedNode = mr->Target();
  }
}

// static
already_AddRefed<nsDOMMutationObserver>
nsDOMMutationObserver::Constructor(const mozilla::dom::GlobalObject& aGlobal,
                                   mozilla::dom::MutationCallback& aCb,
                                   mozilla::ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  MOZ_ASSERT(window->IsInnerWindow());
  bool isChrome = nsContentUtils::IsChromeDoc(window->GetExtantDoc());
  RefPtr<nsDOMMutationObserver> observer =
    new nsDOMMutationObserver(window.forget(), aCb, isChrome);
  return observer.forget();
}


bool
nsDOMMutationObserver::MergeableAttributeRecord(nsDOMMutationRecord* aOldRecord,
                                                nsDOMMutationRecord* aRecord)
{
  MOZ_ASSERT(mMergeAttributeRecords);
  return
    aOldRecord &&
    aOldRecord->mType == nsGkAtoms::attributes &&
    aOldRecord->mType == aRecord->mType &&
    aOldRecord->mTarget == aRecord->mTarget &&
    aOldRecord->mAttrName == aRecord->mAttrName &&
    aOldRecord->mAttrNamespace.Equals(aRecord->mAttrNamespace);
}

void
nsDOMMutationObserver::HandleMutation()
{
  NS_ASSERTION(nsContentUtils::IsSafeToRunScript(), "Whaat!");
  NS_ASSERTION(mCurrentMutations.IsEmpty(),
               "Still generating MutationRecords?");

  mWaitingForRun = false;

  for (int32_t i = 0; i < mReceivers.Count(); ++i) {
    mReceivers[i]->RemoveClones();
  }
  mTransientReceivers.Clear();

  nsPIDOMWindowOuter* outer = mOwner->GetOuterWindow();
  if (!mPendingMutationCount || !outer ||
      outer->GetCurrentInnerWindow() != mOwner) {
    ClearPendingRecords();
    return;
  }

  mozilla::dom::Sequence<mozilla::OwningNonNull<nsDOMMutationRecord> >
    mutations;
  if (mutations.SetCapacity(mPendingMutationCount, mozilla::fallible)) {
    // We can't use TakeRecords easily here, because it deals with a
    // different type of array, and we want to optimize out any extra copying.
    RefPtr<nsDOMMutationRecord> current;
    current.swap(mFirstPendingMutation);
    for (uint32_t i = 0; i < mPendingMutationCount; ++i) {
      RefPtr<nsDOMMutationRecord> next;
      current->mNext.swap(next);
      if (!mMergeAttributeRecords ||
          !MergeableAttributeRecord(mutations.Length() ?
                                      mutations.LastElement().get() : nullptr,
                                    current)) {
        *mutations.AppendElement(mozilla::fallible) = current;
      }
      current.swap(next);
    }
  }
  ClearPendingRecords();

  mCallback->Call(this, mutations, *this);
}

class AsyncMutationHandler : public mozilla::Runnable
{
public:
  NS_IMETHOD Run() override
  {
    nsDOMMutationObserver::HandleMutations();
    return NS_OK;
  }
};

void
nsDOMMutationObserver::HandleMutationsInternal()
{
  if (!nsContentUtils::IsSafeToRunScript()) {
    nsContentUtils::AddScriptRunner(new AsyncMutationHandler());
    return;
  }
  static RefPtr<nsDOMMutationObserver> sCurrentObserver;
  if (sCurrentObserver && !sCurrentObserver->Suppressed()) {
    // In normal cases sScheduledMutationObservers will be handled
    // after previous mutations are handled. But in case some
    // callback calls a sync API, which spins the eventloop, we need to still
    // process other mutations happening during that sync call.
    // This does *not* catch all cases, but should work for stuff running
    // in separate tabs.
    return;
  }

  mozilla::AutoSlowOperation aso;

  nsTArray<RefPtr<nsDOMMutationObserver> >* suppressedObservers = nullptr;

  while (sScheduledMutationObservers) {
    AutoTArray<RefPtr<nsDOMMutationObserver>, 4>* observers =
      sScheduledMutationObservers;
    sScheduledMutationObservers = nullptr;
    for (uint32_t i = 0; i < observers->Length(); ++i) {
      sCurrentObserver = static_cast<nsDOMMutationObserver*>((*observers)[i]);
      if (!sCurrentObserver->Suppressed()) {
        sCurrentObserver->HandleMutation();
      } else {
        if (!suppressedObservers) {
          suppressedObservers = new nsTArray<RefPtr<nsDOMMutationObserver> >;
        }
        if (!suppressedObservers->Contains(sCurrentObserver)) {
          suppressedObservers->AppendElement(sCurrentObserver);
        }
      }
    }
    delete observers;
    aso.CheckForInterrupt();
  }

  if (suppressedObservers) {
    for (uint32_t i = 0; i < suppressedObservers->Length(); ++i) {
      static_cast<nsDOMMutationObserver*>(suppressedObservers->ElementAt(i))->
        RescheduleForRun();
    }
    delete suppressedObservers;
    suppressedObservers = nullptr;
  }
  sCurrentObserver = nullptr;
}

nsDOMMutationRecord*
nsDOMMutationObserver::CurrentRecord(nsIAtom* aType)
{
  NS_ASSERTION(sMutationLevel > 0, "Unexpected mutation level!");

  while (mCurrentMutations.Length() < sMutationLevel) {
    mCurrentMutations.AppendElement(static_cast<nsDOMMutationRecord*>(nullptr));
  }

  uint32_t last = sMutationLevel - 1;
  if (!mCurrentMutations[last]) {
    RefPtr<nsDOMMutationRecord> r = new nsDOMMutationRecord(aType, GetParentObject());
    mCurrentMutations[last] = r;
    AppendMutationRecord(r.forget());
    ScheduleForRun();
  }

#ifdef DEBUG
  MOZ_ASSERT(sCurrentlyHandlingObservers->Length() == sMutationLevel);
  for (size_t i = 0; i < sCurrentlyHandlingObservers->Length(); ++i) {
    MOZ_ASSERT(sCurrentlyHandlingObservers->ElementAt(i).Contains(this),
               "MutationObserver should be added as an observer of all the "
               "nested mutations!");
  }
#endif

  NS_ASSERTION(mCurrentMutations[last]->mType == aType,
               "Unexpected MutationRecord type!");

  return mCurrentMutations[last];
}

nsDOMMutationObserver::~nsDOMMutationObserver()
{
  for (int32_t i = 0; i < mReceivers.Count(); ++i) {
    mReceivers[i]->RemoveClones();
  }
}

void
nsDOMMutationObserver::EnterMutationHandling()
{
  ++sMutationLevel;
}

// Leave the current mutation level (there can be several levels if in case
// of nested calls to the nsIMutationObserver methods).
// The most recent mutation record is removed from mCurrentMutations, so
// that is doesn't get modified anymore by receivers.
void
nsDOMMutationObserver::LeaveMutationHandling()
{
  if (sCurrentlyHandlingObservers &&
      sCurrentlyHandlingObservers->Length() == sMutationLevel) {
    nsTArray<RefPtr<nsDOMMutationObserver> >& obs =
      sCurrentlyHandlingObservers->ElementAt(sMutationLevel - 1);
    for (uint32_t i = 0; i < obs.Length(); ++i) {
      nsDOMMutationObserver* o =
        static_cast<nsDOMMutationObserver*>(obs[i]);
      if (o->mCurrentMutations.Length() == sMutationLevel) {
        // It is already in pending mutations.
        o->mCurrentMutations.RemoveElementAt(sMutationLevel - 1);
      }
    }
    sCurrentlyHandlingObservers->RemoveElementAt(sMutationLevel - 1);
  }
  --sMutationLevel;
}

void
nsDOMMutationObserver::AddCurrentlyHandlingObserver(nsDOMMutationObserver* aObserver,
                                                    uint32_t aMutationLevel)
{
  NS_ASSERTION(aMutationLevel > 0, "Unexpected mutation level!");

  if (aMutationLevel > 1) {
    // MutationObserver must be in the currently handling observer list
    // in all the nested levels.
    AddCurrentlyHandlingObserver(aObserver, aMutationLevel - 1);
  }

  if (!sCurrentlyHandlingObservers) {
    sCurrentlyHandlingObservers =
      new AutoTArray<AutoTArray<RefPtr<nsDOMMutationObserver>, 4>, 4>;
  }

  while (sCurrentlyHandlingObservers->Length() < aMutationLevel) {
    sCurrentlyHandlingObservers->InsertElementAt(
      sCurrentlyHandlingObservers->Length());
  }

  uint32_t index = aMutationLevel - 1;
  if (!sCurrentlyHandlingObservers->ElementAt(index).Contains(aObserver)) {
    sCurrentlyHandlingObservers->ElementAt(index).AppendElement(aObserver);
  }
}

void
nsDOMMutationObserver::Shutdown()
{
  delete sCurrentlyHandlingObservers;
  sCurrentlyHandlingObservers = nullptr;
  delete sScheduledMutationObservers;
  sScheduledMutationObservers = nullptr;
}

nsAutoMutationBatch*
nsAutoMutationBatch::sCurrentBatch = nullptr;

void
nsAutoMutationBatch::Done()
{
  if (sCurrentBatch != this) {
    return;
  }

  sCurrentBatch = mPreviousBatch;
  if (mObservers.IsEmpty()) {
    nsDOMMutationObserver::LeaveMutationHandling();
    // Nothing to do.
    return;
  }

  uint32_t len = mObservers.Length();
  for (uint32_t i = 0; i < len; ++i) {
    nsDOMMutationObserver* ob = mObservers[i].mObserver;
    bool wantsChildList = mObservers[i].mWantsChildList;

    RefPtr<nsSimpleContentList> removedList;
    if (wantsChildList) {
      removedList = new nsSimpleContentList(mBatchTarget);
    }

    nsTArray<nsMutationReceiver*> allObservers;
    ob->GetAllSubtreeObserversFor(mBatchTarget, allObservers);

    int32_t j = mFromFirstToLast ? 0 : mRemovedNodes.Length() - 1;
    int32_t end = mFromFirstToLast ? mRemovedNodes.Length() : -1;
    for (; j != end; mFromFirstToLast ? ++j : --j) {
      nsCOMPtr<nsIContent> removed = mRemovedNodes[j];
      if (removedList) {
        removedList->AppendElement(removed);
      }

      if (allObservers.Length()) {
        nsCOMArray<nsMutationReceiver>* transientReceivers = nullptr;
        ob->mTransientReceivers.Get(removed, &transientReceivers);
        if (!transientReceivers) {
          transientReceivers = new nsCOMArray<nsMutationReceiver>();
          ob->mTransientReceivers.Put(removed, transientReceivers);
        }
        for (uint32_t k = 0; k < allObservers.Length(); ++k) {
          nsMutationReceiver* r = allObservers[k];
          nsMutationReceiver* orig = r->GetParent() ? r->GetParent() : r;
          if (ob->GetReceiverFor(removed, false, false) != orig) {
            // Make sure the elements which are removed from the
            // subtree are kept in the same observation set.
            nsMutationReceiver* tr;
            if (orig->Animations()) {
              tr = nsAnimationReceiver::Create(removed, orig);
            } else {
              tr = nsMutationReceiver::Create(removed, orig);
            }
            transientReceivers->AppendObject(tr);
          }
        }
      }
    }
    if (wantsChildList && (mRemovedNodes.Length() || mAddedNodes.Length())) {
      RefPtr<nsSimpleContentList> addedList =
        new nsSimpleContentList(mBatchTarget);
      for (uint32_t i = 0; i < mAddedNodes.Length(); ++i) {
        addedList->AppendElement(mAddedNodes[i]);
      }
      RefPtr<nsDOMMutationRecord> m =
        new nsDOMMutationRecord(nsGkAtoms::childList,
                                ob->GetParentObject());
      m->mTarget = mBatchTarget;
      m->mRemovedNodes = removedList;
      m->mAddedNodes = addedList;
      m->mPreviousSibling = mPrevSibling;
      m->mNextSibling = mNextSibling;
      ob->AppendMutationRecord(m.forget());
    }
    // Always schedule the observer so that transient receivers are
    // removed correctly.
    ob->ScheduleForRun();
  }
  nsDOMMutationObserver::LeaveMutationHandling();
}

nsAutoAnimationMutationBatch*
nsAutoAnimationMutationBatch::sCurrentBatch = nullptr;

void
nsAutoAnimationMutationBatch::Done()
{
  if (sCurrentBatch != this) {
    return;
  }

  sCurrentBatch = nullptr;
  if (mObservers.IsEmpty()) {
    nsDOMMutationObserver::LeaveMutationHandling();
    // Nothing to do.
    return;
  }

  mBatchTargets.Sort(TreeOrderComparator());

  for (nsDOMMutationObserver* ob : mObservers) {
    bool didAddRecords = false;

    for (nsINode* target : mBatchTargets) {
      EntryArray* entries = mEntryTable.Get(target);
      MOZ_ASSERT(entries,
        "Targets in entry table and targets list should match");

      RefPtr<nsDOMMutationRecord> m =
        new nsDOMMutationRecord(nsGkAtoms::animations, ob->GetParentObject());
      m->mTarget = target;

      for (const Entry& e : *entries) {
        if (e.mState == eState_Added) {
          m->mAddedAnimations.AppendElement(e.mAnimation);
        } else if (e.mState == eState_Removed) {
          m->mRemovedAnimations.AppendElement(e.mAnimation);
        } else if (e.mState == eState_RemainedPresent && e.mChanged) {
          m->mChangedAnimations.AppendElement(e.mAnimation);
        }
      }

      if (!m->mAddedAnimations.IsEmpty() ||
          !m->mChangedAnimations.IsEmpty() ||
          !m->mRemovedAnimations.IsEmpty()) {
        ob->AppendMutationRecord(m.forget());
        didAddRecords = true;
      }
    }

    if (didAddRecords) {
      ob->ScheduleForRun();
    }
  }
  nsDOMMutationObserver::LeaveMutationHandling();
}
