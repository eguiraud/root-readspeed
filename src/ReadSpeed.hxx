/* Copyright (C) 2020 Enrico Guiraud
   See the LICENSE file in the top directory for more information. */

#ifndef ROOTREADSPEED
#define ROOTREADSPEED

#include <ROOT/TSeq.hxx>
#include <ROOT/TThreadExecutor.hxx>
#include <ROOT/TTreeProcessorMT.hxx> // for TTreeProcessorMT::GetTasksPerWorkerHint
#include <ROOT/RDF/Utils.hxx>
#include <TFriendElement.h>
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
#include <set>
#include <regex>

namespace ReadSpeed {

struct Data {
   /// Either a single tree name common for all files, or one tree name per file.
   std::vector<std::string> fTreeNames;
   /// List of input files.
   std::vector<std::string> fFileNames;
   /// Branches to read.
   std::vector<std::string> fBranchNames;
   /// If the branch names should use regex matching.
   bool fUseRegex = false;
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

// Gets the names of all branches in a tree, accounts for friends.
// Copied straight from RDFInterfaceUtils
void GetTopLevelBranchNamesImpl(TTree &t, std::set<std::string> &bNamesReg, ROOT::RDF::ColumnNames_t &bNames,
                                std::set<TTree *> &analysedTrees, const std::string friendName = "")
{
   if (!analysedTrees.insert(&t).second) {
      return;
   }

   auto branches = t.GetListOfBranches();
   if (branches) {
      for (auto branchObj : *branches) {
         const auto name = branchObj->GetName();
         if (bNamesReg.insert(name).second) {
            bNames.emplace_back(name);
         } else if (!friendName.empty()) {
            // If this is a friend and the branch name has already been inserted, it might be because the friend
            // has a branch with the same name as a branch in the main tree. Let's add it as <friendname>.<branchname>.
            // If used for a Snapshot, this name will become <friendname>_<branchname> (with an underscore).
            const auto longName = friendName + "." + name;
            if (bNamesReg.insert(longName).second)
               bNames.emplace_back(longName);
         }
      }
   }

   auto friendTrees = t.GetListOfFriends();

   if (!friendTrees)
      return;

   for (auto friendTreeObj : *friendTrees) {
      auto friendElement = static_cast<TFriendElement *>(friendTreeObj);
      auto friendTree = friendElement->GetTree();
      const std::string frName(friendElement->GetName()); // this gets us the TTree name or the friend alias if any
      GetTopLevelBranchNamesImpl(*friendTree, bNamesReg, bNames, analysedTrees, frName);
   }
}

// Gets the names of all branches in a tree, accounts for friends.
// Copied straight from RDFInterfaceUtils
ROOT::RDF::ColumnNames_t GetTopLevelBranchNames(TTree &t)
{
   std::set<std::string> bNamesSet;
   ROOT::RDF::ColumnNames_t bNames;
   std::set<TTree *> analysedTrees;
   GetTopLevelBranchNamesImpl(t, bNamesSet, bNames, analysedTrees);
   return bNames;
}

std::vector<std::string> GetMatchingBranchNames(const std::string &fileName, const std::string &treeName,
                                                const std::vector<std::string> &regexes, bool useRegex)
{
   TFile *f = TFile::Open(fileName.c_str());
   if (f->IsZombie())
      throw std::runtime_error("Could not open file '" + fileName + '\'');
   std::unique_ptr<TTree> t(f->Get<TTree>(treeName.c_str()));
   if (t == nullptr)
      throw std::runtime_error("Could not retrieve tree '" + treeName + "' from file '" + fileName + '\'');

   const auto unfilteredBranchNames = GetTopLevelBranchNames(*t);
   std::set<std::string> usedRegexes;
   std::vector<std::string> branchNames;

   auto filterBranchName = [useRegex, regexes, &usedRegexes](std::string bName) {
      const auto matchBranch = [useRegex, &usedRegexes, bName](std::string regex) {
         bool match = false;
         if (!useRegex) {
            match = bName.compare(regex) == 0;
         } else {
            std::regex branchRegex(regex);
            match = std::regex_match(bName, branchRegex);
         }

         if (match)
            usedRegexes.insert(regex);

         return match;
      };

      const auto iterator = std::find_if(regexes.begin(), regexes.end(), matchBranch);
      return iterator != regexes.end();
   };
   std::copy_if(unfilteredBranchNames.begin(), unfilteredBranchNames.end(), std::back_inserter(branchNames),
                filterBranchName);

   if (branchNames.empty() && useRegex)
      throw std::runtime_error("Provided branch regexes didn't match any branches in the tree.");
   if (usedRegexes.size() != regexes.size()) {
      std::cout << "The following branches/regexes weren't found in the tree:" << std::endl;
      for (const auto &regex : regexes) {
         if (usedRegexes.find(regex) == usedRegexes.end())
            std::cout << "\t" + regex << std::endl;
      }
      throw std::runtime_error("Some branches/regexes weren't found in the tree...");
   }

   return branchNames;
}

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

