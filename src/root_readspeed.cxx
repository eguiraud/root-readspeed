/* Copyright (C) 2020 Enrico Guiraud
   See the LICENSE file in the top directory for more information. */

#include "ReadSpeed.hxx"

#include <iostream>
#include <cstring>

using namespace ReadSpeed;

void PrintThroughput(const Result &r)
{
   std::cout << "Thread pool size:\t\t" << r.fThreadPoolSize << '\n';

   if (r.fMTSetupRealTime > 0.) {
      std::cout << "Real time to setup MT run:\t" << r.fMTSetupRealTime << " s\n";
      std::cout << "CPU time to setup MT run:\t" << r.fMTSetupCpuTime << " s\n";
   }

   std::cout << "Real time:\t\t\t" << r.fRealTime << " s\n";
   std::cout << "CPU time:\t\t\t" << r.fCpuTime << " s\n";
   std::cout << "Uncompressed data read:\t\t" << r.fUncompressedBytesRead << " bytes\n";

   std::cout << "Throughput:\t\t\t" << r.fUncompressedBytesRead / r.fRealTime / 1024 / 1024 << " MB/s\t" << r.fUncompressedBytesRead / r.fRealTime / 1024 / 1024 / r.fThreadPoolSize<< " MB/s/thread for " << r.fThreadPoolSize<< " threads\n";
}

struct Args {
   Data fData;
   unsigned int fNThreads = 0;
   bool fShouldRun = false;
};

Args ParseArgs(int argc, char **argv)
{
   // Print help message and exit if "--help"
   if (argc < 2 || (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0))) {
      std::cout << "Usage:\n"
                << "  root-readspeed --trees tname1 [tname2 ...] --files fname1 [fname2 ...]\n"
                << "                 --branches bname1 [bname2 ...] [--threads nthreads]\n"
                << "  root-readspeed (--help|-h)\n";
      return {};
   }

   Data d;
   unsigned int nThreads = 0;

   enum class EArgState { kNone, kTrees, kFiles, kBranches, kThreads, kTasksPerWorkerHint } argState = EArgState::kNone;
   for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--trees") == 0)
         argState = EArgState::kTrees;
      else if (std::strcmp(argv[i], "--files") == 0)
         argState = EArgState::kFiles;
      else if (std::strcmp(argv[i], "--branches") == 0)
         argState = EArgState::kBranches;
      else if (std::strcmp(argv[i], "--threads") == 0)
         argState = EArgState::kThreads;
      else if (std::strcmp(argv[i], "--tasksPerWorker") == 0)
        argState = EArgState::kTasksPerWorkerHint;
      else if (argv[i][0] == '-') {
        argState = EArgState::kNone;
        std::cerr << "Unrecognized option '" << argv[i] << "'\n"; return {};
      } else {
         switch (argState) {
         case EArgState::kTrees: d.fTreeNames.emplace_back(argv[i]); break;
         case EArgState::kFiles: d.fFileNames.emplace_back(argv[i]); break;
         case EArgState::kBranches: d.fBranchNames.emplace_back(argv[i]); break;
         case EArgState::kThreads: nThreads = std::atoi(argv[i]); argState = EArgState::kNone; break;
         case EArgState::kTasksPerWorkerHint: ROOT::TTreeProcessorMT::SetTasksPerWorkerHint(std::atoi(argv[i])); argState = EArgState::kNone; break;
         default: std::cerr << "Unrecognized option '" << argv[i] << "'\n"; return {};
         }
      }
   }

   return Args{std::move(d), nThreads, /*fShouldRun=*/true};
}

int main(int argc, char **argv)
{
   const auto args = ParseArgs(argc, argv);
   if (!args.fShouldRun)
      return 1; // ParseArgs has printed the --help, has run the --test or has encountered an issue and logged about it

   PrintThroughput(EvalThroughput(args.fData, args.fNThreads));

   return 0;
}
