/* Copyright (C) 2020 Enrico Guiraud
   See the LICENSE file in the top directory for more information. */

#include <ROOT/TSeq.hxx>
#include <ROOT/TThreadExecutor.hxx>
#include <TBranch.h>
#include <TFile.h>
#include <TStopwatch.h>
#include <TSystem.h>
#include <TTree.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Data {
   /// Either a single tree name common for all files, or one tree name per file.
   std::vector<std::string> fTreeNames;
   /// List of input files.
   // TODO add support for globbing
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
   // TODO returning zipped bytes read too might be interesting, e.g. to estimate network I/O speed
};

struct EntryRange {
   Long64_t fStart = -1;
   Long64_t fEnd = -1;
};

// Read branches listed in branchNames in tree treeName in file fileName, return number of uncompressed bytes read.
ULong64_t ReadTree(const std::string &treeName, const std::string &fileName,
                   const std::vector<std::string> &branchNames, EntryRange range = {-1, -1})
{
   auto f = std::unique_ptr<TFile>(TFile::Open(fileName.c_str())); // TFile::Open uses plug-ins if needed
   if (f->IsZombie())
      throw std::runtime_error("There was a problem opening file \"" + fileName + '"');
   auto *t = f->Get<TTree>(treeName.c_str());
   if (t == nullptr)
      throw std::runtime_error("There was a problem retrieving TTree \"" + treeName + "\" from file \"" + fileName +
                               '"');
   t->SetBranchStatus("*", 0);
   std::vector<TBranch *> branches(branchNames.size());
   auto getBranch = [t](const std::string &bName) {
      auto *b = t->GetBranch(bName.c_str());
      if (b == nullptr)
         throw std::runtime_error("There was a problem retrieving TBranch \"" + bName + "\" from TTree \"" +
                                  t->GetName() + "\" in file \"" + t->GetCurrentFile()->GetName() + '"');
      b->SetStatus(1);
      return b;
   };
   std::transform(branchNames.begin(), branchNames.end(), branches.begin(), getBranch);

   const auto nEntries = t->GetEntries();
   if (range.fStart == -1ll)
      range = EntryRange{0ll, nEntries};
   else if (range.fEnd > nEntries)
      throw std::runtime_error("Range end (" + std::to_string(range.fEnd) + ") is beyod the end of tree \"" +
                               t->GetName() + "\" in file \"" + t->GetCurrentFile()->GetName() + "\" with " +
                               std::to_string(nEntries) + " entries.");
   ULong64_t bytesRead = 0;
   for (auto e = range.fStart; e < range.fEnd; ++e)
      for (auto b : branches)
         bytesRead += b->GetEntry(e);

   return bytesRead;
}

Result EvalThroughputST(const Data &d)
{
   TStopwatch sw;
   sw.Start();

   auto treeIdx = 0;
   ULong64_t bytesRead = 0;
   for (const auto &fName : d.fFileNames) {
      bytesRead += ReadTree(d.fTreeNames[treeIdx], fName, d.fBranchNames);
      if (d.fTreeNames.size() > 1)
         ++treeIdx;
   }

   sw.Stop();

   return {sw.RealTime(), sw.CpuTime(), 0., 0., bytesRead};
}

