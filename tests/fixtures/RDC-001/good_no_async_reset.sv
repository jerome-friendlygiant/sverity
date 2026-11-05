// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_generated_reset.sv: the design has no reset at all. RDC-001 inventories every asynchronous reset root, so the safe case is the design without one.
// expect-none: RDC-001
module top(input logic clka, d, output logic q);
  always_ff @(posedge clka) q <= d;
endmodule
