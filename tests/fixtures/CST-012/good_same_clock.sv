// sverity-args: --top top --sdc @DIR@/good_same_clock.sdc
// A consistent contract does not prove physical timing.
// expect-count: CST-012 INFO 1
// expect-none: CST-009
// expect-none: CST-018
// expect-none: CST-012 WARNING
// expect-none: CST-012 ERROR
module top(input logic clk, output logic [1:0] q);
  always_ff @(posedge clk) q <= q + 2'b01;
endmodule
