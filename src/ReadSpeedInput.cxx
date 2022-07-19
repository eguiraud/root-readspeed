/* Copyright (C) 2020 Enrico Guiraud
   See the LICENSE file in the top directory for more information. */

#include "ReadSpeedInput.hxx"

#include <iostream>
#include <cstring>

namespace ReadSpeed {

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
   std::cout << "Compressed data read:\t\t" << r.fCompressedBytesRead << " bytes\n";

   std::cout << "Uncompressed throughput:\t" << r.fUncompressedBytesRead / r.fRealTime / 1024 / 1024 << " MB/s\n";
   std::cout << "Compressed throughput:\t\t" << r.fCompressedBytesRead / r.fRealTime / 1024 / 1024 << " MB/s\n";
}

Args ParseArgs(int argc, char **argv)
{
   // Print help message and exit if "--help"
   if (argc < 2 || (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0))) {
      std::cout << "Usage:\n"
                << "  root-readspeed --trees tname1 [tname2 ...]\n"
                << "                 --files fname1 [fname2 ...]\n"
                << "                 (--all-branches | --branches bname1 [bname2 ...] | --branches-regex bregex1 "
                   "[bregex2 ...])\n"
                << "                 [--threads nthreads]\n"
                << "  root-readspeed (--help|-h)\n";
      return {};
   }

   Data d;
   unsigned int nThreads = 0;

   enum class EArgState { kNone, kTrees, kFiles, kBranches, kThreads } argState = EArgState::kNone;
   enum class EBranchState { kNone, kRegular, kRegex, kAll } branchState = EBranchState::kNone;
   const auto branchOptionsErrMsg =
      "Options --all-branches, --branches, and --branches-regex are mutually exclusive. You can use only one.\n";

   for (int i = 1; i < argc; ++i) {
      std::string arg(argv[i]);

      if (arg.compare("--trees") == 0) {
         argState = EArgState::kTrees;
      } else if (arg.compare("--files") == 0) {
         argState = EArgState::kFiles;
      } else if (arg.compare("--all-branches") == 0) {
         argState = EArgState::kNone;
         if (branchState != EBranchState::kNone && branchState != EBranchState::kAll) {
            std::cerr << branchOptionsErrMsg;
            return {};
         }
         branchState = EBranchState::kAll;
         d.fUseRegex = true;
         d.fBranchNames = {".*"};
      } else if (arg.compare("--branches") == 0) {
         argState = EArgState::kBranches;
         if (branchState != EBranchState::kNone && branchState != EBranchState::kRegular) {
            std::cerr << branchOptionsErrMsg;
            return {};
         }
         branchState = EBranchState::kRegular;
      } else if (arg.compare("--branches-regex") == 0) {
         argState = EArgState::kBranches;
         if (branchState != EBranchState::kNone && branchState != EBranchState::kRegex) {
            std::cerr << branchOptionsErrMsg;
            return {};
         }
         branchState = EBranchState::kRegex;
         d.fUseRegex = true;
      } else if (arg.compare("--threads") == 0) {
         argState = EArgState::kThreads;
      } else {
         switch (argState) {
         case EArgState::kTrees: d.fTreeNames.emplace_back(argv[i]); break;
         case EArgState::kFiles: d.fFileNames.emplace_back(argv[i]); break;
         case EArgState::kBranches: d.fBranchNames.emplace_back(argv[i]); break;
         case EArgState::kThreads: nThreads = std::atoi(argv[i]); break;
         default: std::cerr << "Unrecognized option '" << argv[i] << "'\n"; return {};
         }
      }
   }

   return Args{std::move(d), nThreads, branchState == EBranchState::kAll, /*fShouldRun=*/true};
}

} // namespace ReadSpeed