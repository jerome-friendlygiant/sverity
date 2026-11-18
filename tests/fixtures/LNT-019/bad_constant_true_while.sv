// sverity-args: --top top
// expect: LNT-019 WARNING 4
module top(input logic d, output logic q);
always_comb begin q = d; while (1) q = ~d; end
endmodule
// revert: while (1) => while (0)
// revert-expect-none: LNT-019
