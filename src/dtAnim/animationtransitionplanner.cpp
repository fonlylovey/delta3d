/*
 * stanceplanner.cpp
 *
 *  Created on: Aug 24, 2014
 *      Author: david
 */

#include <dtAnim/animationtransitionplanner.h>
#include <dtAnim/animationchannel.h>
#include <dtAnim/animationsequence.h>
#include <dtAnim/animationhelper.h>
#include <dtAnim/animationwrapper.h>
#include <dtAnim/sequencemixer.h>
#include <dtGame/actorcomponentcontainer.h>
#include <dtAI/worldstate.h>
#include <dtAI/basenpcutils.h>
#include <dtUtil/log.h>
#include <dtUtil/mathdefines.h>

#include <deque>

namespace dtAnim
{
   class HumanOperator;

   IMPLEMENT_ENUM(WeaponStateEnum);
   WeaponStateEnum WeaponStateEnum::NO_WEAPON("NO_WEAPON");
   WeaponStateEnum WeaponStateEnum::STOWED("STOWED");
   WeaponStateEnum WeaponStateEnum::DEPLOYED("DEPLOYED");
   WeaponStateEnum WeaponStateEnum::FIRING_POSITION("FIRING_POSITION");

   WeaponStateEnum::WeaponStateEnum(const std::string& name) : dtUtil::Enumeration(name)
   {
      AddInstance(this);
   }


   ////////////////////////////////////////////////////////////////////////////
   ////////////////////////////////////////////////////////////////////////////
   template <typename StateVarType>
   class EnumerationConditional: public dtAI::IConditional
   {
      public:
         typedef typename StateVarType::EnumValueType StateVarEnumType;

         EnumerationConditional(const dtUtil::RefString& pName, StateVarEnumType& pData): mName(pName), mData(pData) {}
         ~EnumerationConditional() {}

         /*virtual*/ const std::string& GetName() const
         {
            return mName;
         }

         /*virtual*/ bool Evaluate(const dtAI::WorldState* pWS)
         {
            const StateVarType* pStateVar;
            pWS->GetState(mName, pStateVar);
            if(pStateVar != NULL)
            {
               return pStateVar->GetValue() == mData;
            }
            return false;
         }

      private:
         dtUtil::RefString mName;
         StateVarEnumType& mData;
   };

   template <typename StateVarType>
   class EnumeratedEffect: public dtAI::IEffect
   {
   public:
      typedef typename StateVarType::EnumValueType StateVarEnumType;

      EnumeratedEffect(const dtUtil::RefString& pName, StateVarEnumType& pData): mName(pName), mData(pData){}

      const std::string& GetName() const
      {
         return mName;
      }

      bool Apply(const dtAI::Operator*, dtAI::WorldState* pWSIn) const
      {
         StateVarType* pStateVar;
         pWSIn->GetState(mName, pStateVar);
         if (pStateVar != NULL)
         {
            pStateVar->SetValue(mData);
         }
         return true;
      }

   protected:
      ~EnumeratedEffect(){}

   private:
      const dtUtil::RefString mName;
      StateVarEnumType& mData;
   };

   /**
    * Increments a numeric state variable.
    */
   template <typename StateVarType>
   class IncrementEffect: public dtAI::IEffect
   {
   public:
      IncrementEffect(const dtUtil::RefString& pName): mName(pName){}

      const std::string& GetName() const
      {
         return mName;
      }

      bool Apply(const dtAI::Operator*, dtAI::WorldState* pWSIn) const
      {
         StateVarType* pStateVar;
         pWSIn->GetState(mName, pStateVar);
         if(pStateVar != NULL)
         {
            pStateVar->Set(pStateVar->Get() + 1);
         }
         return true;
      }

   protected:
      ~IncrementEffect(){}

   private:
      const dtUtil::RefString mName;
   };

   class HumanOperator: public dtAI::Operator
   {
      public:
         typedef dtAI::IEffect EffectType;
         typedef std::vector<dtCore::RefPtr<EffectType> > EffectList;

         typedef EnumerationConditional<BasicStanceState> BasicStanceEnumConditional;
         typedef EnumerationConditional<WeaponState> WeaponStateEnumConditional;

         typedef EnumeratedEffect<BasicStanceState> BasicStanceEnumEffect;
         typedef EnumeratedEffect<WeaponState> WeaponStateEnumEffect;

         typedef IncrementEffect<dtAI::StateVar<unsigned> > UnsignedIntIncrementEffect;

      public:
         HumanOperator(const dtUtil::RefString& pName);

         void SetCost(float pcost);

         void AddEffect(EffectType* pEffect);

         void EnqueueReplacementAnim(const dtUtil::RefString& animName) const;

         bool GetNextReplacementAnim(dtUtil::RefString& animName, bool dequeue = true) const;

         bool Apply(const dtAI::Operator* oper, dtAI::WorldState* pWSIn) const;

      private:

         float mCost;
         EffectList mEffects;
         mutable std::deque<dtUtil::RefString> mReplacementQueue;
   };

   ////////////////////////////////////////////////////////////////////////////

