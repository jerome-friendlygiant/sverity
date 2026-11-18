// sverity-args: --top top
// expect: LNT-019 WARNING 4
module top(input logic d, output logic q);
always_comb begin q = 0; for (int i=0; i<4;) q = d; end
endmodule
// revert: i<4;) => i<4;i++)
// revert-expect-none: LNT-019
