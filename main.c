
/*
 * Based on https://github.com/soulxu/kvmsample
 *          https://github.com/dpw/kvm-hello-world
 * KVM API Sample.
 * Author: Xu He Jie xuhj@cn.ibm.com
 * Author: Kokan <kokaipeter@gmail.com>
 */
#include <assert.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <memory.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define RAM_SIZE 512000000
#define CODE_START 0x1000

struct kvm {
  int dev_fd;
  int vm_fd;
  __u64 ram_size;
  __u64 ram_start;
  int kvm_version;
  struct kvm_userspace_memory_region mem;

  struct vcpu *vcpus[10];
  int vcpu_number;
};

struct vcpu {
  int vcpu_id;
  int vcpu_fd;
  pthread_t vcpu_thread;
  struct kvm_run *kvm_run;
  int kvm_run_mmap_size;
  struct kvm_regs regs;
  struct kvm_sregs sregs;
  void *(*vcpu_thread_func)(void *);
};

void kvm_vcpu_set_ax(struct vcpu *vcpu, int ax) {
  if (ioctl(vcpu->vcpu_fd, KVM_GET_REGS, &(vcpu->sregs)) < 0) {
    perror("can not get sregs\n");
    exit(1);
  }

  vcpu->regs.rax = ax;

  if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &(vcpu->regs)) < 0) {
    perror("KVM SET REGS\n");
    exit(1);
  }
}

void kvm_reset_vcpu(struct vcpu *vcpu) {
  if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &(vcpu->sregs)) < 0) {
    perror("can not get sregs\n");
    exit(1);
  }

  vcpu->sregs.cs.selector = CODE_START;
  vcpu->sregs.cs.base = CODE_START * 16;
  vcpu->sregs.ss.selector = CODE_START;
  vcpu->sregs.ss.base = CODE_START * 16;
  vcpu->sregs.ds.selector = CODE_START;
  vcpu->sregs.ds.base = CODE_START * 16;
  vcpu->sregs.es.selector = CODE_START;
  vcpu->sregs.es.base = CODE_START * 16;
  vcpu->sregs.fs.selector = CODE_START;
  vcpu->sregs.fs.base = CODE_START * 16;
  vcpu->sregs.gs.selector = CODE_START;

  if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0) {
    perror("can not set sregs");
    exit(1);
  }

  vcpu->regs.rflags = 0x0000000000000002ULL;
  vcpu->regs.rip = 0;
  vcpu->regs.rax = vcpu->vcpu_id;
  vcpu->regs.rsp = 0xffffffff;
  vcpu->regs.rbp = 0;

  if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &(vcpu->regs)) < 0) {
    perror("KVM SET REGS\n");
    exit(1);
  }
}

volatile int loop_start = 0;

void *kvm_cpu_thread(void *data) {
  struct vcpu *vcpu = (struct vcpu *)data;
  int ret = 0;
  kvm_reset_vcpu(vcpu);

  while (1) {
    printf("#%d: KVM start run\n", vcpu->vcpu_id);
    ret = ioctl(vcpu->vcpu_fd, KVM_RUN, 0);

    if (ret < 0) {
      fprintf(stderr, "KVM_RUN failed\n");
      exit(1);
    }

    if (KVM_EXIT_IO == vcpu->kvm_run->exit_reason) {
      printf("#%d: KVM_EXIT_IO\n", vcpu->vcpu_id);

      int c = *(int *)((char *)(vcpu->kvm_run) + vcpu->kvm_run->io.data_offset);
      printf("#%d: out port: %d, data: %d\n", vcpu->vcpu_id,
             vcpu->kvm_run->io.port, c);

      if (vcpu->kvm_run->io.port == 1) {
        while (!loop_start)
          sleep(1);
        printf("#%d: loop starting...\n", vcpu->vcpu_id);
      }
      if (vcpu->kvm_run->io.port == 2)
        loop_start = 1;
      if (vcpu->kvm_run->io.port == 3)
        goto exit_kvm;
      if (vcpu->kvm_run->io.port == 4) {
        loop_start = 1;
        while (1)
          sleep(1);
      } // never returns from this VM exit
    } else {
      printf("KVM unhandled exit reason: %d\n", vcpu->kvm_run->exit_reason);
      goto exit_kvm;
    }
  }

exit_kvm:
  return 0;
}

void load_executable(struct kvm *kvm, char *executable_path) {
  int fd = open(executable_path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "can not open binary file\n");
    exit(1);
  }

  char *p = (char *)kvm->ram_start;

  for (int ret = 0; ret <= 0; ret = read(fd, p, 4096), p += ret)
    ;
}