   const dtGame::ActorComponent::ACType AnimationTransitionPlanner::TYPE(new dtCore::ActorType("dtAnim", "StancePlanner",
         "An AI planner for chaining animation transitions together for stances and weapons.",
         dtGame::ActorComponent::BaseActorComponentType));


   const dtUtil::RefString AnimationTransitionPlanner::STATE_BASIC_STANCE("BasicStanceState");
   const dtUtil::RefString AnimationTransitionPlanner::STATE_WEAPON("WeaponState");
   const dtUtil::RefString AnimationTransitionPlanner::STATE_DEAD("DeadState");
   //const dtUtil::RefString StancePlanner::STATE_MOVING("MovingState");
   const dtUtil::RefString AnimationTransitionPlanner::STATE_TRANSITION("TranstionState");
   const dtUtil::RefString AnimationTransitionPlanner::STATE_STANDING_ACTION_COUNT("StandingActionCountState");
   const dtUtil::RefString AnimationTransitionPlanner::STATE_KNEELING_ACTION_COUNT("KneelingActionCountState");
   const dtUtil::RefString AnimationTransitionPlanner::STATE_PRONE_ACTION_COUNT("ProneActionCountState");
   const dtUtil::RefString AnimationTransitionPlanner::STATE_SHOT("ShotState");

