	.file	"tls-probe.c"
	.text
	.globl	bump                            // -- Begin function bump
	.p2align	2
	.type	bump,@function
bump:                                   // @bump
// %bb.0:
	mrs	x8, TPIDR_EL0
	add	x8, x8, :tprel_hi12:counter
	add	x8, x8, :tprel_lo12_nc:counter
	ldr	w9, [x8]
	add	w0, w9, #1
	str	w0, [x8]
	ret
.Lfunc_end0:
	.size	bump, .Lfunc_end0-bump
                                        // -- End function
	.type	counter,@object                 // @counter
	.section	.tdata,"awT",@progbits
	.p2align	2, 0x0
counter:
	.word	3                               // 0x3
	.size	counter, 4

	.ident	"Apple clang version 21.0.0 (clang-2100.3.34.2)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