   std::vector<TBranch *> branches;
   for (auto &bName : branchNames) {
      auto *b = t->GetBranch(bName.c_str());
      b->SetStatus(1);
      branches.push_back(b);
   }

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
      for (const auto &b : branches)
         bytesRead += b->GetEntry(e);

   const ULong64_t fileBytesRead = f->GetBytesRead() - fileStartBytes;
   return {bytesRead, fileBytesRead};
}

inline Result EvalThroughputST(const Data &d)
{
   auto treeIdx = 0;
   ULong64_t uncompressedBytesRead = 0;
   ULong64_t compressedBytesRead = 0;

   TStopwatch sw;

   for (const auto &fName : d.fFileNames) {
      const auto branchNames = GetMatchingBranchNames(fName, d.fTreeNames[treeIdx], d.fBranchNames, d.fUseRegex);

      sw.Start();

      const auto byteData = ReadTree(d.fTreeNames[treeIdx], fName, branchNames);
      uncompressedBytesRead += byteData.fUncompressedBytesRead;
      compressedBytesRead += byteData.fCompressedBytesRead;

      if (d.fTreeNames.size() > 1)
         ++treeIdx;

      sw.Stop();
   }

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

   auto treeIdx = 0;
   std::vector<std::vector<std::string>> fileBranchNames;
   for (const auto &fName : d.fFileNames) {
      const auto branchNames = GetMatchingBranchNames(fName, d.fTreeNames[treeIdx], d.fBranchNames, d.fUseRegex);
      fileBranchNames.push_back(branchNames);

      if (d.fTreeNames.size() > 1)
         ++treeIdx;
   }

   // for each file, for each range, spawn a reading task
   auto sumBytes = [](const std::vector<ByteData> &bytesData) -> ByteData {
      const auto uncompressedBytes =
         std::accumulate(bytesData.begin(), bytesData.end(), 0ull,
                         [](ULong64_t sum, const ByteData &o) { return sum + o.fUncompressedBytesRead; });
      const auto compressedBytes =
         std::accumulate(bytesData.begin(), bytesData.end(), 0ull,
                         [](ULong64_t sum, const ByteData &o) { return sum + o.fCompressedBytesRead; });

      return {uncompressedBytes, compressedBytes};
   };

   auto processFile = [&](int fileIdx) {
      const auto &fileName = d.fFileNames[fileIdx];
      const auto &treeName = d.fTreeNames.size() > 1 ? d.fTreeNames[fileIdx] : d.fTreeNames[0];
      const auto &branchNames = fileBranchNames[fileIdx];

      auto readRange = [&](const EntryRange &range) -> ByteData {
         return ReadTree(treeName, fileName, branchNames, range);
      };

      return pool.MapReduce(readRange, rangesPerFile[fileIdx], sumBytes);
   };

   TStopwatch sw;
   sw.Start();
   const auto totalByteData = pool.MapReduce(processFile, ROOT::TSeqUL{d.fFileNames.size()}, sumBytes);
   sw.Stop();

   return {sw.RealTime(),
           sw.CpuTime(),
           clsw.RealTime(),
           clsw.CpuTime(),
           totalByteData.fUncompressedBytesRead,
           totalByteData.fCompressedBytesRead,
           actualThreads};
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
