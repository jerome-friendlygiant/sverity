// sverity-args: --top top --sdc @DIR@/async.sdc
// expect: CST-012 WARNING
// expect-none: CST-012 INFO
// expect-coverage: constraints INCOMPLETE
// revert: missing u(.o(q)); => assign q = a;
// revert-expect-none: CST-012 WARNING
module top(input logic clk_a, clk_b, output logic q);
 logic a;
 always_ff @(posedge clk_a) a <= ~a;
 missing u(.o(q));
endmodule