struct kvm *kvm_init(void) {
  struct kvm *kvm = malloc(sizeof(struct kvm));
  kvm->dev_fd = open("/dev/kvm", O_RDWR);

  if (kvm->dev_fd < 0) {
    perror("open kvm device fault: ");
    return NULL;
  }

  kvm->kvm_version = ioctl(kvm->dev_fd, KVM_GET_API_VERSION, 0);

  return kvm;
}

int kvm_create_vm(struct kvm *kvm, int ram_size) {
  int ret = 0;
  kvm->vm_fd = ioctl(kvm->dev_fd, KVM_CREATE_VM, 0);

  if (kvm->vm_fd < 0) {
    perror("can not create vm");
    return -1;
  }

  kvm->ram_size = ram_size;
  kvm->ram_start =
      (__u64)mmap(NULL, kvm->ram_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  if ((void *)kvm->ram_start == MAP_FAILED) {
    perror("can not mmap ram");
    return -1;
  }

  kvm->mem.slot = 0;
  kvm->mem.guest_phys_addr = 0;
  kvm->mem.memory_size = kvm->ram_size;
  kvm->mem.userspace_addr = kvm->ram_start;

  ret = ioctl(kvm->vm_fd, KVM_SET_USER_MEMORY_REGION, &(kvm->mem));

  if (ret < 0) {
    perror("can not set user memory region");
    return ret;
  }

  return ret;
}

void kvm_clean_vcpu(struct vcpu *vcpu) {
  munmap(vcpu->kvm_run, vcpu->kvm_run_mmap_size);
  close(vcpu->vcpu_fd);
}

void kvm_clean(struct kvm *kvm) {
  for (int i = 0; i < kvm->vcpu_number; ++i) {
    kvm_clean_vcpu(kvm->vcpus[i]);
  }
  close(kvm->vm_fd);
  munmap((void *)kvm->ram_start, kvm->ram_size);

  close(kvm->dev_fd);
  free(kvm);
}

struct vcpu *kvm_init_vcpu(struct kvm *kvm, int vcpu_id, void *(*fn)(void *)) {
  struct vcpu *vcpu = malloc(sizeof(struct vcpu));
  vcpu->vcpu_id = vcpu_id;
  vcpu->vcpu_fd = ioctl(kvm->vm_fd, KVM_CREATE_VCPU, vcpu->vcpu_id);

  if (vcpu->vcpu_fd < 0) {
    perror("can not create vcpu");
    return NULL;
  }

  vcpu->kvm_run_mmap_size = ioctl(kvm->dev_fd, KVM_GET_VCPU_MMAP_SIZE, 0);

  if (vcpu->kvm_run_mmap_size < 0) {
    perror("can not get vcpu mmsize");
    return NULL;
  }

  printf("%d\n", vcpu->kvm_run_mmap_size);
  vcpu->kvm_run = mmap(NULL, vcpu->kvm_run_mmap_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, vcpu->vcpu_fd, 0);

  if (vcpu->kvm_run == MAP_FAILED) {
    perror("can not mmap kvm_run");
    return NULL;
  }

  vcpu->vcpu_thread_func = fn;
  return vcpu;
}

void kvm_run_vm(struct kvm *kvm) {
  int i = 0;

  for (i = 0; i < kvm->vcpu_number; i++) {
    struct vcpu *vcpu = kvm->vcpus[i];
    if (pthread_create(&(vcpu->vcpu_thread), (const pthread_attr_t *)NULL,
                       vcpu->vcpu_thread_func, vcpu) != 0) {
      perror("can not create kvm thread");
      exit(1);
    }
  }

  printf("all thread started\n");

  for (i = 0; i < kvm->vcpu_number; i++) {
    pthread_join(kvm->vcpus[i]->vcpu_thread, NULL);
    printf("thread %d joined\n", i);
  }
}

int main(int argc, char **argv) {
  struct kvm *kvm = kvm_init();

  if (kvm == NULL) {
    printf("kvm init fauilt\n");
    return 1;
  }

  if (kvm_create_vm(kvm, RAM_SIZE) < 0) {
    printf("create vm fault\n");
    goto exit;
  }

  char *executable_path = "test.bin";
  if (argc > 1)
    executable_path = argv[1];

  load_executable(kvm, executable_path);

  kvm->vcpu_number = 2;
  for (int i = 0; i < kvm->vcpu_number; ++i) {
    kvm->vcpus[i] = kvm_init_vcpu(kvm, i, kvm_cpu_thread);
  }

exit:
  kvm_run_vm(kvm);

  kvm_clean(kvm);

  return 0;
}
