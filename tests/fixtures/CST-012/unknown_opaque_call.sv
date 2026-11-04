// sverity-args: --top top --sdc @DIR@/async.sdc
// expect: CST-012 WARNING
// expect-none: CST-012 INFO
// expect-coverage: constraints INCOMPLETE
// revert: opaque(a) => a
// revert-expect-none: CST-012 WARNING
module top(input logic clk_a, clk_b, output logic q);
 import "DPI-C" function bit opaque(input bit a);
 logic a;
 always_ff @(posedge clk_a) a <= ~a;
 assign q = opaque(a);
endmodule
