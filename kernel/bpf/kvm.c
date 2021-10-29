#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/kvm.h>

static const struct bpf_func_proto *
kvm_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_copy_from_user:
		return prog->aux->sleepable ? &bpf_copy_from_user_proto : NULL;
	case BPF_FUNC_copy_to_user:
		return prog->aux->sleepable ? &bpf_copy_to_user_proto : NULL;
	case BPF_FUNC_syscall:
		return prog->aux->sleepable ? &bpf_syscall_proto : NULL;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static bool kvm_prog_is_valid_access(int off, int size, enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
	if (type != BPF_READ)
		return false;
	if (off < 0 ||
	    off >= sizeof(struct kvm_run) ||
	    off + size > sizeof(struct kvm_run))
		return false;
	if (off % size != 0)
		return false;
	return true;
}

const struct bpf_verifier_ops kvm_verifier_ops = {
	.get_func_proto  = kvm_prog_func_proto,
	.is_valid_access = kvm_prog_is_valid_access,
};

const struct bpf_prog_ops kvm_prog_ops = {
};

