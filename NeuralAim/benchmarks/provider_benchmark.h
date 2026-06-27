#ifndef PROVIDER_BENCHMARK_H
#define PROVIDER_BENCHMARK_H

namespace benchmarks
{
bool IsProviderBenchmarkRequested(int argc, char** argv);
int RunProviderBenchmarkCli(int argc, char** argv);
}

#endif // PROVIDER_BENCHMARK_H
