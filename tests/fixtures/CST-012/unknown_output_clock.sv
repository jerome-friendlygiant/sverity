// sverity-args: --top top --sdc @DIR@/unknown_output_clock.sdc
// expect: CST-018 WARNING
// expect-none: CST-009
// expect-none: CST-012
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= !d;
endmodule