// Return a vector of EntryRanges per file, i.e. a vector of vectors of EntryRanges with outer size equal to
// d.fFileNames.
std::vector<std::vector<EntryRange>> GetClusters(const Data &d) {
   auto treeIdx = 0;
   const auto nFiles = d.fFileNames.size();
   std::vector<std::vector<EntryRange>> ranges(nFiles);
   for (auto fileIdx = 0u; fileIdx < nFiles; ++fileIdx) {
      const auto &fileName = d.fFileNames[fileIdx];
      std::unique_ptr<TFile> f(TFile::Open(fileName.c_str()));
      if (f->IsZombie())
         throw std::runtime_error("There was a problem opening file \"" + fileName + '"');
      const auto &treeName = d.fTreeNames.size() > 1 ? d.fTreeNames[fileIdx] : d.fTreeNames[0];
      auto *t = f->Get<TTree>(treeName.c_str()); // TFile owns this TTree
      if (t == nullptr)
         throw std::runtime_error("There was a problem retrieving TTree \"" + treeName + "\" from file \"" + fileName +
                                  '"');

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

std::vector<std::vector<EntryRange>> MergeClusters(std::vector<std::vector<EntryRange>> &&clusters)
{
   // TODO to implement
   return std::move(clusters);
}

Result EvalThroughputMT(const Data &d, unsigned nThreads)
{
   TStopwatch clsw;
   clsw.Start();
   const auto clusters = MergeClusters(GetClusters(d));
   clsw.Stop();

   // for each cluster, spawn a reading task
   std::atomic_ullong bytesRead{0};

   ROOT::TThreadExecutor pool(nThreads);

   auto processFile = [&] (int fileIdx) mutable {
      const auto &fileName = d.fFileNames[fileIdx];
      const auto &treeName = d.fTreeNames.size() > 1 ? d.fTreeNames[fileIdx] : d.fTreeNames[0];

      auto processCluster = [&] (const EntryRange &range) mutable {
         std::cout << "range: " << range.fStart << " " << range.fEnd << '\n';
         bytesRead += ReadTree(treeName, fileName, d.fBranchNames, range);
      };

      pool.Foreach(processCluster, clusters[fileIdx]);
   };

   TStopwatch sw;
   sw.Start();
   pool.Foreach(processFile, ROOT::TSeqUL{d.fFileNames.size()});
   sw.Stop();

   return {sw.RealTime(), sw.CpuTime(), clsw.RealTime(), clsw.CpuTime(), bytesRead};
}

Result EvalThroughput(const Data &d, unsigned nThreads)
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

void PrintThroughput(const Result &r)
{
   if (r.fMTSetupRealTime > 0.) {
      std::cout << "Real time to setup MT run:\t" << r.fMTSetupRealTime << " s\n";
      std::cout << "CPU time to setup MT run:\t" << r.fMTSetupCpuTime << " s\n";
   }

   std::cout << "Real time:\t\t\t" << r.fRealTime << " s\n";
   std::cout << "CPU time:\t\t\t" << r.fCpuTime << " s\n";
   std::cout << "Uncompressed data read:\t\t" << r.fUncompressedBytesRead << " bytes\n";

   std::cout << "Throughput:\t\t\t" << r.fUncompressedBytesRead / r.fRealTime / 1024 / 1024 << " MB/s\n";
}

void RequireFile(const std::string &fname)
{
   if (gSystem->AccessPathName(fname.c_str()) == false) // then the file already exists: weird return value convention
      return; // nothing to do

   TFile f(fname.c_str(), "recreate");
   TTree t("t", "t");
   int x = 42;
   t.Branch("x", &x);
   for (int i = 0; i < 10000000; ++i)
      t.Fill();
   t.Write();
}

void TestST()
{
   RequireFile("test1.root");
   RequireFile("test2.root");

   const auto result = EvalThroughput({{"t"}, {"test1.root", "test2.root"}, {"x"}}, 0);
   assert(result.fUncompressedBytesRead == 80000000 && "Wrong number of bytes read");
   std::cout << "\n**** Single-thread test ****\n";
   PrintThroughput(result);
}

void TestMT()
{
   RequireFile("test1.root");
   RequireFile("test2.root");

   const auto result = EvalThroughput({{"t"}, {"test1.root", "test2.root"}, {"x"}}, 4);
   assert(result.fUncompressedBytesRead == 80000000 && "Wrong number of bytes read");
   std::cout << "\n**** Multi-thread test ****\n";
   PrintThroughput(result);
}

int main()
{
   TestST();
   TestMT();

   return 0;
}
