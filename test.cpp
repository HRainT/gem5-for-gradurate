#include <iostream>
#include <random>

#define INSERT_UNCORRELATED 1

int main(int argc, char** argv) {
  std::random_device random_device;
  std::mt19937 rng0(random_device());
  std::mt19937 rng1(random_device());

  if (argc != 5) {
    std::cout << "bad arguments\n";
    return 0;
  }

  float alpha = ::atof(argv[1]);
  int Nmin = ::atoi(argv[2]);
  int Nmax = ::atoi(argv[3]);
  int Rounds = ::atoi(argv[4]);

  std::uniform_int_distribution<> rand_dist0(Nmin, Nmax);
  std::uniform_real_distribution<> rand_dist1(0.0, 1.0);
#if INSERT_UNCORRELATED == 1
  std::uniform_int_distribution<> rand_dist2(10, 20);
#endif

  int volatile y = 0;
  for (int i = 0; i < Rounds; ++i) {

    int64_t N = rand_dist0(rng0);
    std::vector<int64_t> predicates(N);
    for (int j = 0; j < N; ++j) {
      predicates[j] = (rand_dist1(rng1) <= alpha) ? 1 : 0;
    }

#if INSERT_UNCORRELATED == 1
    int64_t M = rand_dist2(rng0);
    std::vector<int64_t> uncorrelated_predicates(M);
    for (int j = 0; j < M; ++j) {
      uncorrelated_predicates[j] = (rand_dist1(rng1) <= 0.5) ? 1 : 0;
    }
#endif

    for (int j = 0; j < 300; ++j) {
      y += 1;
    }
    /*
     */
    asm volatile(
        "lea (%1), %%rax\n"
        "movq %2, %%rcx\n"
        "movq $0, %%rdx\n"

        "movq $0, %%rbx\n"
        "loop1: testq $1, (%%rax, %%rbx, 8)\n"
        "je skip\n"
        "add $1, %%rdx\n"
        "skip: add $1, %%rbx\n"
        "cmp %%rbx, %%rcx\n"
        "jne loop1\n"

#if INSERT_UNCORRELATED == 1
        "lea (%3), %%rax\n"
        "movq %4, %%rcx\n"
        "movq $0, %%rbx\n"
        "loop_uncorrelated: testq $1, (%%rax, %%rbx, 8)\n"
        "je skip_uncorrelated\n"
        "add $1, %0 \n"
        "skip_uncorrelated: add $1, %%rbx\n"
        "cmp %%rbx, %%rcx\n"
        "jne loop_uncorrelated\n"
#endif

        "movq %%rdx, %%rbx\n"
        "loop2: test %%rbx, %%rbx\n"
        "jz exit\n"
        "add $1, %0\n"
        "add $-1, %%rbx\n"
        "jmp loop2\n"
        "exit: add $0, %%edx\n"
        : "=m"(y)
        : "g"(&predicates[0]), "g"(N)
#if INSERT_UNCORRELATED == 1
        , "g"(&uncorrelated_predicates[0]), "g"(M)
#endif
        : "rax", "rbx", "rcx", "rdx", "memory");

    /*
    int x = 0;
    for (int j = 0; j < N; ++j) {
      if (predicates[j] == 1) {
        x += 1;
      }
    }

    for (int j = 0; j < x; ++j) {
      y += 1;
    }
       */
  }
  std::cout << "\n";
  std::cout << y << "\n";
}