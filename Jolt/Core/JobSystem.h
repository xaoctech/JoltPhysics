// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#pragma once

#include <Core/Reference.h>
#include <Core/Color.h>
#include <Core/Profiler.h>
#include <Core/NonCopyable.h>
#include <Core/StaticArray.h>
#include <atomic>

namespace JPH {

/// A class that allows units of work (Jobs) to be scheduled across multiple threads.
/// It allows dependencies between the jobs so that the jobs form a graph.
/// 
/// The pattern for using this class is:
/// 
///		// Create job system
///		JobSystem *job_system = new JobSystemThreadPool(...);
///		
///		// Create some jobs
///		JobHandle second_job = job_system->CreateJob("SecondJob", Color::sRed, []() { ... }, 1); // Create a job with 1 dependency
///		JobHandle first_job = job_system->CreateJob("FirstJob", Color::sGreen, [second_job]() { ....; second_job.RemoveDependency(); }, 0); // Job can start immediately, will start second job when it's done
///		JobHandle third_job = job_system->CreateJob("ThirdJob", Color::sBlue, []() { ... }, 0); // This job can run immediately as well and can run in parallel to job 1 and 2
///		
///		// Add the jobs to the barrier so that we can execute them while we're waiting
///		Barrier *barrier = job_system->CreateBarrier();
///		barrier->AddJob(first_job);
///		barrier->AddJob(second_job);
///		barrier->AddJob(third_job);
///		job_system->WaitForJobs(barrier);
///		
/// 	// Clean up
/// 	job_system->DestroyBarrier(barrier);
/// 	delete job_system;
///	
///	Jobs are guaranteed to be started in the order that their dependency counter becomes zero (in case they're scheduled on a background thread) 
///	or in the order they're added to the barrier (when dependency count is zero and when executing on the thread that calls WaitForJobs).
class JobSystem : public NonCopyable
{
protected:
	class Job;

public:
	/// A job handle contains a reference to a job. The job will be deleted as soon as there are no JobHandles.
	/// referring to the job and when it is not in the job queue / being processed.
	class JobHandle : private Ref<Job>
	{
	public:
		/// Constructor 
		inline				JobHandle()									{ }
		inline				JobHandle(const JobHandle &inHandle)		: Ref<Job>(inHandle) { }
		inline				JobHandle(JobHandle &&inHandle) noexcept	: Ref<Job>(move(inHandle)) { }

		/// Constructor, only to be used by JobSystem
		inline				JobHandle(Job *inJob)						: Ref<Job>(inJob) { }

		/// Assignment
		inline JobHandle &	operator = (const JobHandle &inHandle)		{ Ref<Job>::operator = (inHandle); return *this; }
		inline JobHandle &	operator = (JobHandle &&inHandle) noexcept	{ Ref<Job>::operator = (move(inHandle)); return *this; }

		/// Check if this handle contains a job
		inline bool			IsValid() const								{ return GetPtr() != nullptr; }

		/// Check if this job has finished executing
		inline bool			IsDone() const								{ return GetPtr() != nullptr && GetPtr()->IsDone(); }

		/// Add to the dependency counter. 
		inline void			AddDependency(int inCount = 1) const		{ GetPtr()->AddDependency(inCount); }

		/// Remove from the dependency counter. Job will start whenever the dependency counter reaches zero
		/// and if it does it is no longer valid to call the AddDependency/RemoveDependency functions.
		inline void			RemoveDependency(int inCount = 1) const		{ GetPtr()->RemoveDependencyAndQueue(inCount); }

		/// Remove a dependency from a batch of jobs at once, this can be more efficient than removing them one by one as it requires less locking
		static inline void	sRemoveDependencies(JobHandle *inHandles, uint inNumHandles, int inCount = 1);

		/// Helper function to remove dependencies on a static array of job handles
		template <uint N>
		static inline void	sRemoveDependencies(StaticArray<JobHandle, N> &inHandles, int inCount = 1)
		{
			sRemoveDependencies(inHandles.data(), inHandles.size(), inCount);
		}

		/// Inherit the GetPtr function, only to be used by the JobSystem
		using Ref<Job>::GetPtr;
	};

	/// A job barrier keeps track of a number of jobs and allows waiting until they are all completed.
	class Barrier : public NonCopyable
	{
	public:
		/// Add a job to this barrier
		/// Note that jobs can keep being added to the barrier while waiting for the barrier
		virtual void		AddJob(const JobHandle &inJob) = 0;

		/// Add multiple jobs to this barrier
		/// Note that jobs can keep being added to the barrier while waiting for the barrier
		virtual void		AddJobs(const JobHandle *inHandles, uint inNumHandles) = 0;

	protected:
		/// Job needs to be able to call OnJobFinished
		friend class Job;

		/// Destructor, you should call JobSystem::DestroyBarrier instead of destructing this object directly
		virtual				~Barrier()									{ }

		/// Called by a Job to mark that it is finished
		virtual void		OnJobFinished(Job *inJob) = 0;
	};

	/// Main function of the job
	using JobFunction = function<void()>;

	/// Destructor
	virtual					~JobSystem()								{ }

	/// Get maximum number of concurrently executing jobs
	virtual int				GetMaxConcurrency() const = 0;

