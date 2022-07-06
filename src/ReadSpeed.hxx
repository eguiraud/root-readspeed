/* Copyright (C) 2020 Enrico Guiraud
   See the LICENSE file in the top directory for more information. */

#ifndef ROOTREADSPEED
#define ROOTREADSPEED

#include <ROOT/TSeq.hxx>
#include <ROOT/TThreadExecutor.hxx>
#include <ROOT/TTreeProcessorMT.hxx> // for TTreeProcessorMT::GetTasksPerWorkerHint
#include <TBranch.h>
#include <TFile.h>
#include <TStopwatch.h>
#include <TTree.h>

#include <algorithm>
#include <cassert>
#include <cmath> // std::ceil
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ReadSpeed {

struct Data {
   /// Either a single tree name common for all files, or one tree name per file.
   std::vector<std::string> fTreeNames;
   /// List of input files.
   std::vector<std::string> fFileNames;
   /// Branches to read.
   std::vector<std::string> fBranchNames;
};

struct Result {
   /// Real time spent reading and decompressing all data, in seconds.
   double fRealTime;
   /// CPU time spent reading and decompressing all data, in seconds.
   double fCpuTime;
   /// Real time spent preparing the multi-thread workload.
   double fMTSetupRealTime;
   /// CPU time spent preparing the multi-thread workload.
   double fMTSetupCpuTime;
   /// Number of uncompressed bytes read in total from TTree branches.
   ULong64_t fUncompressedBytesRead;
   /// Number of compressed bytes read in total from the TFiles.
   ULong64_t fCompressedBytesRead;
   /// Size of ROOT's thread pool for the run (0 indicates a single-thread run with no thread pool present).
   unsigned int fThreadPoolSize;
   // TODO returning zipped bytes read too might be interesting, e.g. to estimate network I/O speed
};

struct EntryRange {
   Long64_t fStart = -1;
   Long64_t fEnd = -1;
};

struct ByteData {
   ULong64_t fUncompressedBytesRead;
   ULong64_t fCompressedBytesRead;
};

// Read branches listed in branchNames in tree treeName in file fileName, return number of uncompressed bytes read.
inline ByteData ReadTree(const std::string &treeName, const std::string &fileName,
                          const std::vector<std::string> &branchNames, EntryRange range = {-1, -1})
{
   // This logic avoids re-opening the same file many times if not needed
   // Given the static lifetime of `f`, we cannot use a `unique_ptr<TFile>` lest we have issues at teardown
   // (e.g. because this file outlives ROOT global lists). Instead we rely on ROOT's memory management.
   thread_local TFile *f;
   if (f == nullptr || f->GetName() != fileName) {
     delete f;
     f = TFile::Open(fileName.c_str()); // TFile::Open uses plug-ins if needed
   }

   if (f->IsZombie())
      throw std::runtime_error("Could not open file '" + fileName + '\'');
   std::unique_ptr<TTree> t(f->Get<TTree>(treeName.c_str()));
   if (t == nullptr)
      throw std::runtime_error("Could not retrieve tree '" + treeName + "' from file '" + fileName + '\'');
   t->SetBranchStatus("*", 0);
   std::vector<TBranch *> branches(branchNames.size());
   auto getBranch = [&t](const std::string &bName) {
      auto *b = t->GetBranch(bName.c_str());
      if (b == nullptr)
         throw std::runtime_error("Could not retrieve branch '" + bName + "' from tree '" + t->GetName() +
                                  "' in file '" + t->GetCurrentFile()->GetName() + '\'');
      b->SetStatus(1);
      return b;
   };
   std::transform(branchNames.begin(), branchNames.end(), branches.begin(), getBranch);

   const auto nEntries = t->GetEntries();
   if (range.fStart == -1ll)
      range = EntryRange{0ll, nEntries};
   else if (range.fEnd > nEntries)
      throw std::runtime_error("Range end (" + std::to_string(range.fEnd) + ") is beyod the end of tree '" +
                               t->GetName() + "' in file '" + t->GetCurrentFile()->GetName() + "' with " +
                               std::to_string(nEntries) + " entries.");
   
   ULong64_t bytesRead = 0;
   const ULong64_t fileStartBytes = f->GetBytesRead();
   for (auto e = range.fStart; e < range.fEnd; ++e)
      for (auto b : branches)
         bytesRead += b->GetEntry(e);
   
   const ULong64_t fileBytesRead = f->GetBytesRead() - fileStartBytes;

   return { bytesRead, fileBytesRead };
}

inline Result EvalThroughputST(const Data &d)
{
   TStopwatch sw;
   sw.Start();

   auto treeIdx = 0;
   ULong64_t uncompressedBytesRead = 0;
   ULong64_t compressedBytesRead = 0;

   for (const auto &fName : d.fFileNames) {
      const auto byteData = ReadTree(d.fTreeNames[treeIdx], fName, d.fBranchNames);
      uncompressedBytesRead += byteData.fUncompressedBytesRead;
      compressedBytesRead += byteData.fCompressedBytesRead;

      if (d.fTreeNames.size() > 1)
         ++treeIdx;
   }

   sw.Stop();

   return {sw.RealTime(), sw.CpuTime(), 0., 0., uncompressedBytesRead, compressedBytesRead, 0};
}

// Return a vector of EntryRanges per file, i.e. a vector of vectors of EntryRanges with outer size equal to
// d.fFileNames.
inline std::vector<std::vector<EntryRange>> GetClusters(const Data &d)
{
   auto treeIdx = 0;
   const auto nFiles = d.fFileNames.size();
   std::vector<std::vector<EntryRange>> ranges(nFiles);
   for (auto fileIdx = 0u; fileIdx < nFiles; ++fileIdx) {
      const auto &fileName = d.fFileNames[fileIdx];
      std::unique_ptr<TFile> f(TFile::Open(fileName.c_str()));
      if (f->IsZombie())
         throw std::runtime_error("There was a problem opening file '" + fileName + '\'');
      const auto &treeName = d.fTreeNames.size() > 1 ? d.fTreeNames[fileIdx] : d.fTreeNames[0];
      auto *t = f->Get<TTree>(treeName.c_str()); // TFile owns this TTree
      if (t == nullptr)
         throw std::runtime_error("There was a problem retrieving TTree '" + treeName + "' from file '" + fileName +
                                  '\'');

      const auto nEntries = t->GetEntries();
      auto it = t->GetClusterIterator(0);
      Long64_t start = 0;
      std::vector<EntryRange> rangesInFile;
      while ((start = it.Next()) < nEntries)
         rangesInFile.emplace_back(EntryRange{start, it.GetNextEntry()});
      ranges[fileIdx] = std::move(rangesInFile);
      if (d.fTreeNames.size() > 1)
         ++treeIdx;
   }
   return ranges;
}

// Mimic the logic of TTreeProcessorMT::MakeClusters: merge entry ranges together such that we
// run around TTreeProcessorMT::GetTasksPerWorkerHint tasks per worker thread.
// TODO it would be better to expose TTreeProcessorMT's actual logic and call the exact same method from here
inline std::vector<std::vector<EntryRange>>
MergeClusters(std::vector<std::vector<EntryRange>> &&clusters, unsigned int maxTasksPerFile)
{
   std::vector<std::vector<EntryRange>> mergedClusters(clusters.size());

   auto clustersIt = clusters.begin();
   auto mergedClustersIt = mergedClusters.begin();
   for (; clustersIt != clusters.end(); clustersIt++, mergedClustersIt++) {
      const auto nClustersInThisFile = clustersIt->size();
      const auto nFolds = nClustersInThisFile / maxTasksPerFile;
      // If the number of clusters is less than maxTasksPerFile
      // we take the clusters as they are
      if (nFolds == 0) {
         *mergedClustersIt = *clustersIt;
         continue;
      }
      // Otherwise, we have to merge clusters, distributing the reminder evenly
      // between the first clusters
      auto nReminderClusters = nClustersInThisFile % maxTasksPerFile;
      const auto &clustersInThisFile = *clustersIt;
      for (auto i = 0ULL; i < nClustersInThisFile; ++i) {
         const auto start = clustersInThisFile[i].fStart;
         // We lump together at least nFolds clusters, therefore
         // we need to jump ahead of nFolds-1.
         i += (nFolds - 1);
         // We now add a cluster if we have some reminder left
         if (nReminderClusters > 0) {
            i += 1U;
            nReminderClusters--;
         }
         const auto end = clustersInThisFile[i].fEnd;
         mergedClustersIt->emplace_back(EntryRange({start, end}));
      }
      assert(nReminderClusters == 0 && "This should never happen, cluster-merging logic is broken.");
   }

   return mergedClusters;
}

inline Result EvalThroughputMT(const Data &d, unsigned nThreads)
{
   ROOT::TThreadExecutor pool(nThreads);
   const auto actualThreads = ROOT::GetThreadPoolSize();
   if (actualThreads != nThreads)
      std::cerr << "Running with " << actualThreads << " threads even though " << nThreads << " were requested.\n";

   TStopwatch clsw;
   clsw.Start();
   const unsigned int maxTasksPerFile =
      std::ceil(float(ROOT::TTreeProcessorMT::GetTasksPerWorkerHint() * actualThreads) / float(d.fFileNames.size()));

   const auto rangesPerFile = MergeClusters(GetClusters(d), maxTasksPerFile);
   clsw.Stop();

   // for each file, for each range, spawn a reading task
   auto sumBytes = [](const std::vector<ByteData> &bytesData) -> ByteData {
      const auto uncompressedBytes = std::accumulate(bytesData.begin(), bytesData.end(), 0ull, [](int, const ByteData& o){ return o.fUncompressedBytesRead; });
      const auto compressedBytes = std::accumulate(bytesData.begin(), bytesData.end(), 0ull, [](int, const ByteData& o){ return o.fCompressedBytesRead; });

      return { uncompressedBytes, compressedBytes };
   };

   auto processFile = [&](int fileIdx) {
      const auto &fileName = d.fFileNames[fileIdx];
      const auto &treeName = d.fTreeNames.size() > 1 ? d.fTreeNames[fileIdx] : d.fTreeNames[0];

      auto readRange = [&](const EntryRange &range) -> ByteData {
         return ReadTree(treeName, fileName, d.fBranchNames, range);
      };

      return pool.MapReduce(readRange, rangesPerFile[fileIdx], sumBytes);
   };

   TStopwatch sw;
   sw.Start();
   const auto totalByteData = pool.MapReduce(processFile, ROOT::TSeqUL{d.fFileNames.size()}, sumBytes);
   sw.Stop();

   return {sw.RealTime(), sw.CpuTime(), clsw.RealTime(), clsw.CpuTime(), totalByteData.fUncompressedBytesRead, totalByteData.fCompressedBytesRead, actualThreads};
}

inline Result EvalThroughput(const Data &d, unsigned nThreads)
{
   if (d.fTreeNames.empty())
      throw std::runtime_error("Please provide at least one tree name");
   if (d.fFileNames.empty())
      throw std::runtime_error("Please provide at least one file name");
   if (d.fBranchNames.empty())
      throw std::runtime_error("Please provide at least one branch name");
   if (d.fTreeNames.size() != 1 && d.fTreeNames.size() != d.fFileNames.size())
      throw std::runtime_error("Please provide either one tree name or as many as the file names");

   return nThreads > 0 ? EvalThroughputMT(d, nThreads) : EvalThroughputST(d);
}

} // namespace ReadSpeed

#endif // ROOTREADSPEED
