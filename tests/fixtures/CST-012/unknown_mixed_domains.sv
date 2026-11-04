// sverity-args: --top top --sdc @DIR@/unknown.sdc
// expect: CST-012 WARNING
// expect-none: CST-012 INFO
// expect-coverage: constraints INCOMPLETE
// revert: a ^ b => a
// revert-expect-none: CST-012 WARNING
module top(input logic clk_a, clk_b, output logic q);
 logic a,b;
 always_ff @(posedge clk_a) a <= ~a;
 always_ff @(posedge clk_b) b <= ~b;
 assign q = a ^ b;
endmodule
