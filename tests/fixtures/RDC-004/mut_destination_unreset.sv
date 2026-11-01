// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_same_reset.sv: the destination register loses its reset.
// expect: RDC-004 WARNING
module top(input logic clka, rst_a, d, output logic q);
  logic s;
  always_ff @(posedge clka or negedge rst_a) if (!rst_a) s <= 1'b0; else s <= d;
  always_ff @(posedge clka) q <= s;
endmodule
