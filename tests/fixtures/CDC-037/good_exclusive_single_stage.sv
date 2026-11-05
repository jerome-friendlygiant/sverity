// sverity-args: --top top --sdc @FIXTURES@/CDC-003/excl.sdc
// Near miss of bad_exclusive_with_synchroniser.sv: the destination is one register, not a chain of depth 2, so there is no synchronised crossing between the exclusive clocks.
// expect-none: CDC-037
module top(input logic clka, clkb, d, output logic q);
  logic launch;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) q <= launch;
endmodule
