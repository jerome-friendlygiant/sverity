// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION baseline: both registers are reset by the same source.
// expect-none: RDC-004
module top(input logic clka, rst_a, d, output logic q);
  logic s;
  always_ff @(posedge clka or negedge rst_a) if (!rst_a) s <= 1'b0; else s <= d;
  always_ff @(posedge clka or negedge rst_a) if (!rst_a) q <= 1'b0; else q <= s;
endmodule