	/// Create a new job, the job is started immediately if inNumDependencies == 0 otherwise it starts when
	/// RemoveDependency causes the dependency counter to reach 0.
	virtual JobHandle		CreateJob(const char *inName, ColorArg inColor, const JobFunction &inJobFunction, uint32 inNumDependencies = 0) = 0;

	/// Create a new barrier, used to wait on jobs
	virtual Barrier *		CreateBarrier() = 0;

	/// Destroy a barrier when it is no longer used. The barrier should be empty at this point.
	virtual void			DestroyBarrier(Barrier *inBarrier) = 0;

	/// Wait for a set of jobs to be finished, note that only 1 thread can be waiting on a barrier at a time
	virtual void			WaitForJobs(Barrier *inBarrier) = 0;

protected:
	/// A class that contains information for a single unit of work
	class Job
	{
	public:
		/// Constructor
							Job(const char *inJobName, ColorArg inColor, JobSystem *inJobSystem, const JobFunction &inJobFunction, uint32 inNumDependencies) : 
		#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			mJobName(inJobName), 
			mColor(inColor), 
		#endif // defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			mJobSystem(inJobSystem), 
			mJobFunction(inJobFunction), 
			mReferenceCount(0), 
			mNumDependencies(inNumDependencies) 
		{ 
		}

		/// Get the jobs system to which this job belongs
		inline JobSystem *	GetJobSystem()								{ return mJobSystem; }

		/// Add or release a reference to this object
		inline void			AddRef() 									{ ++mReferenceCount; }
		inline void			Release()									{ if (--mReferenceCount == 0) mJobSystem->FreeJob(this); }

		/// Add to the dependency counter. 
		inline void			AddDependency(int inCount);

		/// Remove from the dependency counter. Returns true whenever the dependency counter reaches zero
		/// and if it does it is no longer valid to call the AddDependency/RemoveDependency functions.
		inline bool			RemoveDependency(int inCount);

		/// Remove from the dependency counter. Job will be queued whenever the dependency counter reaches zero
		/// and if it does it is no longer valid to call the AddDependency/RemoveDependency functions.
		inline void			RemoveDependencyAndQueue(int inCount);

		/// Set the job barrier that this job belongs to and returns false if this was not possible because the job already finished
		inline bool			SetBarrier(Barrier *inBarrier)
		{ 
			intptr_t barrier = 0; 
			if (mBarrier.compare_exchange_strong(barrier, reinterpret_cast<intptr_t>(inBarrier)))
				return true; 
			JPH_ASSERT(barrier == cBarrierDoneState, "A job can only belong to 1 barrier");
			return false;
		}

		/// Run the job function, returns the number of dependencies that this job still has or cExecutingState or cDoneState
		inline uint32		Execute()
		{
			// Transition job to executing state
			uint32 state = 0; // We can only start running with a dependency counter of 0
			if (!mNumDependencies.compare_exchange_strong(state, cExecutingState))
				return state; // state is updated by compare_exchange_strong to the current value

			// Run the job function
			{
				JPH_PROFILE(mJobName, mColor.GetUInt32());
				mJobFunction();
			}

			// Fetch the barrier pointer and exchange it for the done state, so we're sure that no barrier gets set after we want to call the callback
			intptr_t barrier;
			for (;;)
			{
				barrier = mBarrier;
				if (mBarrier.compare_exchange_strong(barrier, cBarrierDoneState))
					break;
			}
			JPH_ASSERT(barrier != cBarrierDoneState);

			// Mark job as done
			state = cExecutingState;
			mNumDependencies.compare_exchange_strong(state, cDoneState);
			JPH_ASSERT(state == cExecutingState);

			// Notify the barrier after we've changed the job to the done state so that any thread reading the state after receiving the callback will see that the job has finished
			if (barrier != 0)
				reinterpret_cast<Barrier *>(barrier)->OnJobFinished(this);

			return cDoneState;
		}

		/// Test if the job can be executed
		inline bool			CanBeExecuted() const						{ return mNumDependencies == 0; }

		/// Test if the job finished executing
		inline bool			IsDone() const								{ return mNumDependencies == cDoneState; }

		static constexpr uint32 cExecutingState = 0xe0e0e0e0;			///< Value of mNumDependencies when job is executing
		static constexpr uint32 cDoneState		= 0xd0d0d0d0;			///< Value of mNumDependencies when job is done executing

		static constexpr intptr_t cBarrierDoneState = ~intptr_t(0);		///< Value to use when the barrier has been triggered

private:
	#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char *		mJobName;									///< Name of the job
		Color				mColor;										///< Color of the job in the profiler
	#endif // defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		JobSystem *			mJobSystem;									///< The job system we belong to
		atomic<intptr_t>	mBarrier = 0;								///< Barrier that this job is associated with (is a Barrier pointer)
		JobFunction			mJobFunction;								///< Main job function
		atomic<uint32>		mReferenceCount;							///< Amount of JobHandles pointing to this job
		atomic<uint32>		mNumDependencies;							///< Amount of jobs that need to complete before this job can run
	};

	/// Adds a job to the job queue
	virtual void			QueueJob(Job *inJob) = 0;

	/// Adds a number of jobs at once to the job queue
	virtual void			QueueJobs(Job **inJobs, uint inNumJobs) = 0;

	/// Frees a job
	virtual void			FreeJob(Job *inJob) = 0;
};

using JobHandle = JobSystem::JobHandle;

} // JPH

#include <Core/JobSystem.inl>