   /////////////////////////////////////////////////////////////////////////////
   AnimationTransitionPlanner::AnimationTransitionPlanner()
   : ActorComponent(TYPE)
   , mIsDead(false)
   , mStance(&BasicStanceEnum::STANDING)
   , mWeaponState(&dtAnim::WeaponStateEnum::STOWED)
   , mMaxTimePerIteration(0.5)
   , mPlannerHelper(
         dtAI::PlannerHelper::RemainingCostFunctor(this, &AnimationTransitionPlanner::GetRemainingCost),
         dtAI::PlannerHelper::DesiredStateFunctor(this, &AnimationTransitionPlanner::IsDesiredState)
         )
   , mAnimOperators(mPlannerHelper)
   {
      std::vector<dtAnim::BasicStanceEnum*> basicStances = dtAnim::BasicStanceEnum::EnumerateType();
      for (unsigned i = 0; i < basicStances.size(); ++i)
      {
         mExecutedActionCounts.insert(std::make_pair(basicStances[i], 0U));
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   AnimationTransitionPlanner::~AnimationTransitionPlanner()
   {
   }

   ////////////////////////////////////////////////////////////////////////////
   void AnimationTransitionPlanner::Setup()
   {
      dtAI::WorldState initialState;

      BasicStanceState* stanceState = new BasicStanceState();
      stanceState->SetStance(BasicStanceEnum::STANDING);

      initialState.AddState(STATE_BASIC_STANCE,         stanceState);

      WeaponState* weaponState = new WeaponState();
      weaponState->SetWeaponStateEnum(WeaponStateEnum::DEPLOYED);
      initialState.AddState(STATE_WEAPON,                weaponState);

      initialState.AddState(STATE_DEAD,                  new dtAI::StateVariable(false));

//         initialState.AddState(STATE_MOVING,                new dtAI::StateVariable(false));
      //Setting transition to true will make the planner generate the correct initial animation.
      initialState.AddState(STATE_TRANSITION,            new dtAI::StateVariable(true));
      initialState.AddState(STATE_STANDING_ACTION_COUNT, new dtAI::StateVar<unsigned>(0U));
      initialState.AddState(STATE_KNEELING_ACTION_COUNT, new dtAI::StateVar<unsigned>(0U));
      initialState.AddState(STATE_PRONE_ACTION_COUNT,    new dtAI::StateVar<unsigned>(0U));
      initialState.AddState(STATE_SHOT,                  new dtAI::StateVariable(false));


      mPlannerHelper.SetCurrentState(initialState);
   }

   ////////////////////////////////////////////////////////////////////////////
   float AnimationTransitionPlanner::GetRemainingCost(const dtAI::WorldState* pWS) const
   {
      float value = 1.0f;

      const WeaponState* weaponState;
      pWS->GetState(STATE_WEAPON, weaponState);
      WeaponStateEnum* effectiveWeaponState = &WeaponStateEnum::FIRING_POSITION;
      if (*mWeaponState != WeaponStateEnum::FIRING_POSITION)
      {
         effectiveWeaponState = &WeaponStateEnum::DEPLOYED;
      }

      if (weaponState->GetWeaponStateEnum() != *effectiveWeaponState)
      {
         value += 1.0;
      }

      float preactionValue = value;

      value += 2.0 * float(CheckActionState(pWS, STATE_STANDING_ACTION_COUNT, mExecutedActionCounts.find(&BasicStanceEnum::STANDING)->second));
      value += 2.0 * float(CheckActionState(pWS, STATE_KNEELING_ACTION_COUNT, mExecutedActionCounts.find(&BasicStanceEnum::KNEELING)->second));
      value += 2.0 * float(CheckActionState(pWS, STATE_PRONE_ACTION_COUNT, mExecutedActionCounts.find(&BasicStanceEnum::PRONE)->second));

      //Only add the stance difference if no actions need to be performed
      if (preactionValue == value)
      {
         const BasicStanceState* stanceState;
         pWS->GetState(STATE_BASIC_STANCE, stanceState);

         // Use a smaller number for here than the actions so that completing the final action
         // won't make the planner think it is no closer to its goal.
         value += dtUtil::Abs(stanceState->GetStance().GetCostValue() - mStance->GetCostValue());
      }

      const dtAI::StateVariable* deadState;
      pWS->GetState(STATE_DEAD, deadState);

      //dead is the same as the damage state being equal to destroyed.
      if (deadState->Get() != (GetIsDead()))
         value += 1.0;

      return value;
   }

   ////////////////////////////////////////////////////////////////////////////
   unsigned AnimationTransitionPlanner::CheckActionState(const dtAI::WorldState* pWS, const std::string& stateName, unsigned desiredVal) const
   {
      const dtAI::StateVar<unsigned>* actionState = NULL;
      pWS->GetState(stateName, actionState);
      if (actionState == NULL)
      {
         return 0U;
      }

      if (desiredVal < actionState->Get())
      {
         return 0U;
      }

      return desiredVal - actionState->Get();
   }

   ////////////////////////////////////////////////////////////////////////////
   bool AnimationTransitionPlanner::IsDesiredState(const dtAI::WorldState* pWS) const
   {
      //If we are in a transition, we are not in the desired state.
      const dtAI::StateVariable* transState;
      pWS->GetState(STATE_TRANSITION, transState);
      if (transState->Get())
         return false;

      const dtAI::StateVariable* deadState;
      pWS->GetState(STATE_DEAD, deadState);

      //dead is the same as the damage state being equal to destroyed.
      if (deadState->Get() != (GetIsDead()))
         return false;

      //If we are dead, ignore any other changes.  Just let's be dead, shall we :-)
      if (deadState->Get() && GetIsDead())
         return true;

      const BasicStanceState* stanceState;
      pWS->GetState(STATE_BASIC_STANCE, stanceState);

      if (stanceState->GetStance() != *mStance)
         return false;

      const WeaponState* weaponState;
      pWS->GetState(STATE_WEAPON, weaponState);

      WeaponStateEnum* effectiveWeaponState = &WeaponStateEnum::FIRING_POSITION;
      if (*mWeaponState != WeaponStateEnum::FIRING_POSITION)
         effectiveWeaponState = &WeaponStateEnum::DEPLOYED;

      if (weaponState->GetWeaponStateEnum() != *effectiveWeaponState)
         return false;

//         const dtAI::StateVariable* movingState;
//         pWS->GetState(STATE_MOVING, movingState);

      //This requires that plans be made in one frame.
      //Moving is the same as the velocity > 0.
      // When standing, we use the same thing for both standing
//         if (stanceState->GetStance() != BasicStanceEnum::STANDING && movingState->Get() != !dtUtil::Equivalent(CalculateWalkingSpeed(), 0.0f))
//            return false;

      bool actionStateResult =
         0U == CheckActionState(pWS, STATE_STANDING_ACTION_COUNT, mExecutedActionCounts.find(&BasicStanceEnum::STANDING)->second) &&
         0U == CheckActionState(pWS, STATE_KNEELING_ACTION_COUNT, mExecutedActionCounts.find(&BasicStanceEnum::KNEELING)->second) &&
         0U == CheckActionState(pWS, STATE_PRONE_ACTION_COUNT,    mExecutedActionCounts.find(&BasicStanceEnum::PRONE)->second);

      if (!actionStateResult)
         { return false; }

      return true;
   }

   ////////////////////////////////////////////////////////////////////////////////////
   const Animatable* AnimationTransitionPlanner::ApplyOperatorAndGetAnimatable(const dtAI::Operator& op)
   {
      AnimationHelper* animAC = GetOwner()->GetComponent<AnimationHelper>();
      SequenceMixer& seqMixer = animAC->GetSequenceMixer();
      op.Apply(mPlannerHelper.GetCurrentState());

      const Animatable* animatable = NULL;

      const HumanOperator* hOp = dynamic_cast<const HumanOperator*>(&op);
      dtUtil::RefString nextReplacementAnim;
      if (hOp != NULL && hOp->GetNextReplacementAnim(nextReplacementAnim))
      {
         animatable = seqMixer.GetRegisteredAnimation(nextReplacementAnim);
      }
      else
      {
         animatable = seqMixer.GetRegisteredAnimation(op.GetName());
      }

      return animatable;
   }

   ////////////////////////////////////////////////////////////////////////////////////
   unsigned AnimationTransitionPlanner::GetExecutedActionCount(BasicStanceEnum& stance) const
   {
      return mExecutedActionCounts.find(&stance)->second;
   }

   ////////////////////////////////////////////////////////////////////////////////////
   void AnimationTransitionPlanner::ExecuteAction(const dtUtil::RefString& animatableName, BasicStanceEnum& basicStance)
   {
      mExecutedActionCounts[&basicStance]++;

      dtUtil::RefString actionOpName;

      if (basicStance == BasicStanceEnum::STANDING)
      {
         actionOpName = AnimationOperators::ANIM_STANDING_ACTION;
      }
      else if (basicStance == BasicStanceEnum::KNEELING)
      {
         actionOpName = AnimationOperators::ANIM_KNEELING_ACTION;
      }
      else if (basicStance == BasicStanceEnum::PRONE)
      {
         actionOpName = AnimationOperators::ANIM_PRONE_ACTION;
      }

      if (animatableName != actionOpName)
      {
         const HumanOperator* hOp = NULL;
         hOp = dynamic_cast<const HumanOperator*>(mPlannerHelper.GetOperator(actionOpName));
         if (hOp != NULL)
         {
            hOp->EnqueueReplacementAnim(animatableName);
         }
      }

//      if (!IsRemote())
//      {
//         //TODO send action message.
//      }
   }

   ////////////////////////////////////////////////////////////////////////////
   void AnimationTransitionPlanner::CheckAndUpdateAnimationState()
   {
      // TODO Check the animation helper for this.
      if (!IsDesiredState(mPlannerHelper.GetCurrentState())) //&& mModelNode.valid())
      {
         LOGN_DEBUG("stanceplanner.cpp", "The planner is not in the desired state on actor named \"" + GetName() + "\".  Generating animations.");

         UpdatePlanAndAnimations();
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   void AnimationTransitionPlanner::UpdatePlanAndAnimations()
   {
      const float blendTime = 0.2f;

      const dtAI::StateVariable* deadState;
      mPlannerHelper.GetCurrentState()->GetState(STATE_DEAD, deadState);

      //if we WERE dead and now we are not, we have to reset our state.
      if (deadState->Get() && !GetIsDead())
         Setup();

      bool gottaSequence = GenerateNewAnimationSequence();
      if (!gottaSequence)
      {
         Setup();
         gottaSequence = GenerateNewAnimationSequence();
      }

      if (gottaSequence)
      {
         dtAI::Planner::OperatorList::iterator i, iend;
         SequenceMixer& seqMixer = GetOwner()->GetComponent<AnimationHelper>()->GetSequenceMixer();
         dtCore::RefPtr<AnimationSequence> generatedSequence = new AnimationSequence();

         if (mSequenceId.empty())
         {
            mSequenceId = "seq:0";
         }
         else
         {
            // rather than do an int to string, just change the last character so it does'0'-'9'.
            // this will require much less overhead, and won't ever require allocating
            // and deallocating string memory.
            mSequenceId[4] = (((mSequenceId[4] - '0') + 1) % 10) + '0';
         }

         generatedSequence->SetName(mSequenceId);

         LOGN_DEBUG("stanceplanner.cpp", "Current animation plan has \"" + dtUtil::ToString(mCurrentPlan.size()) + "\" steps.");

         if (!mCurrentPlan.empty())
         {
            i = mCurrentPlan.begin();
            iend = mCurrentPlan.end();

            float accumulatedStartTime = 0.0f;

            dtCore::RefPtr<Animatable> newAnim;
            for (; i != iend; ++i)
            {
               //if the last anim was NOT the last one, it has to end and be an action
               if (newAnim)
               {
                  AnimationChannel* animChannel = dynamic_cast<AnimationChannel*>(newAnim.get());
                  if (animChannel != NULL)
                  {

                     float duration = animChannel->GetAnimation()->GetDuration();
                     accumulatedStartTime += (duration - blendTime);
                     animChannel->SetMaxDuration(duration);
                     animChannel->SetAction(true);
                  }
               }

               const Animatable* animatable = ApplyOperatorAndGetAnimatable(**i);

               if (animatable != NULL)
               {
                  LOGN_DEBUG("stanceplanner.cpp", std::string("Adding animatable named \"") + animatable->GetName().c_str() + "\".");
                  newAnim = animatable->Clone(GetOwner()->GetComponent<AnimationHelper>()->GetModelWrapper());
                  newAnim->SetStartDelay(std::max(0.0f, accumulatedStartTime));
                  newAnim->SetFadeIn(blendTime);
                  newAnim->SetFadeOut(blendTime);

                  generatedSequence->AddAnimation(newAnim);
               }
               else
               {
                  newAnim = NULL;
               }
            }

            seqMixer.ClearActiveAnimations(blendTime);
            seqMixer.PlayAnimation(generatedSequence.get());

            SignalAnimationsTransitioning.emit_signal(*this);
         }
      }
      else
      {
         AnimationHelper* animAC = GetOwner()->GetComponent<AnimationHelper>();
         //This is the error-out state.
         animAC->ClearAll(blendTime);
         animAC->PlayAnimation(AnimationOperators::ANIM_WALK_DEPLOYED);
         SignalAnimationsTransitioning.emit_signal(*this);
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   bool AnimationTransitionPlanner::GenerateNewAnimationSequence()
   {
      mCurrentPlan.clear();
      mPlanner.Reset(&mPlannerHelper);

      mPlanner.GetConfig().mMaxTimePerIteration = mMaxTimePerIteration;

      dtAI::Planner::PlannerResult result = mPlanner.GeneratePlan();
      if (result == dtAI::Planner::PLAN_FOUND)
      {
         mCurrentPlan = mPlanner.GetConfig().mResult;
         //std::cout << " BOGUS TEST -- stanceplanner.cpp - Plan took[" << mPlanner.GetConfig().mTotalElapsedTime << "]." << std::endl;
         return true;
      }
      else
      {
         std::ostringstream ss;
         ss << "Unable to generate a plan. Time[" << mPlanner.GetConfig().mTotalElapsedTime
            << "]\n\nGoing from:\n\n"
            << *mPlannerHelper.GetCurrentState()
            << "\n\n Going To:\n\n"
            << "Stance:  \"" << GetStance().GetName()
            << "\"\n Primary Weapon: \"" << GetWeaponState().GetName()
            << "\"\n IsDead: \"" << GetIsDead();
         ExecuteActionCountMap::const_iterator i, iend;
         i = mExecutedActionCounts.begin();
         iend = mExecutedActionCounts.end();
         for (; i != iend; ++i)
         {
            ss << i->first->GetName() << ": \"" << i->second << "\" \n";
         }
         LOGN_ERROR("stanceplanner.cpp", ss.str());
      }
      return false;
   }

   ////////////////////////////////////////////////////////////////////////////
   const dtAI::Planner::OperatorList& AnimationTransitionPlanner::GetCurrentPlan()
   {
      return mCurrentPlan;
   }

   ////////////////////////////////////////////////////////////////////////////
   void AnimationTransitionPlanner::OnEnteredWorld()
   {
      BaseClass::OnEnteredWorld();

      AnimationHelper* animAC = GetOwner()->GetComponent<AnimationHelper>();

      if (animAC == NULL)
      {
         animAC = new dtAnim::AnimationHelper;
         GetOwner()->AddComponent(*animAC);
      }

      if (animAC != NULL)
      {
         if (animAC->GetModelWrapper() != NULL)
         {
            OnModelLoaded(animAC);
         }
         else
         {
            // Reset the state just so it's not empty.
            Setup();
         }

         animAC->ModelLoadedSignal.connect_slot(this, &AnimationTransitionPlanner::OnModelLoaded);
         animAC->ModelUnloadedSignal.connect_slot(this, &AnimationTransitionPlanner::OnModelUnloaded);
      }
   }

   ////////////////////////////////////////////////////////////////////////
   void AnimationTransitionPlanner::OnModelLoaded(AnimationHelper*)
   {
      Setup();
      UpdatePlanAndAnimations();
   }

   ////////////////////////////////////////////////////////////////////////
   void AnimationTransitionPlanner::OnModelUnloaded(AnimationHelper*)
   {

   }

   ////////////////////////////////////////////////////////////////////////
   DT_IMPLEMENT_ACCESSOR(AnimationTransitionPlanner, bool, IsDead);
   DT_IMPLEMENT_ACCESSOR(AnimationTransitionPlanner, dtUtil::EnumerationPointer<BasicStanceEnum>, Stance);
   DT_IMPLEMENT_ACCESSOR(AnimationTransitionPlanner, dtUtil::EnumerationPointer<WeaponStateEnum>, WeaponState);
   DT_IMPLEMENT_ACCESSOR(AnimationTransitionPlanner, double, MaxTimePerIteration);


   ////////////////////////////////////////////////////////////////////////////
   ////////////////////////////////////////////////////////////////////////////
   const dtUtil::RefString AnimationOperators::ANIM_WALK_READY("Walk Run Ready");
   const dtUtil::RefString AnimationOperators::ANIM_WALK_DEPLOYED("Walk Run Deployed");

   const dtUtil::RefString AnimationOperators::ANIM_LOW_WALK_READY("Kneel-Low Walk Ready");
   const dtUtil::RefString AnimationOperators::ANIM_LOW_WALK_DEPLOYED("Kneel-Low Walk Deployed");

   const dtUtil::RefString AnimationOperators::ANIM_STAND_TO_KNEEL("Stand To Kneel");
   const dtUtil::RefString AnimationOperators::ANIM_KNEEL_TO_STAND("Kneel To Stand");

   const dtUtil::RefString AnimationOperators::ANIM_CRAWL_READY("Prone-Crawl Ready");
   const dtUtil::RefString AnimationOperators::ANIM_CRAWL_DEPLOYED("Prone-Crawl Deployed");

   const dtUtil::RefString AnimationOperators::ANIM_PRONE_TO_KNEEL("Prone To Kneel");
   const dtUtil::RefString AnimationOperators::ANIM_KNEEL_TO_PRONE("Kneel To Prone");

   const dtUtil::RefString AnimationOperators::ANIM_SHOT_STANDING("Shot Standing");
   const dtUtil::RefString AnimationOperators::ANIM_SHOT_KNEELING("Shot Kneeling");
   const dtUtil::RefString AnimationOperators::ANIM_SHOT_PRONE("Shot Prone");

   const dtUtil::RefString AnimationOperators::ANIM_DEAD_STANDING("Dead Standing");
   const dtUtil::RefString AnimationOperators::ANIM_DEAD_KNEELING("Dead Kneeling");
   const dtUtil::RefString AnimationOperators::ANIM_DEAD_PRONE("Dead Prone");

   const dtUtil::RefString AnimationOperators::ANIM_STANDING_ACTION("Standing Action");
   const dtUtil::RefString AnimationOperators::ANIM_KNEELING_ACTION("Kneeling Action");
   const dtUtil::RefString AnimationOperators::ANIM_PRONE_ACTION("Prone Action");

   const dtUtil::RefString AnimationOperators::OPER_DEPLOYED_TO_READY("Deployed To Ready");
   const dtUtil::RefString AnimationOperators::OPER_READY_TO_DEPLOYED("Ready To Deployed");

   ////////////////////////////////////////////////////////////////////////////
   AnimationOperators::AnimationOperators(dtAI::PlannerHelper& plannerHelper):
      mPlannerHelper(plannerHelper)
   {
      CreateOperators();
   }

   ////////////////////////////////////////////////////////////////////////////
   AnimationOperators::~AnimationOperators()
   {
   }


   ////////////////////////////////////////////////////////////////////////////
   HumanOperator* AnimationOperators::AddOperator(const std::string& name)
   {
      HumanOperator* op = new HumanOperator(name);
      mOperators.insert(std::make_pair(op->GetName(), op));
      mPlannerHelper.AddOperator(op);
      return op;
   }

   ////////////////////////////////////////////////////////////////////////////
   void AnimationOperators::CreateOperators()
   {
      dtCore::RefPtr<HumanOperator::BasicStanceEnumConditional> standing
         = new HumanOperator::BasicStanceEnumConditional(AnimationTransitionPlanner::STATE_BASIC_STANCE, BasicStanceEnum::STANDING);

      dtCore::RefPtr<HumanOperator::BasicStanceEnumConditional> kneeling
         = new HumanOperator::BasicStanceEnumConditional(AnimationTransitionPlanner::STATE_BASIC_STANCE, BasicStanceEnum::KNEELING);

      dtCore::RefPtr<HumanOperator::BasicStanceEnumConditional> prone
         = new HumanOperator::BasicStanceEnumConditional(AnimationTransitionPlanner::STATE_BASIC_STANCE, BasicStanceEnum::PRONE);

      dtCore::RefPtr<HumanOperator::BasicStanceEnumConditional> idle
         = new HumanOperator::BasicStanceEnumConditional(AnimationTransitionPlanner::STATE_BASIC_STANCE, BasicStanceEnum::IDLE);


      dtCore::RefPtr<HumanOperator::WeaponStateEnumConditional> deployed
         = new HumanOperator::WeaponStateEnumConditional(AnimationTransitionPlanner::STATE_WEAPON, WeaponStateEnum::DEPLOYED);

      dtCore::RefPtr<HumanOperator::WeaponStateEnumConditional> ready
         = new HumanOperator::WeaponStateEnumConditional(AnimationTransitionPlanner::STATE_WEAPON, WeaponStateEnum::FIRING_POSITION);


//      dtCore::RefPtr<dtAI::Precondition> isDead
//         = new dtAI::Precondition(StancePlanner::STATE_DEAD, true);

      dtCore::RefPtr<dtAI::Precondition> isShot
         = new dtAI::Precondition(AnimationTransitionPlanner::STATE_SHOT, true);


      dtCore::RefPtr<dtAI::Precondition> isTransition
         = new dtAI::Precondition(AnimationTransitionPlanner::STATE_TRANSITION, true);

      dtCore::RefPtr<dtAI::Precondition> notTransition
         = new dtAI::Precondition(AnimationTransitionPlanner::STATE_TRANSITION, false);

      dtCore::RefPtr<HumanOperator::BasicStanceEnumEffect> standingEff
         = new HumanOperator::BasicStanceEnumEffect(AnimationTransitionPlanner::STATE_BASIC_STANCE, BasicStanceEnum::STANDING);

      dtCore::RefPtr<HumanOperator::BasicStanceEnumEffect> kneelingEff
         = new HumanOperator::BasicStanceEnumEffect(AnimationTransitionPlanner::STATE_BASIC_STANCE, BasicStanceEnum::KNEELING);

      dtCore::RefPtr<HumanOperator::BasicStanceEnumEffect> proneEff
         = new HumanOperator::BasicStanceEnumEffect(AnimationTransitionPlanner::STATE_BASIC_STANCE, BasicStanceEnum::PRONE);


      dtCore::RefPtr<HumanOperator::WeaponStateEnumEffect> readyEff
         = new HumanOperator::WeaponStateEnumEffect(AnimationTransitionPlanner::STATE_WEAPON, WeaponStateEnum::FIRING_POSITION);

      dtCore::RefPtr<HumanOperator::WeaponStateEnumEffect> deployedEff
         = new HumanOperator::WeaponStateEnumEffect(AnimationTransitionPlanner::STATE_WEAPON, WeaponStateEnum::DEPLOYED);

      dtCore::RefPtr<HumanOperator::UnsignedIntIncrementEffect >
         incrementStandingActionCount = new HumanOperator::UnsignedIntIncrementEffect(AnimationTransitionPlanner::STATE_STANDING_ACTION_COUNT);
      dtCore::RefPtr<HumanOperator::UnsignedIntIncrementEffect >
         incrementKneelingActionCount = new HumanOperator::UnsignedIntIncrementEffect(AnimationTransitionPlanner::STATE_KNEELING_ACTION_COUNT);
      dtCore::RefPtr<HumanOperator::UnsignedIntIncrementEffect >
         incrementProneActionCount = new HumanOperator::UnsignedIntIncrementEffect(AnimationTransitionPlanner::STATE_PRONE_ACTION_COUNT);

      dtCore::RefPtr<dtAI::Effect>
         deadEff = new dtAI::Effect(AnimationTransitionPlanner::STATE_DEAD, true);

      dtCore::RefPtr<dtAI::Effect>
         shotEff = new dtAI::Effect(AnimationTransitionPlanner::STATE_SHOT, true);

      dtCore::RefPtr<dtAI::Effect>
         transitionEff = new dtAI::Effect(AnimationTransitionPlanner::STATE_TRANSITION, true);

      dtCore::RefPtr<dtAI::Effect>
         notTransitionEff = new dtAI::Effect(AnimationTransitionPlanner::STATE_TRANSITION, false);

      HumanOperator* newOp;

      newOp = AddOperator(ANIM_WALK_READY);
      newOp->AddPreCondition(standing.get());
      newOp->AddPreCondition(ready.get());

      newOp->AddEffect(standingEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_WALK_DEPLOYED);
      newOp->AddPreCondition(standing.get());
      newOp->AddPreCondition(deployed.get());

      newOp->AddEffect(standingEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_LOW_WALK_READY);
      newOp->AddPreCondition(kneeling.get());
      newOp->AddPreCondition(ready.get());

      newOp->AddEffect(kneelingEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_LOW_WALK_DEPLOYED);
      newOp->AddPreCondition(kneeling.get());
      newOp->AddPreCondition(deployed.get());

      newOp->AddEffect(kneelingEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_STAND_TO_KNEEL);
      newOp->AddPreCondition(standing.get());
      newOp->AddPreCondition(deployed.get());

      newOp->AddEffect(kneelingEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_KNEEL_TO_STAND);
      newOp->AddPreCondition(kneeling.get());
      newOp->AddPreCondition(deployed.get());

      newOp->AddEffect(standingEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_PRONE_TO_KNEEL);
      newOp->AddPreCondition(prone.get());
      newOp->AddPreCondition(deployed.get());

      newOp->AddEffect(kneelingEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_CRAWL_READY);
      newOp->AddPreCondition(prone.get());
      newOp->AddPreCondition(ready.get());

      newOp->AddEffect(proneEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_CRAWL_DEPLOYED);
      newOp->AddPreCondition(prone.get());
      newOp->AddPreCondition(deployed.get());

      newOp->AddEffect(proneEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_KNEEL_TO_PRONE);
      newOp->AddPreCondition(kneeling.get());
      newOp->AddPreCondition(deployed.get());

      newOp->AddEffect(proneEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(OPER_READY_TO_DEPLOYED);
      newOp->AddPreCondition(ready.get());
      newOp->AddEffect(deployedEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(OPER_DEPLOYED_TO_READY);
      newOp->AddPreCondition(deployed.get());
      newOp->AddEffect(readyEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_SHOT_STANDING);
      newOp->AddPreCondition(standing.get());
      newOp->AddEffect(shotEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_SHOT_KNEELING);
      newOp->AddPreCondition(kneeling.get());
      newOp->AddEffect(shotEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_SHOT_PRONE);
      newOp->AddPreCondition(prone.get());
      newOp->AddEffect(shotEff.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_DEAD_STANDING);
      newOp->AddPreCondition(standing.get());
      newOp->AddPreCondition(isShot.get());
      newOp->AddEffect(deadEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_DEAD_KNEELING);
      newOp->AddPreCondition(kneeling.get());
      newOp->AddPreCondition(isShot.get());
      newOp->AddEffect(deadEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_DEAD_PRONE);
      newOp->AddPreCondition(prone.get());
      newOp->AddPreCondition(isShot.get());
      newOp->AddEffect(deadEff.get());
      newOp->AddEffect(notTransitionEff.get());

      newOp = AddOperator(ANIM_STANDING_ACTION);
      newOp->AddPreCondition(standing.get());
      newOp->AddPreCondition(deployed.get());
      newOp->AddEffect(incrementStandingActionCount.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_KNEELING_ACTION);
      newOp->AddPreCondition(kneeling.get());
      newOp->AddPreCondition(deployed.get());
      newOp->AddEffect(incrementKneelingActionCount.get());
      newOp->AddEffect(transitionEff.get());

      newOp = AddOperator(ANIM_PRONE_ACTION);
      newOp->AddPreCondition(prone.get());
      newOp->AddPreCondition(deployed.get());
      newOp->AddEffect(incrementProneActionCount.get());
      newOp->AddEffect(transitionEff.get());
   }

   ////////////////////////////////////////////////////////////////////////////
   ////////////////////////////////////////////////////////////////////////////
   IMPLEMENT_ENUM(BasicStanceEnum);
   BasicStanceEnum BasicStanceEnum::IDLE("IDLE", 1.75);
   BasicStanceEnum BasicStanceEnum::STANDING("STANDING", 1.75);
   BasicStanceEnum BasicStanceEnum::KNEELING("KNEELING", 1.00);
   BasicStanceEnum BasicStanceEnum::PRONE("PRONE", 0.00);

   ////////////////////////////////////////////////////////////////////////////
   BasicStanceEnum::BasicStanceEnum(const std::string& name, float costValue)
   : dtUtil::Enumeration(name)
   , mCostValue(costValue)
   {
      AddInstance(this);
   }

   ////////////////////////////////////////////////////////////////////////////
   float BasicStanceEnum::GetCostValue() const
   {
      return mCostValue;
   }

   ////////////////////////////////////////////////////////////////////////////
   ////////////////////////////////////////////////////////////////////////////
   BasicStanceState::BasicStanceState():
      mStance(&BasicStanceEnum::IDLE)
   {
   }

   ////////////////////////////////////////////////////////////////////////////
   BasicStanceState::~BasicStanceState()
   {
   }

   ////////////////////////////////////////////////////////////////////////////
   dtAI::IStateVariable* BasicStanceState::Copy() const
   {
      BasicStanceState* stanceState = new BasicStanceState;
      stanceState->mStance = mStance;
      return stanceState;
   }

   ////////////////////////////////////////////////////////////////////////////
   BasicStanceEnum& BasicStanceState::GetStance() const
   {
      return *mStance;
   }

   ////////////////////////////////////////////////////////////////////////////
   void BasicStanceState::SetStance(BasicStanceEnum& newStance)
   {
      mStance = &newStance;
   }
   ////////////////////////////////////////////////////////////////////////////
   BasicStanceEnum& BasicStanceState::GetValue() const
   {
      return GetStance();
   }

   ////////////////////////////////////////////////////////////////////////////
   void BasicStanceState::SetValue(BasicStanceEnum& pStance)
   {
      SetStance(pStance);
   }

   ////////////////////////////////////////////////////////////////////////////
   const std::string BasicStanceState::ToString() const
   {
      return GetStance().GetName();
   }

   ////////////////////////////////////////////////////////////////////////////
   ////////////////////////////////////////////////////////////////////////////
   WeaponState::WeaponState():
      mWeaponStateEnum(&WeaponStateEnum::STOWED)
   {
   }

   ////////////////////////////////////////////////////////////////////////////
   WeaponState::~WeaponState()
   {
   }

   ////////////////////////////////////////////////////////////////////////////
   dtAI::IStateVariable* WeaponState::Copy() const
   {
      WeaponState* weaponState = new WeaponState;
      weaponState->mWeaponStateEnum = mWeaponStateEnum;
      return weaponState;
   }

   ////////////////////////////////////////////////////////////////////////////
   WeaponStateEnum& WeaponState::GetWeaponStateEnum() const
   {
      return *mWeaponStateEnum;
   }

   ////////////////////////////////////////////////////////////////////////////
   void WeaponState::SetWeaponStateEnum(WeaponStateEnum& newWeaponStateEnum)
   {
      mWeaponStateEnum = &newWeaponStateEnum;
   }

   ////////////////////////////////////////////////////////////////////////////
   WeaponStateEnum& WeaponState::GetValue() const
   {
      return GetWeaponStateEnum();
   }

   ////////////////////////////////////////////////////////////////////////////
   void WeaponState::SetValue(WeaponStateEnum& pWeaponState)
   {
      SetWeaponStateEnum(pWeaponState);
   }

   ////////////////////////////////////////////////////////////////////////////
   const std::string WeaponState::ToString() const
   {
      return GetWeaponStateEnum().GetName();
   }

   ////////////////////////////////////////////////////////////////////////////
   ////////////////////////////////////////////////////////////////////////////
   HumanOperator::HumanOperator(const dtUtil::RefString& pName)
   : Operator(pName, Operator::ApplyOperatorFunctor(this, &HumanOperator::Apply))
   , mCost(1.0f)
   {}

   ////////////////////////////////////////////////////////////////////////////
   void HumanOperator::SetCost(float pcost) { mCost = pcost; }

   ////////////////////////////////////////////////////////////////////////////
   void HumanOperator::AddEffect(EffectType* pEffect) { mEffects.push_back(pEffect); }

   ////////////////////////////////////////////////////////////////////////////
   void HumanOperator::EnqueueReplacementAnim(const dtUtil::RefString& animName) const
   {
      mReplacementQueue.push_back(animName);
   }

   ////////////////////////////////////////////////////////////////////////////
   bool HumanOperator::GetNextReplacementAnim(dtUtil::RefString& animName, bool dequeue) const
   {
      if (mReplacementQueue.empty())
      {
         return false;
      }

      animName = mReplacementQueue.front();
      if (dequeue)
      {
         mReplacementQueue.pop_front();
      }
      return true;
   }

   ////////////////////////////////////////////////////////////////////////////
   bool HumanOperator::Apply(const dtAI::Operator* oper, dtAI::WorldState* pWSIn) const
   {
      //std::cout << GetName() << std::endl;
      EffectList::const_iterator iter = mEffects.begin();
      EffectList::const_iterator endOfList = mEffects.end();
      for (;iter != endOfList; ++iter)
      {
         (*iter)->Apply(oper, pWSIn);
      }

      pWSIn->AddCost(mCost);
      return true;
   }

}
